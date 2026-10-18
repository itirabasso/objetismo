// objetismo in the browser: decodes the image, runs the WebAssembly core on a
// pool of workers and draws the mosaic from tiles.png. Nothing is uploaded.

import { Matcher } from './matcher.js';
import { encodePng } from './png.js';

const $ = (id) => document.getElementById(id);

// Safari caps a canvas at 16.7M pixels; the others allow more, but stay safe.
const MAX_CANVAS_AREA = 16_777_216;
const MAX_CANVAS_SIDE = 16_384;
const MAX_INPUT_PIXELS = 50_000_000;
const MAX_COLS = 400;
const MAX_TILE_ON_SCREEN = 256;  // largest a tile gets when zoomed in, in CSS pixels
const ZOOM_STEP = 1.5;
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
  matcher: null,
  tiles: null,
  atlas: null,
  image: null,   // ImageData of the input; null while loading
  name: 'mosaic',
  mosaic: null,  // last displayed result, with its run number
  tilePx: 32,    // tile size the mosaic is drawn at
  run: 0,        // invalidates old image loads and matching results
  exporting: false,
};

function setStatus(text, isError = false) {
  $('status').textContent = text;
  $('status').classList.toggle('error', isError);
}

async function openImage(blob, name = blob?.name) {
  if (!blob) return;
  const run = ++state.run;
  state.image = null;
  pending.next = null;
  refreshControls();
  try {
    await ready;
    if (run !== state.run) return;
    setStatus('Loading image…');
    const bitmap = await createImageBitmap(blob);
    if (run !== state.run) {
      bitmap.close();
      return;
    }
    const { width, height } = bitmap;
    if (width * height > MAX_INPUT_PIXELS) {
      bitmap.close();
      throw new Error(`image too large (${width} × ${height})`);
    }
    const canvas = new OffscreenCanvas(width, height);
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(bitmap, 0, 0);
    bitmap.close();
    const image = ctx.getImageData(0, 0, width, height);
    await state.matcher.setImage(image);
    if (run !== state.run) return;
    state.image = image;
    state.name = name.replace(/\.[^.]*$/, '') || 'mosaic';

    const preview = $('original');
    preview.width = width;
    preview.height = height;
    preview.getContext('2d').putImageData(state.image, 0, 0);
    $('source-size').textContent = `${width} × ${height} px`;

    // More tiles than pixels would only repeat cells.
    const max = Math.min(MAX_COLS, width);
    $('width').min = $('width-value').min = Math.min(4, max);
    $('width').max = $('width-value').max = max;
    setWidth(+$('width-value').value);
    document.body.classList.add('has-image');
    schedule('exact');
  } catch (e) {
    if (run === state.run) setStatus(`Could not read ${name}: ${e.message}`, true);
  }
}

// ---- matching and scheduling ------------------------------------------------

// Builds and draws the mosaic for the current settings. A preview matches
// rounded colors, for live feedback while the slider moves.
async function update(preview) {
  if (!state.image) return;
  const run = ++state.run;
  const isCurrent = () => run === state.run;
  const cols = +$('width').value;
  const de2000 = $('metric').value === 'de2000';
  if (!preview) setStatus('Matching…');
  const t0 = performance.now();
  try {
    const mosaic = await state.matcher.match(cols, de2000, preview, isCurrent);
    if (!mosaic) return;
    state.mosaic = { ...mosaic, preview, run, ms: performance.now() - t0 };
    draw();
  } catch (e) {
    if (isCurrent()) setStatus(e.message, true);
  }
}

// Runs update() for the latest request, one run at a time: a preview that is
// under way finishes (so dragging always shows progress), while an exact run,
// which can take seconds, is dropped as soon as anything changes.
const pending = { running: null, next: null };  // 'preview' or 'exact'

function schedule(kind) {
  if (!state.image) return;
  pending.next = kind;
  if (pending.running === 'exact') state.run++;
  refreshControls();
  if (!pending.running) drain();
}

