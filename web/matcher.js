// Worker pool and cached color matching. No UI state lives here.

const MATCH_CHUNK = 1024;
const MAX_CACHED_COLORS = 2_000_000;
const EMPTY = 0xFFFFFFFF;

export class Matcher {
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
    this.caches = new Map();
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

  // Decode PNGs with the same library as the CLI, preserving partial alpha.
  async decodePng(bytes) {
    await this.ready;
    return this.call(this.workers[0], 'png', { bytes }, [bytes]);
  }

  // The image stays on the first worker, which averages it into cells.
  async setImage(image) {
    await this.ready;
    const pixels = image.data.slice().buffer;
    return this.call(this.workers[0], 'image',
      { pixels, width: image.width, height: image.height }, [pixels]);
  }

  // Matches packed 0xRRGGBB colors to tiles in chunks, each taken by whichever
  // worker is free, and hands every chunk's result to onChunk(start, indices).
  // Stops handing out chunks once isCurrent() turns false, so an abandoned run
  // frees the workers within one chunk.
  async matchColors(colors, de2000, isCurrent, onChunk) {
    await this.ready;
    const size = Math.min(MATCH_CHUNK, Math.ceil(colors.length / this.workers.length));
    let next = 0;
    await Promise.all(this.workers.map(async (w) => {
      while (next < colors.length && isCurrent()) {
        const start = next;
        next = Math.min(colors.length, next + size);
        const chunk = colors.slice(start, next);
        const { indices } = await this.call(w, 'match', { cells: chunk, de2000 }, [chunk.buffer]);
        onChunk(start, indices);
      }
    }));
  }

  // Exact colors for final output; 6-bit bucket centers for live previews.
  // Cache each metric separately and keep completed chunks after cancellation.
  async match(cols, de2000, preview, isCurrent) {
    await this.ready;
    const { cells, rows } = await this.call(this.workers[0], 'cells', { cols, preview });
    if (!isCurrent()) return null;

    const name = `${de2000}-${preview}`;
    if (!this.caches.has(name) || this.caches.get(name).size > MAX_CACHED_COLORS)
      this.caches.set(name, new Map());
    const cache = this.caches.get(name);
    const key = preview ? (c) => (c >>> 2) & 0x3F3F3F : (c) => c;

    const missing = new Set();
    for (const c of cells) if (c !== EMPTY && !cache.has(key(c))) missing.add(key(c));
    const keys = Uint32Array.from(missing);
    const colors = preview ? keys.map((k) => (k << 2) | 0x020202) : keys.slice();  // bucket centers
    await this.matchColors(colors, de2000, isCurrent, (start, indices) => {
      for (let i = 0; i < indices.length; i++) cache.set(keys[start + i], indices[i]);
    });
    if (!isCurrent()) return null;

    const indices = new Int32Array(cells.length);
    for (let i = 0; i < cells.length; i++) indices[i] = cells[i] === EMPTY ? -1 : cache.get(key(cells[i]));
    const distinct = new Set(cells);
    distinct.delete(EMPTY);
    return { indices, cols, rows, distinct: distinct.size };
  }
}
