// Streaming RGB PNG encoding from a mosaic and RGBA tile pixels.

const PNG_SIGNATURE = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
const CRC_TABLE = new Uint32Array(256).map((_, n) => {
  for (let k = 0; k < 8; k++) n = n & 1 ? 0xEDB88320 ^ (n >>> 1) : n >>> 1;
  return n;
});

function pngChunk(type, data) {
  const out = new Uint8Array(12 + data.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  let crc = 0xFFFFFFFF;
  for (let i = 4; i < 8 + data.length; i++) crc = CRC_TABLE[(crc ^ out[i]) & 0xFF] ^ (crc >>> 8);
  view.setUint32(8 + data.length, (crc ^ 0xFFFFFFFF) >>> 0);
  return out;
}

// Encodes the mosaic as an RGB PNG with t px tiles, one row of tiles at a
// time like the CLI's write_mosaic, so its size is not bound by the canvas
// limit and the full image never sits in memory uncompressed.
export async function encodePng({ indices, cols, rows }, t, tiles, progress) {
  const rgbTiles = new Map();
  const tileRgb = (k) => {
    if (!rgbTiles.has(k)) {
      const tile = tiles[k];
      const rgba = new Uint8Array(tile.buffer, tile.byteOffset, tile.byteLength);
      const rgb = new Uint8Array(t * t * 3);
      for (let i = 0, o = 0; i < rgba.length; i += 4) {
        rgb[o++] = rgba[i];
        rgb[o++] = rgba[i + 1];
        rgb[o++] = rgba[i + 2];
      }
      rgbTiles.set(k, rgb);
    }
    return rgbTiles.get(k);
  };

  const width = cols * t, height = rows * t, stride = 1 + width * 3;
  const deflate = new CompressionStream('deflate');  // zlib format, as IDAT needs
  const idat = [];
  const reading = (async () => {
    const reader = deflate.readable.getReader();
    for (let r; !(r = await reader.read()).done;) idat.push(pngChunk('IDAT', r.value));
  })();
  const writer = deflate.writable.getWriter();
  for (let cy = 0; cy < rows; cy++) {
    // t scanlines, each a filter byte (0, none) then RGB; empty cells stay black.
    const band = new Uint8Array(stride * t);
    for (let cx = 0; cx < cols; cx++) {
      const k = indices[cy * cols + cx];
      if (k < 0) continue;
      const rgb = tileRgb(k);
      for (let y = 0; y < t; y++)
        band.set(rgb.subarray(y * t * 3, (y + 1) * t * 3), y * stride + 1 + cx * t * 3);
    }
    await writer.write(band);
    progress?.((cy + 1) / rows);
  }
  await writer.close();
  await reading;

  const ihdr = new Uint8Array(13);
  const view = new DataView(ihdr.buffer);
  view.setUint32(0, width);
  view.setUint32(4, height);
  ihdr.set([8, 2, 0, 0, 0], 8);  // 8-bit RGB, deflate, adaptive filters, no interlace
  const iend = pngChunk('IEND', new Uint8Array());
  return new Blob([PNG_SIGNATURE, pngChunk('IHDR', ihdr), ...idat, iend], { type: 'image/png' });
}
