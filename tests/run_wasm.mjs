// Runs the WebAssembly build on raw RGBA pixels and prints the tile indices
// as TSV, like objetismo --dump-indices. Used by tests/check_wasm.py.
//
// Usage: node tests/run_wasm.mjs web/objetismo.js TILES.tsv IMAGE WIDTH HEIGHT COLS METRIC [rgba|png]
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, tsvPath, imagePath, w, h, c, metric, format = 'rgba'] = process.argv.slice(2);
const width = +w, height = +h, cols = +c;

const { default: createModule } = await import(pathToFileURL(resolve(modulePath)));
const m = await createModule();

const tsv = m.stringToNewUTF8(readFileSync(tsvPath, 'utf8'));
const err = m._objetismo_load_tiles(tsv);
m._free(tsv);
if (err) throw new Error(m.UTF8ToString(err));

const bytes = readFileSync(imagePath);
let ptr = m._malloc(bytes.length);
m.HEAPU8.set(bytes, ptr);
if (format === 'png') {
  const result = m._malloc(12);
  const error = m._objetismo_decode_png(ptr, bytes.length, result);
  m._free(ptr);
  if (error) throw new Error(m.UTF8ToString(error));
  ptr = m.HEAP32[result / 4] >>> 0;
  if (m.HEAP32[result / 4 + 1] !== width || m.HEAP32[result / 4 + 2] !== height)
    throw new Error('bad decoded PNG size');
  m._free(result);
} else if (bytes.length !== width * height * 4) throw new Error('bad RGBA size');
const rows = m._objetismo_rows(width, height, cols);
const cells = m._objetismo_cells(ptr, width, height, cols, rows);
m._free(ptr);
const out = m._objetismo_match_cells(cells, cols * rows, metric === 'de2000' ? 1 : 0);

const idx = m.HEAP32.subarray(out >> 2, (out >> 2) + cols * rows);
const lines = [];
for (let y = 0; y < rows; y++) lines.push(Array.from(idx.subarray(y * cols, (y + 1) * cols)).join('\t'));
process.stdout.write(lines.join('\n') + '\n');
