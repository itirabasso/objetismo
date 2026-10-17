// Runs the WebAssembly build on raw RGBA pixels and prints the tile indices
// as TSV, like objetismo --dump-indices. Used by tests/check_wasm.py.
//
// Usage: node tests/run_wasm.mjs web/objetismo.js TILES.tsv IMAGE.rgba WIDTH HEIGHT COLS METRIC
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, tsvPath, rgbaPath, w, h, c, metric] = process.argv.slice(2);
const width = +w, height = +h, cols = +c;

const { default: createModule } = await import(pathToFileURL(resolve(modulePath)));
const m = await createModule();

const tsv = m.stringToNewUTF8(readFileSync(tsvPath, 'utf8'));
const err = m._objetismo_load_tiles(tsv);
m._free(tsv);
if (err) throw new Error(m.UTF8ToString(err));

const rgba = readFileSync(rgbaPath);
if (rgba.length !== width * height * 4) throw new Error('bad RGBA size');
const ptr = m._malloc(rgba.length);
m.HEAPU8.set(rgba, ptr);
const rows = m._objetismo_rows(width, height, cols);
const cells = m._objetismo_cells(ptr, width, height, cols, rows);
m._free(ptr);
const out = m._objetismo_match_cells(cells, cols * rows, metric === 'de2000' ? 1 : 0);

const idx = m.HEAP32.subarray(out >> 2, (out >> 2) + cols * rows);
const lines = [];
for (let y = 0; y < rows; y++) lines.push(Array.from(idx.subarray(y * cols, (y + 1) * cols)).join('\t'));
process.stdout.write(lines.join('\n') + '\n');
