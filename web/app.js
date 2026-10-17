// objetismo in the browser: decodes the image, runs the WebAssembly core on a
// pool of workers and draws the mosaic from tiles.png. Nothing is uploaded.

const $ = (id) => document.getElementById(id);

// Safari caps a canvas at 16.7M pixels; the others allow more, but stay safe.
const MAX_CANVAS_AREA = 16_777_216;
const MAX_CANVAS_SIDE = 16_384;
const MAX_INPUT_PIXELS = 50_000_000;
const MAX_COLS = 400;

// ---- workers ----------------------------------------------------------------

class Pool {
  constructor(n, tsv) {
    this.workers = Array.from({ length: n }, () => {
      const w = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
      w.pending = new Map();
      w.onmessage = ({ data: { id, result, error } }) => {
        const p = w.pending.get(id);
        w.pending.delete(id);
        error ? p.reject(new Error(error)) : p.resolve(result);
      };
      w.onerror = (e) => {
        for (const p of w.pending.values()) p.reject(new Error(e.message || 'worker failed'));
        w.pending.clear();
      };
      return w;
    });
    this.nextId = 0;
    this.ready = Promise.all(this.workers.map((w) => this.call(w, 'init', { tsv })));
  }

  call(worker, type, args, transfer = []) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      worker.pending.set(id, { resolve, reject });
      worker.postMessage({ id, type, ...args }, transfer);
    });
  }

  // Same result as the CLI: cells are averaged on one worker, then every
  // worker matches an interleaved share of them (interleaved so that a flat
  // region does not leave one worker with all the distinct colors).
  async run(image, cols, de2000) {
    await this.ready;
    const pixels = image.data.slice().buffer;
    const { cells, rows } = await this.call(this.workers[0], 'cells',
      { pixels, width: image.width, height: image.height, cols }, [pixels]);

    const n = this.workers.length;
    const shares = this.workers.map((_, k) => {
      const share = new Uint32Array(Math.ceil((cells.length - k) / n));
      for (let i = k, j = 0; i < cells.length; i += n, j++) share[j] = cells[i];
      return share;
    });
    const results = await Promise.all(this.workers.map((w, k) =>
      this.call(w, 'match', { cells: shares[k], de2000 }, [shares[k].buffer])));

    const indices = new Int32Array(cells.length);
    results.forEach(({ indices: part }, k) => {
      for (let i = k, j = 0; i < cells.length; i += n, j++) indices[i] = part[j];
    });
    const distinct = new Set(cells);
    distinct.delete(0xFFFFFFFF);
    return { indices, cols, rows, distinct: distinct.size };
  }
}

// ---- tiles ------------------------------------------------------------------

function parseTiles(tsv) {
  const header = tsv.match(/tile_size=(\d+) cols=(\d+) count=(\d+)/);
  if (!header) throw new Error('tiles.tsv: missing header');
  const names = [];
  for (const line of tsv.split('\n')) {
    if (!line || line[0] === '#') continue;
    names.push(line.split('\t')[6]);
  }
  return { size: +header[1], cols: +header[2], names };
}

function loadImage(src) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error('cannot load ' + src));
    img.src = src;
  });
}

// ---- state ------------------------------------------------------------------

const state = {
  pool: null,
  tiles: null,
  atlas: null,
  image: null,   // ImageData of the input
  name: 'mosaic',
  mosaic: null,  // last result of Pool.run
  tilePx: 32,    // tile size the mosaic is drawn at
  run: 0,        // increments on every change so stale results are dropped
};

function setStatus(text, isError = false) {
  $('status').textContent = text;
  $('status').classList.toggle('error', isError);
}

async function decode(blob, name) {
  const bitmap = await createImageBitmap(blob);
  const { width, height } = bitmap;
  if (width * height > MAX_INPUT_PIXELS) {
    bitmap.close();
    throw new Error(`image too large (${width} × ${height})`);
  }
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  ctx.drawImage(bitmap, 0, 0);
  bitmap.close();
  state.image = ctx.getImageData(0, 0, width, height);
  state.name = name.replace(/\.[^.]*$/, '') || 'mosaic';

  const preview = $('original');
  preview.width = width;
  preview.height = height;
  preview.getContext('2d').putImageData(state.image, 0, 0);
  $('source-size').textContent = `${width} × ${height} px`;

  // More tiles than pixels would only repeat cells.
  const max = Math.min(MAX_COLS, width);
  $('width').max = $('width-value').max = max;
  setWidth(Math.min(+$('width-value').value, max));
  document.body.classList.add('has-image');
  update();
}

async function openFile(file) {
  if (!file) return;
  try {
    await decode(file, file.name);
  } catch (e) {
    setStatus(`Could not read ${file.name}: ${e.message}`, true);
  }
}

// ---- rendering --------------------------------------------------------------

async function update() {
  if (!state.image) return;
  const run = ++state.run;
  const cols = +$('width').value;
  const de2000 = $('metric').value === 'de2000';
  setStatus('Matching…');
  document.body.classList.add('busy');
  const t0 = performance.now();
  try {
    const mosaic = await state.pool.run(state.image, cols, de2000);
    if (run !== state.run) return;
    mosaic.ms = performance.now() - t0;
    state.mosaic = mosaic;
    draw();
  } catch (e) {
    if (run === state.run) setStatus(e.message, true);
  } finally {
    if (run === state.run) document.body.classList.remove('busy');
  }
}

