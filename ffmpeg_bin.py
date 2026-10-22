from __future__ import annotations

import os
import shutil
from pathlib import Path


def _from_env(*keys: str) -> Path | None:
    for k in keys:
        v = os.environ.get(k)
        if v:
            p = Path(v)
            if p.is_file():
                return p
    return None


def ffmpeg() -> Path:
    p = _from_env("DSEE_FFMPEG", "FFMPEG")
    if p:
        return p
    w = shutil.which("ffmpeg")
    if w:
        return Path(w)
    raise RuntimeError(
        "ffmpeg not found: add it to PATH or set DSEE_FFMPEG to ffmpeg.exe"
    )


def ffprobe() -> Path:
    p = _from_env("DSEE_FFPROBE", "FFPROBE")
    if p:
        return p
    w = shutil.which("ffprobe")
    if w:
        return Path(w)

    try:
        sibling = ffmpeg().with_name("ffprobe.exe")
        if sibling.is_file():
            return sibling
        sibling = ffmpeg().with_name("ffprobe")
        if sibling.is_file():
            return sibling
    except RuntimeError:
        pass
    raise RuntimeError(
        "ffprobe not found: add it to PATH or set DSEE_FFPROBE to ffprobe.exe"
    )