async function drain() {
  while (pending.next) {
    pending.running = pending.next;
    pending.next = null;
    refreshControls();
    await update(pending.running === 'preview');
  }
  pending.running = null;
  refreshControls();
}

// Download requires a current exact result and no pending work or export.
function refreshControls() {
  const { mosaic, exporting } = state;
  const button = $('download');
  button.disabled = exporting || !state.image || !!pending.running || !!pending.next ||
    !mosaic || mosaic.preview || mosaic.run !== state.run;
  if (!exporting) {
    const t = +$('tile-px').value;
    button.textContent = mosaic
      ? `Download PNG (${mosaic.cols * t} × ${mosaic.rows * t} px)` : 'Download PNG';
  }
  document.body.classList.toggle('busy', pending.running === 'exact');
}

// ---- rendering --------------------------------------------------------------

// The atlas with every tile scaled to t px, cached per size. Each tile is
// scaled on its own so its neighbors in the atlas do not bleed into it.
const sheets = new Map();
function tileSheet(t) {
  if (!sheets.has(t)) {
    const { size, cols, names } = state.tiles;
    const sheet = new OffscreenCanvas(cols * t, Math.ceil(names.length / cols) * t);
    const ctx = sheet.getContext('2d', { willReadFrequently: true });
    ctx.imageSmoothingQuality = 'high';
    for (let k = 0; k < names.length; k++) {
      const x = k % cols, y = Math.floor(k / cols);
      ctx.drawImage(state.atlas, x * size, y * size, size, size, x * t, y * t, t, t);
    }
    sheets.set(t, sheet);
  }
  return sheets.get(t);
}

// The pixels of every tile at t px, one RGBA Uint32Array per tile, cached per size.
const tilePixelCache = new Map();
function tilePixels(t) {
  if (!tilePixelCache.has(t)) {
    const sheet = tileSheet(t), cols = state.tiles.cols;
    const px = new Uint32Array(
      sheet.getContext('2d').getImageData(0, 0, sheet.width, sheet.height).data.buffer);
    tilePixelCache.set(t, state.tiles.names.map((_, k) => {
      const tile = new Uint32Array(t * t);
      const x0 = (k % cols) * t, y0 = Math.floor(k / cols) * t;
      for (let y = 0; y < t; y++) {
        const row = (y0 + y) * sheet.width + x0;
        tile.set(px.subarray(row, row + t), y * t);
      }
      return tile;
    }));
  }
  return tilePixelCache.get(t);
}

function draw() {
  const { indices, cols, rows, distinct, ms, preview } = state.mosaic;
  const fit = Math.min(Math.sqrt(MAX_CANVAS_AREA / (cols * rows)),
                       MAX_CANVAS_SIDE / cols, MAX_CANVAS_SIDE / rows);
  // The preview halves the tile size until it fits the canvas limit; halving
  // keeps the sprites sharp. The download is always at the chosen size.
  const wanted = +$('tile-px').value;
  let t = wanted;
  while (t > fit && t > 1) t /= 2;
  // A preview only needs the detail the stage shows when the whole mosaic
  // fits in it, so it is drawn with the smallest tile that still covers that.
  const stage = $('stage');
  const shown = Math.min(stage.clientWidth / cols, stage.clientHeight / rows) * devicePixelRatio;
  if (preview) while (t / 2 >= shown && t > 1) t /= 2;
  state.tilePx = t;

  const canvas = $('mosaic');
  const resized = canvas.width !== cols * t || canvas.height !== rows * t;
  canvas.width = cols * t;
  canvas.height = rows * t;
  if (resized) view.zoom = null;
  applyZoom();

  // Copies the tiles' pixels into one image and puts it on the canvas at
  // once, which is much faster than a drawImage call per cell.
  const image = new ImageData(canvas.width, canvas.height);
  const out = new Uint32Array(image.data.buffer);
  out.fill(0xFF000000);  // opaque black (RGBA bytes, little-endian)
  const tiles = tilePixels(t), w = canvas.width;
  const used = new Set();
  for (let y = 0; y < rows; y++) {
    for (let x = 0; x < cols; x++) {
      const k = indices[y * cols + x];
      if (k < 0) continue;
      used.add(k);
      const tile = tiles[k];
      for (let r = 0, i = 0, o = y * t * w + x * t; r < t; r++, o += w - t)
        for (let c = 0; c < t; c++) out[o++] = tile[i++];
    }
  }
  canvas.getContext('2d').putImageData(image, 0, 0);

  const size = `${cols * wanted} × ${rows * wanted} px`;
  const note = t < wanted
    ? ` · previewed at ${t} px per tile, the download is full size` : '';
  setStatus(`${cols} × ${rows} tiles · ${distinct.toLocaleString()} distinct colors · ` +
            `${used.size} of ${state.tiles.names.length} items used · ` +
            `${size} · ${preview ? 'preview' : (ms / 1000).toFixed(2) + ' s'}${note}`);
  refreshControls();
}

