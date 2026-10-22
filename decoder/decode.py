from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

from ffmpeg_bin import ffmpeg as find_ffmpeg

def decode_pcm(src: Path, rate: int) -> Path:

    out = Path(tempfile.mkstemp(prefix="dsee_", suffix=".s32le")[1])
    ff = find_ffmpeg()
    p = subprocess.run(
        [str(ff), "-v", "error", "-y", "-i", str(src), "-map", "0:a:0",
         "-ac", "2", "-ar", str(rate), "-f", "s32le", str(out)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if p.returncode != 0:
        out.unlink(missing_ok=True)
        lines = [l for l in p.stderr.splitlines() if l.strip()]
        bad = next((l for l in lines if "Error" in l or "error" in l),
                   lines[-1] if lines else f"ffmpeg rc={p.returncode}")
        raise RuntimeError(f"decode failed: {bad.strip()}")
    return out
