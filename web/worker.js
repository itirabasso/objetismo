// Runs the WebAssembly core. app.js starts one worker per core: the first one
// holds the image and averages it into cells, then all of them match colors.
import createModule from './objetismo.js';

const PREVIEW_WIDTH = 1024;

let m;
let image = null;  // { ptr, width, height } of the image in the module's memory
let small = null;  // the same, averaged down for previews (or the image itself)

function copyIn(bytes) {
  const ptr = m._malloc(bytes.byteLength);
  if (!ptr) throw new Error('not enough memory for the image data');
  m.HEAPU8.set(bytes, ptr);
  return ptr;
}

// Averages an image down to cols px across with the same code as the cells,
// so in linear light; transparent cells stay transparent.
function shrink({ ptr, width, height }, cols) {
  const rows = m._objetismo_rows(width, height, cols);
  const out = m._objetismo_cells(ptr, width, height, cols, rows);  // may grow (and move) HEAPU8
  const cells = new Uint32Array(m.HEAPU8.buffer, out, cols * rows);
  const rgba = new Uint8Array(cols * rows * 4);
  cells.forEach((c, i) => {
    if (c !== 0xFFFFFFFF) rgba.set([c >>> 16, (c >>> 8) & 0xFF, c & 0xFF, 0xFF], i * 4);
  });
  return { ptr: copyIn(rgba), width: cols, height: rows };
}

const handlers = {
  async init({ tsv }) {
    m = await createModule();
    const str = m.stringToNewUTF8(tsv);
    const err = m._objetismo_load_tiles(str);
    m._free(str);
    if (err) throw new Error('tiles.tsv: ' + m.UTF8ToString(err));
    return {};
  },

  // Decode PNGs without a canvas premultiply/unpremultiply round trip.
  png({ bytes }) {
    let encoded = 0, result = 0, rgba = 0;
    try {
      encoded = copyIn(new Uint8Array(bytes));
      result = m._malloc(12);
      if (!result) throw new Error('not enough memory to decode the image');
      const err = m._objetismo_decode_png(encoded, bytes.byteLength, result);
      if (err) throw new Error(m.UTF8ToString(err));
      // Decoding may grow memory, so obtain the views afterwards.
      rgba = m.HEAP32[result / 4] >>> 0;
      const width = m.HEAP32[result / 4 + 1], height = m.HEAP32[result / 4 + 2];
      const pixels = m.HEAPU8.slice(rgba, rgba + width * height * 4).buffer;
      return { pixels, width, height, transfer: [pixels] };
    } finally {
      if (rgba) m._free(rgba);
      if (result) m._free(result);
      if (encoded) m._free(encoded);
    }
  },

  // Keeps a width x height RGBA image in the module's memory for cells(), so
  // moving the slider does not copy it again, plus a copy at most
  // PREVIEW_WIDTH px wide that previews average instead, which is much faster
  // for large photos.
  image({ pixels, width, height }) {
    const previous = new Set([image, small]);
    // A failed replacement must not leave references to freed buffers.
    image = small = null;
    for (const img of previous) if (img) m._free(img.ptr);
    image = { ptr: copyIn(new Uint8Array(pixels)), width, height };
    small = width > PREVIEW_WIDTH ? shrink(image, PREVIEW_WIDTH) : image;
    return {};
  },

  // Averages the image (or its small copy, for a preview) to cols cells per row.
  cells({ cols, preview }) {
    const { ptr, width, height } = preview ? small : image;
    const rows = m._objetismo_rows(width, height, cols);
    const out = m._objetismo_cells(ptr, width, height, cols, rows);
    const cells = new Uint32Array(m.HEAPU8.buffer, out, cols * rows).slice();
    return { cells, rows, transfer: [cells.buffer] };
  },

  // Picks the closest tile for each packed cell color.
  match({ cells, de2000 }) {
    const ptr = copyIn(new Uint8Array(cells.buffer, cells.byteOffset, cells.byteLength));
    const out = m._objetismo_match_cells(ptr, cells.length, de2000 ? 1 : 0);
    m._free(ptr);
    const indices = new Int32Array(m.HEAPU8.buffer, out, cells.length).slice();
    return { indices, transfer: [indices.buffer] };
  },
};

self.onmessage = async ({ data: { id, type, ...args } }) => {
  try {
    const { transfer = [], ...result } = await handlers[type](args);
    self.postMessage({ id, result }, transfer);
  } catch (e) {
    self.postMessage({ id, error: String(e.message || e) });
  }
};
