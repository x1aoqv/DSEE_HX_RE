from __future__ import annotations

import sys
from pathlib import Path

from run_song import ensure_dll, normalize_bits, process_track


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "Usage: python cli.py <input> <output.wav> <output_rate> <bits>\n"
            "bits: 16 | 24 | 32",
            file=sys.stderr,
        )
        return 2

    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    rate = int(sys.argv[3])
    bits = normalize_bits(sys.argv[4])

    if not src.is_file():
        print(f"Input not found: {src}", file=sys.stderr)
        return 1

    ensure_dll()
    process_track(src, out, out_rate=rate, out_bits=bits)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
