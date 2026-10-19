#!/usr/bin/env python3
"""Check early size rejection and atomic PNG replacement (POSIX)."""
import os
from pathlib import Path
import resource
import signal
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np
from PIL import Image


def main():
    binary = os.path.abspath(sys.argv[1])
    env = dict(os.environ, OMP_NUM_THREADS='1')
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        src, out = root / 'input.png', root / 'output.png'
        original = b'previous mosaic must survive'

        def run(*args, **kwargs):
            return subprocess.run([binary, '-i', str(src), '-o', str(out), *args],
                                  capture_output=True, text=True, timeout=30, env=env, **kwargs)

        # Even an input with an invalid pixel stream must report the output
        # size error first: validation needs only the PNG header.
        Image.new('RGB', (1, 1)).save(src)
        template = src.read_bytes()
        for width, height, options in [
            (31251, 1, []), (1, 31251, []),
            (1, 1, ['-w', '31251']),
            (1000, 40000, ['-w', '1000']),
            (1, 30000, ['-w', '100000']),
        ]:
            png = bytearray(template)
            png[16:24] = struct.pack('>II', width, height)
            png[29:33] = struct.pack('>I', zlib.crc32(png[12:29]))
            src.write_bytes(png)
            out.write_bytes(original)
            result = run(*options)
            assert result.returncode == 1, result
            assert 'output too large' in result.stderr, result.stderr
            assert out.read_bytes() == original
            assert not list(root.glob('output.png.tmp.*'))
            out.unlink()
            result = run(*options)
            assert result.returncode == 1 and not out.exists(), result

        # The one-million-pixel boundary is inclusive (31,250 original tiles).
        Image.new('RGB', (31250, 1)).save(src)
        result = run()
        assert result.returncode == 0, result.stderr
        with Image.open(out) as png:
            assert png.size == (1000000, 32)

        # A buffered write failure must remove its temporary file and preserve
        # an existing destination, even after PNG encoding has started.
        pixels = np.random.default_rng(5).integers(0, 256, (16, 16, 3), dtype=np.uint8)
        Image.fromarray(pixels).save(src)
        out.write_bytes(original)

        def limit_file_size():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (512, 512))

        result = run(preexec_fn=limit_file_size)
        assert result.returncode == 1, result
        assert out.read_bytes() == original
        assert not list(root.glob('output.png.tmp.*'))

        # A completed PNG replaces the old destination and leaves no debris.
        result = run()
        assert result.returncode == 0, result.stderr
        with Image.open(out) as png:
            png.load()
            assert png.size == (512, 512)
        assert not list(root.glob('output.png.tmp.*'))

        # Rename failure also cleans up, without disturbing the destination.
        out.unlink()
        out.mkdir()
        (out / 'keep').write_bytes(original)
        result = run()
        assert result.returncode == 1, result
        assert (out / 'keep').read_bytes() == original
        assert not list(root.glob('output.png.tmp.*'))
    print('ok: early output limits, overflow rejection, and atomic PNG replacement')


if __name__ == '__main__':
    main()
