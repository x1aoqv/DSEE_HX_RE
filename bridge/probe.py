from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from ffmpeg_bin import ffprobe as find_ffprobe

def probe_media_facts(src: Path) -> dict:

    p = subprocess.run(
        [str(find_ffprobe()), "-v", "error", "-show_streams", "-show_format",
         "-of", "json", str(src)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if p.returncode != 0:
        raise RuntimeError(f"ffprobe failed: {p.stderr.strip()}")
    j = json.loads(p.stdout)
    st = next((s for s in j.get("streams", []) if s.get("codec_type") == "audio"), None)
    if st is None:
        raise RuntimeError("no audio stream in file")
    fmt = j.get("format", {})
    rate = int(st.get("sample_rate") or 0)
    ch = int(st.get("channels") or 0)
    bits = int(st.get("bits_per_raw_sample") or st.get("bits_per_sample") or 0)
    if bits == 0:
        fmt_name = (st.get("sample_fmt") or "").lower()
        if "s16" in fmt_name:
            bits = 16
        elif "s32" in fmt_name or "flt" in fmt_name:
            bits = 32
        elif "u8" in fmt_name:
            bits = 8
        else:
            bits = 16
    name = (st.get("codec_name") or "").lower()
    br = int(st.get("bit_rate") or fmt.get("bit_rate") or 0)
    return {
        "rate": rate,
        "channels": ch,
        "bits": bits,
        "bit_rate": br,
        "codec_name": name,
        "path_has_cbr": 1 if "cbr" in src.name.lower() else 0,
    }