// ---- PNG export -------------------------------------------------------------

async function download() {
  const button = $('download');
  if (button.disabled) return;
  const { mosaic, name } = state;
  const t = +$('tile-px').value;
  state.exporting = true;
  refreshControls();
  try {
    const blob = await encodePng(mosaic, t, tilePixels(t), (f) => {
      button.textContent = `Saving PNG… ${Math.round(f * 100)}%`;
    });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${name}-objetismo-${mosaic.cols}.png`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 60_000);
  } catch (e) {
    setStatus(`Could not save the PNG: ${e.message}`, true);
  } finally {
    state.exporting = false;
    refreshControls();
  }
}

function showTile(e) {
  const tip = $('tip');
  if (!state.mosaic || view.pan) return;
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

// ---- zoom -------------------------------------------------------------------

// view.zoom is CSS pixels per mosaic pixel, or null to fit the whole mosaic.
const view = { zoom: null, pan: null };

function zoomLimits() {
  const stage = $('stage'), canvas = $('mosaic');
  const fit = canvas.width
    ? Math.min(1, stage.clientWidth / canvas.width, stage.clientHeight / canvas.height) : 1;
  return [fit, Math.max(fit, MAX_TILE_ON_SCREEN / state.tilePx)];
}

const currentZoom = () => view.zoom ?? zoomLimits()[0];

function applyZoom() {
  const stage = $('stage'), canvas = $('mosaic');
  const [min, max] = zoomLimits();
  const z = view.zoom ?? min;
  canvas.style.width = `${canvas.width * z}px`;
  canvas.style.height = `${canvas.height * z}px`;
  canvas.classList.toggle('magnified', z > 1);
  stage.classList.toggle('zoomed', stage.scrollWidth > stage.clientWidth ||
                                   stage.scrollHeight > stage.clientHeight);
  // Shown relative to the sprites' own size, whatever tile size they are drawn at.
  const size = state.tiles ? state.tiles.size : 32;
  $('zoom-level').textContent = `${Math.round(z * state.tilePx / size * 100)}%`;
  $('zoom-out').disabled = z <= min * 1.001;
  $('zoom-in').disabled = z >= max * 0.999;
}

// Zooms to z, keeping the point of the mosaic under (cx, cy) in place
// (the middle of the stage by default).
function setZoom(z, cx, cy) {
  const stage = $('stage'), canvas = $('mosaic');
  const [min, max] = zoomLimits();
  z = Math.max(min, Math.min(max, z));
  const box = stage.getBoundingClientRect();
  if (cx === undefined) [cx, cy] = [box.left + box.width / 2, box.top + box.height / 2];
  const before = canvas.getBoundingClientRect();
  const px = (cx - before.left) / before.width * canvas.width;
  const py = (cy - before.top) / before.height * canvas.height;
  view.zoom = z <= min ? null : z;
  applyZoom();
  const after = canvas.getBoundingClientRect();
  stage.scrollLeft += after.left + px * z - cx;
  stage.scrollTop += after.top + py * z - cy;
}

function bindZoom() {
  const stage = $('stage');
  $('zoom-in').addEventListener('click', () => setZoom(currentZoom() * ZOOM_STEP));
  $('zoom-out').addEventListener('click', () => setZoom(currentZoom() / ZOOM_STEP));
  $('zoom-fit').addEventListener('click', () => setZoom(0));
  $('zoom-level').addEventListener('click', () => setZoom(state.tiles.size / state.tilePx));

  stage.addEventListener('wheel', (e) => {
    if (!state.mosaic) return;
    e.preventDefault();
    const speed = e.deltaMode === WheelEvent.DOM_DELTA_PIXEL ? 0.002 : 0.05;
    setZoom(currentZoom() * Math.exp(-e.deltaY * speed), e.clientX, e.clientY);
  }, { passive: false });

  // Drag to pan with the mouse; touch keeps its native scrolling.
  stage.addEventListener('pointerdown', (e) => {
    if (e.pointerType !== 'mouse' || e.button !== 0 || !stage.classList.contains('zoomed')) return;
    e.preventDefault();
    view.pan = { x: e.clientX, y: e.clientY, left: stage.scrollLeft, top: stage.scrollTop };
    stage.setPointerCapture(e.pointerId);
    stage.classList.add('panning');
    $('tip').hidden = true;
  });
  stage.addEventListener('pointermove', (e) => {
    if (!view.pan) return;
    stage.scrollLeft = view.pan.left - (e.clientX - view.pan.x);
    stage.scrollTop = view.pan.top - (e.clientY - view.pan.y);
  });
  const endPan = () => {
    view.pan = null;
    stage.classList.remove('panning');
  };
  stage.addEventListener('pointerup', endPan);
  stage.addEventListener('pointercancel', endPan);

  document.addEventListener('keydown', (e) => {
    if (!state.mosaic || e.ctrlKey || e.metaKey || e.altKey ||
        e.target.closest('input, select, textarea')) return;
    const keys = {
      '+': () => setZoom(currentZoom() * ZOOM_STEP),
      '=': () => setZoom(currentZoom() * ZOOM_STEP),
      '-': () => setZoom(currentZoom() / ZOOM_STEP),
      '0': () => setZoom(0),
      '1': () => setZoom(state.tiles.size / state.tilePx),
    };
    if (keys[e.key]) {
      e.preventDefault();
      keys[e.key]();
    }
  });

  new ResizeObserver(() => state.mosaic && applyZoom()).observe(stage);
}

// ---- controls ---------------------------------------------------------------

function setWidth(v) {
  v = Math.max(+$('width').min, Math.min(+$('width').max, Math.round(v || 0)));
  $('width').value = v;
  $('width-value').value = v;
}

function bindControls() {
  $('file').addEventListener('change', (e) => openImage(e.target.files[0]));
  $('sample').addEventListener('click', async () => {
    try {
      const blob = await (await fetch('sample.png')).blob();
      await openImage(blob, 'mona-lisa.png');
    } catch (e) {
      setStatus(e.message, true);
    }
  });

  // Live preview while the slider moves, exact match once it is released.
  $('width').addEventListener('input', (e) => {
    $('width-value').value = e.target.value;
    schedule('preview');
  });
  $('width').addEventListener('change', () => schedule('exact'));
  $('width-value').addEventListener('change', (e) => {
    setWidth(+e.target.value);
    schedule('exact');
  });
  $('metric').addEventListener('change', () => schedule('exact'));
  $('tile-px').addEventListener('change', () => state.mosaic && draw());

  $('download').addEventListener('click', download);

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
    openImage(e.dataTransfer.files[0]);
  });
  document.addEventListener('paste', (e) => {
    const item = [...e.clipboardData.items].find((i) => i.type.startsWith('image/'));
    if (item) openImage(item.getAsFile());
  });
}

async function main() {
  bindControls();
  bindZoom();
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
  state.matcher = new Matcher(threads, tsv);
  await state.matcher.ready;
  document.body.classList.add('ready');
  setStatus('Pick an image to start.');
}

const ready = main();
ready.catch((e) => setStatus(e.message, true));
