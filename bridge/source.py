from __future__ import annotations

import ctypes
from pathlib import Path

from .probe import probe_media_facts

FINAL = Path(__file__).resolve().parent.parent
DLL_PATH = FINAL / "build" / "dsee_kern.dll"

class Source(ctypes.Structure):

    _fields_ = [
        ("mode", ctypes.c_int), ("channels", ctypes.c_int),
        ("codec", ctypes.c_int), ("input_fs", ctypes.c_int),
        ("output_fs", ctypes.c_int), ("input_bits", ctypes.c_int),
        ("output_bits", ctypes.c_int), ("vbr", ctypes.c_int),
        ("abr", ctypes.c_int), ("sb", ctypes.c_int), ("head", ctypes.c_int),
        ("dcr", ctypes.c_int), ("gain", ctypes.c_int), ("offset", ctypes.c_int),
    ]

class MediaFacts(ctypes.Structure):
    _fields_ = [
        ("rate", ctypes.c_int),
        ("channels", ctypes.c_int),
        ("bits", ctypes.c_int),
        ("bit_rate", ctypes.c_int),
        ("codec_name", ctypes.c_char * 64),
        ("path_has_cbr", ctypes.c_int),
    ]

_lib = None

def _load_lib():
    global _lib
    if _lib is not None:
        return _lib
    if not DLL_PATH.is_file():
        raise RuntimeError(f"DLL not found: {DLL_PATH} (run build.bat first)")
    lib = ctypes.CDLL(str(DLL_PATH))
    lib.dsee_fill_source.argtypes = [
        ctypes.POINTER(MediaFacts), ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.POINTER(Source),
    ]
    lib.dsee_fill_source.restype = ctypes.c_int
    _lib = lib
    return lib

def _bits_to_kernel(bits: str | int) -> int:
    s = str(bits).strip().lower()
    if s not in ("16", "24", "32"):
        raise ValueError(f"unsupported bit depth: {bits} (use 16 / 24 / 32)")
    return int(s)

def set_configuration(
    src: Path,
    output_fs: int,
    output_bits: str | int = 32,
    *,
    enable: int = 1,
    media: dict | None = None,
) -> tuple[Source, dict]:
    src = Path(src)
    facts_d = media if media and "bit_rate" in media else probe_media_facts(src)
    if "bit_rate" not in facts_d:
        facts_d = probe_media_facts(src)

    mf = MediaFacts()
    mf.rate = int(facts_d["rate"])
    mf.channels = int(facts_d["channels"])
    mf.bits = int(facts_d["bits"])
    mf.bit_rate = int(facts_d.get("bit_rate") or 0)
    mf.codec_name = (facts_d.get("codec_name") or "").encode("ascii", "ignore")[:63]
    mf.path_has_cbr = int(facts_d.get("path_has_cbr") or 0)

    out = Source()
    rc = _load_lib().dsee_fill_source(
        ctypes.byref(mf), int(output_fs), _bits_to_kernel(output_bits),
        int(enable), ctypes.byref(out),
    )
    if rc:
        raise RuntimeError(f"dsee_fill_source rc={rc}")

    media_out = {
        "rate": out.input_fs,
        "channels": out.channels,
        "bits": out.input_bits,
        "codec": out.codec,
        "vbr": out.vbr,
        "abr": out.abr,
        "codec_name": (facts_d.get("codec_name") or ""),
        "bit_rate": mf.bit_rate,
        "path_has_cbr": mf.path_has_cbr,
    }
    return out, media_out