function draw() {
  const { indices, cols, rows, distinct, ms } = state.mosaic;
  const { size, cols: atlasCols } = state.tiles;
  const fit = Math.floor(Math.min(Math.sqrt(MAX_CANVAS_AREA / (cols * rows)),
                                  MAX_CANVAS_SIDE / cols, MAX_CANVAS_SIDE / rows));
  const wanted = +$('tile-px').value;
  const t = state.tilePx = Math.max(1, Math.min(wanted, fit));

  const canvas = $('mosaic');
  canvas.width = cols * t;
  canvas.height = rows * t;
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingQuality = 'high';
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  const used = new Set();
  for (let y = 0; y < rows; y++) {
    for (let x = 0; x < cols; x++) {
      const k = indices[y * cols + x];
      if (k < 0) continue;
      used.add(k);
      ctx.drawImage(state.atlas, (k % atlasCols) * size, Math.floor(k / atlasCols) * size,
                    size, size, x * t, y * t, t, t);
    }
  }

  const note = t < wanted ? ` · drawn at ${t} px per tile to fit the browser's canvas limit` : '';
  setStatus(`${cols} × ${rows} tiles · ${distinct.toLocaleString()} distinct colors · ` +
            `${used.size} of ${state.tiles.names.length} items used · ` +
            `${canvas.width} × ${canvas.height} px · ${(ms / 1000).toFixed(2)} s${note}`);
  $('download').disabled = false;
}

function showTile(e) {
  const tip = $('tip');
  if (!state.mosaic) return;
  const canvas = $('mosaic');
  const r = canvas.getBoundingClientRect();
  const x = Math.floor((e.clientX - r.left) / r.width * state.mosaic.cols);
  const y = Math.floor((e.clientY - r.top) / r.height * state.mosaic.rows);
  const k = state.mosaic.indices[y * state.mosaic.cols + x];
  if (k === undefined || k < 0) {
    tip.hidden = true;
    return;
  }
  tip.textContent = state.tiles.names[k].split(' | ').filter((n, i, a) => a.indexOf(n) === i)
    .join(' · ');
  tip.hidden = false;
  tip.style.left = `${e.clientX + 14}px`;
  tip.style.top = `${e.clientY + 14}px`;
}

// ---- controls ---------------------------------------------------------------

function setWidth(v) {
  $('width').value = v;
  $('width-value').value = v;
}

let debounce;
function updateSoon() {
  clearTimeout(debounce);
  debounce = setTimeout(update, 200);
}

function bindControls() {
  $('file').addEventListener('change', (e) => openFile(e.target.files[0]));
  $('sample').addEventListener('click', async () => {
    const blob = await (await fetch('sample.png')).blob();
    decode(blob, 'mona-lisa.png').catch((e) => setStatus(e.message, true));
  });

  $('width').addEventListener('input', (e) => {
    $('width-value').value = e.target.value;
    updateSoon();
  });
  $('width-value').addEventListener('change', (e) => {
    const v = Math.max(+$('width').min, Math.min(+$('width').max, Math.round(+e.target.value || 0)));
    setWidth(v);
    update();
  });
  $('metric').addEventListener('change', update);
  $('tile-px').addEventListener('change', () => state.mosaic && draw());

  $('download').addEventListener('click', () => {
    $('mosaic').toBlob((blob) => {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `${state.name}-objetismo-${state.mosaic.cols}.png`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 10_000);
    }, 'image/png');
  });

  const mosaic = $('mosaic');
  mosaic.addEventListener('mousemove', showTile);
  mosaic.addEventListener('mouseleave', () => ($('tip').hidden = true));

  const drop = document.body;
  drop.addEventListener('dragover', (e) => {
    e.preventDefault();
    drop.classList.add('dragging');
  });
  drop.addEventListener('dragleave', (e) => {
    if (!e.relatedTarget) drop.classList.remove('dragging');
  });
  drop.addEventListener('drop', (e) => {
    e.preventDefault();
    drop.classList.remove('dragging');
    openFile(e.dataTransfer.files[0]);
  });
  document.addEventListener('paste', (e) => {
    const item = [...e.clipboardData.items].find((i) => i.type.startsWith('image/'));
    if (item) openFile(item.getAsFile());
  });
}

async function main() {
  bindControls();
  try {
    const [tsv, atlas] = await Promise.all([
      fetch('tiles.tsv').then((r) => {
        if (!r.ok) throw new Error('cannot load tiles.tsv');
        return r.text();
      }),
      loadImage('tiles.png'),
    ]);
    state.tiles = parseTiles(tsv);
    state.atlas = atlas;
    const threads = Math.max(1, Math.min(navigator.hardwareConcurrency || 4, 8));
    state.pool = new Pool(threads, tsv);
    await state.pool.ready;
    document.body.classList.add('ready');
    setStatus('Pick an image to start.');
  } catch (e) {
    setStatus(e.message, true);
  }
}

main();
