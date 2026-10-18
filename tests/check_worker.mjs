// Exercise the real worker message handlers with controlled allocation failures.
// Run with: node tests/check_worker.mjs
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const live = new Set();
let nextPtr = 8;
let failCopy = false;
let failShrink = false;
const memory = new Uint8Array(1_000_000);
const module = {
  HEAPU8: memory,
  _malloc(bytes) {
    if (failCopy) throw new Error('copy failed');
    const ptr = nextPtr;
    nextPtr += Math.ceil(bytes / 8) * 8;
    live.add(ptr);
    return ptr;
  },
  _free(ptr) {
    assert.ok(live.delete(ptr), `buffer ${ptr} was already freed`);
  },
  stringToNewUTF8() { return this._malloc(8); },
  _objetismo_load_tiles() { return 0; },
  _objetismo_rows(width, height, cols) {
    return Math.max(1, Math.round(height * cols / width));
  },
  _objetismo_cells() {
    if (failShrink) throw new Error('preview failed');
    return 800_000; // Separate scratch space for the fake cell colors.
  },
};

let response;
const self = { postMessage(message) { response = message; } };
const source = readFileSync(new URL('../web/worker.js', import.meta.url), 'utf8');
const importLine = "import createModule from './objetismo.js';";
assert.ok(source.includes(importLine));
vm.runInNewContext(source.replace(importLine, ''), {
  self, createModule: async () => module, Uint8Array, Uint32Array,
});

let id = 0;
async function send(type, args = {}) {
  await self.onmessage({ data: { id: ++id, type, ...args } });
  assert.equal(response.id, id);
  return response;
}
async function upload(width = 1025) {
  return send('image', { pixels: new ArrayBuffer(width * 4), width, height: 1 });
}

assert.equal((await send('init', { tsv: '' })).error, undefined);
assert.equal((await upload()).error, undefined);
assert.equal(live.size, 2);

// Failure after the old full-size and preview buffers have been released.
failShrink = true;
assert.equal((await upload()).error, 'preview failed');
failShrink = false;
assert.equal((await upload()).error, undefined);
assert.equal(live.size, 2);

// Failure before a replacement image exists must also allow another upload.
failCopy = true;
assert.equal((await upload()).error, 'copy failed');
assert.equal(live.size, 0);
failCopy = false;
assert.equal((await upload()).error, undefined);

// Small images share one buffer with their preview; free it only once.
assert.equal((await upload(10)).error, undefined);
assert.equal(live.size, 1);
assert.equal((await upload()).error, undefined);
assert.equal(live.size, 2);
console.log('ok: worker retries after failures and frees shared buffers once');
