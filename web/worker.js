// Runs the WebAssembly core. app.js starts one worker per core: one of them
// averages the image into cells, then each matches its share of the cells.
import createModule from './objetismo.js';

let m;

function copyIn(bytes) {
  const ptr = m._malloc(bytes.byteLength);
  m.HEAPU8.set(bytes, ptr);
  return ptr;
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

  // Averages a width x height RGBA image down to cols cells per row.
  cells({ pixels, width, height, cols }) {
    const ptr = copyIn(new Uint8Array(pixels));
    const rows = m._objetismo_rows(width, height, cols);
    const out = m._objetismo_cells(ptr, width, height, cols, rows);
    m._free(ptr);
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
