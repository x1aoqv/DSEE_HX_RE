from __future__ import annotations

import ctypes
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

from bridge import Source, set_configuration

FINAL = Path(__file__).resolve().parent
OMG = FINAL / "kern"
CFG_C = FINAL / "config"
BUILD = FINAL / "build"
DLL = BUILD / "dsee_kern.dll"
BUILD_BAT = FINAL / "build.bat"

RING = 1589

HIST = 42

class Cfg(ctypes.Structure):

    _fields_ = [
        ("f76", ctypes.c_int), ("f80", ctypes.c_int), ("upmode", ctypes.c_int),
        ("src", ctypes.c_int), ("dst", ctypes.c_int), ("step_src", ctypes.c_int),
        ("step_dst", ctypes.c_int), ("n12", ctypes.c_int), ("f18992", ctypes.c_int),
        ("nblk", ctypes.c_int), ("off_up", ctypes.c_int), ("off_low", ctypes.c_int),
        ("channels", ctypes.c_int), ("nch_gate", ctypes.c_int), ("st1", ctypes.c_int),
        ("active", ctypes.c_int), ("rs_in", ctypes.c_int), ("rs_out", ctypes.c_int),
        ("rate", ctypes.c_int),
        ("gain_a", ctypes.c_float), ("gain_b", ctypes.c_float),
        ("gain_c", ctypes.c_float), ("gain_e", ctypes.c_float),
    ]

def normalize_bits(bits) -> str:

    s = str(bits).strip().lower()
    if s in ("16", "24", "32"):
        return s
    raise ValueError(f"unsupported bit depth: {bits} (use 16 / 24 / 32)")

def ensure_dll() -> None:

    sources: list[Path] = [BUILD_BAT]
    sources.extend(OMG.rglob("*.c"))
    sources.extend(OMG.rglob("*.h"))
    sources.extend(CFG_C.rglob("*.c"))
    sources.extend(CFG_C.rglob("*.h"))
    if not any(OMG.rglob("*.c")):
        raise RuntimeError("kern sources not found")
    if not any(CFG_C.rglob("*.c")):
        raise RuntimeError("config C sources not found")
    if DLL.exists() and DLL.stat().st_mtime >= max(p.stat().st_mtime for p in sources):
        return
    print("building build/dsee_kern.dll ...", flush=True)
    subprocess.run(["cmd.exe", "/c", str(BUILD_BAT)], check=True)
    if not DLL.exists():
        raise RuntimeError("build.bat did not produce build/dsee_kern.dll")
    try:
        import bridge.source as _bs
        _bs._lib = None
    except Exception:
        pass

def write_wav(path: Path, rate: int, data: np.ndarray, bits: str) -> None:

    path.parent.mkdir(parents=True, exist_ok=True)
    bits = normalize_bits(bits)
    nch = 2
    x = np.clip(np.ascontiguousarray(data, dtype=np.float64), -1.0, 1.0)

    if bits == "16":
        pcm = np.clip(np.rint(x * 32767.0), -32768, 32767).astype("<i2")
        raw = pcm.tobytes()
        bps = 16
        block = nch * 2
    elif bits == "24":
        pcm = np.clip(np.rint(x * 8388607.0), -8388608, 8388607).astype(np.int32)
        u = pcm.view(np.uint32)
        b0 = (u & 0xFF).astype(np.uint8)
        b1 = ((u >> 8) & 0xFF).astype(np.uint8)
        b2 = ((u >> 16) & 0xFF).astype(np.uint8)
        packed = np.column_stack([b0, b1, b2]).reshape(-1)
        raw = packed.tobytes()
        bps = 24
        block = nch * 3
    else:
        pcm = np.clip(np.rint(x * 2147483647.0), -2147483648, 2147483647).astype("<i4")
        raw = pcm.tobytes()
        bps = 32
        block = nch * 4

    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF", 36 + len(raw), b"WAVE", b"fmt ", 16, 1, nch,
        rate, rate * block, block, bps, b"data", len(raw),
    )
    path.write_bytes(header + raw)

def _fmt_sec(s: float) -> str:
    if s < 10:
        return f"{s:.3f}s"
    return f"{s:.2f}s"

class Kern:

    def __init__(self, source: Source, media: dict, out_bits: str) -> None:
        lib = self.lib = ctypes.CDLL(str(DLL))
        lib.dsee_mem_size.restype = ctypes.c_int
        lib.dsee_open_source.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(Source),
            ctypes.POINTER(Cfg),
        ]
        lib.dsee_open_source.restype = ctypes.c_int
        lib.dsee_heq_delays.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        lib.dsee_heq_delays.restype = ctypes.c_int
        lib.dsee_heq_gain.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
        lib.dsee_heq_gain.restype = ctypes.c_int
        fp = ctypes.POINTER(ctypes.c_float)
        ip = ctypes.POINTER(ctypes.c_int)
        i32 = ctypes.POINTER(ctypes.c_int32)
        lib.dsee_heq.argtypes = [
            fp, fp, fp, fp, ip, ip, ip, ctypes.c_double, i32, i32, ctypes.c_int,
        ]
        lib.dsee_heq.restype = None
        lib.dsee_f76_frame.argtypes = [
            ctypes.c_void_p, fp, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ]
        lib.dsee_f76_frame.restype = None
        lib.dsee_dsp.argtypes = [
            ctypes.c_void_p, fp, ctypes.c_int, ctypes.c_int,
        ]
        lib.dsee_dsp.restype = None
        lib.dsee_process_i32.argtypes = [
            ctypes.c_void_p, fp, fp, fp, fp, ip, ip, ip, ctypes.c_double,
            i32, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, fp,
        ]
        lib.dsee_process_i32.restype = ctypes.c_int

        self.media = media
        self.source = source
        size = lib.dsee_mem_size()
        self.mem = (ctypes.c_ubyte * size)()
        self.cfg = Cfg()
        rc = lib.dsee_open_source(self.mem, size, ctypes.byref(self.source),
                                  ctypes.byref(self.cfg))
        if rc:
            raise RuntimeError(
                f"open_source failed: rc={rc} (fs={source.input_fs}->{source.output_fs} "
                f"ch={source.channels} bits={source.input_bits}->{out_bits} "
                f"codec={source.codec} vbr={source.vbr} abr={source.abr})"
            )
        if not self.cfg.active:
            raise RuntimeError("DSEE HX inactive for these parameters")

        self.delay = (ctypes.c_int * 7)()
        gain = ctypes.c_float()
        if lib.dsee_heq_delays(self.cfg.src, self.delay):
            raise RuntimeError(f"no HEQ delay table for {self.cfg.src} Hz")
        heq_bits = media["bits"] if media["bits"] in (8, 16, 20, 24, 32) else 16
        if lib.dsee_heq_gain(heq_bits, ctypes.byref(gain)):
            raise RuntimeError(f"no HEQ gain for {heq_bits}-bit")
        self.gain = float(gain.value)

        self.ring = [np.zeros(RING * 2, dtype=np.float32) for _ in range(2)]
        self.hist = [np.zeros(HIST, dtype=np.float32) for _ in range(2)]
        self.lag = ctypes.c_int(0)
        self.hidx = ctypes.c_int(0)
        self.sec_heq = 0.0
        self.sec_f76 = 0.0
        self.sec_f80 = 0.0
        self.sec_quant = 0.0

    def _mem_u32(self, off: int) -> int:
        return int(struct.unpack_from("<I", self.mem, off)[0])

    def _mem_set_u32(self, off: int, val: int) -> None:
        self.mem[off:off + 4] = struct.pack("<I", val & 0xFFFFFFFF)

    def process_batch(self, pcm: np.ndarray, out: np.ndarray) -> None:

        cfg = self.cfg
        hop = cfg.rs_in
        n_in = hop * 2
        n_out = cfg.rs_out * 2
        a3 = hop
        a4 = cfg.rs_out
        hops = int(pcm.size // n_in)
        if hops <= 0:
            return
        pcm = np.ascontiguousarray(pcm, dtype=np.int32)
        if not out.flags["C_CONTIGUOUS"] or out.dtype != np.float32:
            raise ValueError("out must be contiguous float32")
        fp = ctypes.POINTER(ctypes.c_float)
        i32t = ctypes.POINTER(ctypes.c_int32)
        rc = self.lib.dsee_process_i32(
            self.mem,
            self.ring[0].ctypes.data_as(fp), self.ring[1].ctypes.data_as(fp),
            self.hist[0].ctypes.data_as(fp), self.hist[1].ctypes.data_as(fp),
            ctypes.byref(self.lag), ctypes.byref(self.hidx),
            self.delay, self.gain,
            pcm.ctypes.data_as(i32t), hops, n_in, n_out, a3, a4,
            out.ctypes.data_as(fp),
        )
        if rc:
            raise RuntimeError(f"dsee_process_i32 rc={rc}")

    def process_timed(self, pcm: np.ndarray, out: np.ndarray) -> None:

        cfg = self.cfg
        hop = cfg.rs_in
        n_in = hop * 2
        n_out = cfg.rs_out * 2
        a3 = hop
        a4 = cfg.rs_out
        hops = pcm.size // n_in
        lib = self.lib
        fp = ctypes.POINTER(ctypes.c_float)
        i32t = ctypes.POINTER(ctypes.c_int32)
        hi = struct.unpack("<f", struct.pack("<I", 0x3F7FFFFF))[0]

        heq_buf = np.empty(n_in, dtype=np.int32)
        frame = np.empty(n_out, dtype=np.float32)

        for h in range(hops):
            inn = np.ascontiguousarray(pcm[h * n_in:(h + 1) * n_in], dtype=np.int32)

            t0 = time.perf_counter()
            lib.dsee_heq(
                self.ring[0].ctypes.data_as(fp), self.ring[1].ctypes.data_as(fp),
                self.hist[0].ctypes.data_as(fp), self.hist[1].ctypes.data_as(fp),
                ctypes.byref(self.lag), ctypes.byref(self.hidx),
                self.delay, self.gain,
                inn.ctypes.data_as(i32t), heq_buf.ctypes.data_as(i32t), hop,
            )
            self.sec_heq += time.perf_counter() - t0

            frame.fill(0.0)
            frame[:n_in] = heq_buf.astype(np.float64) * (2.0 ** -31)

            if cfg.f76:
                t0 = time.perf_counter()
                lib.dsee_f76_frame(
                    self.mem, frame.ctypes.data_as(fp), a3, a3, 0,
                )
                self.sec_f76 += time.perf_counter() - t0

            if cfg.f80:

                old76 = self._mem_u32(76)
                self._mem_set_u32(76, 0)
                t0 = time.perf_counter()
                lib.dsee_dsp(self.mem, frame.ctypes.data_as(fp), a3, a4)
                self.sec_f80 += time.perf_counter() - t0
                self._mem_set_u32(76, old76)

            t0 = time.perf_counter()

            v = frame
            iv = np.empty(n_out, dtype=np.int32)
            ge = v >= hi
            le = v <= -1.0
            mid = ~(ge | le)
            iv[ge] = 2147483520
            iv[le] = -2147483648
            iv[mid] = (v[mid].astype(np.float64) * 2147483648.0).astype(np.int32)
            out[h * n_out:(h + 1) * n_out] = iv.astype(np.float64) / 2147483648.0
            self.sec_quant += time.perf_counter() - t0

def process_track(
    src: Path,
    out_path: Path,
    out_rate: int,
    out_bits: str,
) -> Path:

    sys.path.insert(0, str(FINAL))
    from decoder.decode import decode_pcm

    src = Path(src).resolve()
    out_path = Path(out_path)
    out_bits = normalize_bits(out_bits)

    t0 = time.perf_counter()
    source, media = set_configuration(src, out_rate, out_bits)
    sec_cfg = time.perf_counter() - t0
    print(f"[time] SetConfiguration {_fmt_sec(sec_cfg)}  "
          f"mode={source.mode} fs={source.input_fs}→{source.output_fs} "
          f"bits={source.input_bits}→{source.output_bits} "
          f"codec={source.codec} vbr={source.vbr} abr={source.abr}",
          flush=True)

    t0 = time.perf_counter()
    pcm_path = decode_pcm(src, media["rate"])
    try:
        pcm = np.fromfile(pcm_path, dtype="<i4").copy()
    finally:
        try:
            pcm_path.unlink(missing_ok=True)
        except OSError:
            pass
    sec_decode = time.perf_counter() - t0
    if pcm.size % 2:
        pcm = pcm[:-1]
    print(f"[time] decode    {_fmt_sec(sec_decode)}  "
          f"samples={pcm.size // 2} ({pcm.size * 4 / 1e6:.1f} MB s32)",
          flush=True)

    t0 = time.perf_counter()
    kern = Kern(source, media, out_bits)
    sec_open = time.perf_counter() - t0
    cfg = kern.cfg
    print(
        f"[time] open      {_fmt_sec(sec_open)}  "
        f"f76={cfg.f76} f80={cfg.f80} up={cfg.upmode} "
        f"src={cfg.src} dst={cfg.dst} rs_in={cfg.rs_in} rs_out={cfg.rs_out}",
        flush=True,
    )

    hop = cfg.rs_in
    n = (pcm.size // 2 // hop) * hop
    nframes_out = (n // hop) * cfg.rs_out
    hops = n // hop
    print(
        f"{src.name}: {n}→{nframes_out} @ {cfg.dst} Hz / {out_bits}  hops={hops}",
        flush=True,
    )

    out = np.empty(nframes_out * 2, dtype=np.float32)
    n_in = hop * 2
    n_out = cfg.rs_out * 2

    chunk = max(256, (cfg.src // hop) // 2)
    profile = bool(__import__("os").environ.get("DSEE_PROFILE"))

    t_dsp0 = time.perf_counter()
    done = 0
    last_pct = -1
    for start in range(0, hops, chunk):
        count = min(chunk, hops - start)
        if profile:
            kern.process_timed(
                pcm[start * n_in:(start + count) * n_in],
                out[start * n_out:(start + count) * n_out],
            )
        else:
            kern.process_batch(
                pcm[start * n_in:(start + count) * n_in],
                out[start * n_out:(start + count) * n_out],
            )
        done += count
        pct = int(100.0 * done / hops) if hops else 100
        if pct >= last_pct + 5 or done >= hops:
            last_pct = pct - (pct % 5)
            rate = done / max(time.perf_counter() - t_dsp0, 1e-6)
            eta = (hops - done) / max(rate, 1e-6)
            print(
                f"  dsp {done}/{hops} ({pct}%)  {rate:.0f} hop/s  ETA {_fmt_sec(eta)}",
                flush=True,
            )
    sec_dsp = time.perf_counter() - t_dsp0

    print(
        f"[time] dsp total {_fmt_sec(sec_dsp)}  ({hops / max(sec_dsp, 1e-6):.0f} hop/s)",
        flush=True,
    )
    if profile:
        dsp_parts = kern.sec_heq + kern.sec_f76 + kern.sec_f80 + kern.sec_quant
        print(f"         modules {_fmt_sec(dsp_parts)}", flush=True)
        print(f"         heq     {_fmt_sec(kern.sec_heq)}", flush=True)
        print(f"         f76     {_fmt_sec(kern.sec_f76)}"
              f"{'' if cfg.f76 else '  (off)'}", flush=True)
        print(f"         f80     {_fmt_sec(kern.sec_f80)}"
              f"{'' if cfg.f80 else '  (off)'}", flush=True)
        print(f"         quant   {_fmt_sec(kern.sec_quant)}", flush=True)

    t0 = time.perf_counter()
    write_wav(out_path, cfg.dst, out, out_bits)
    sec_wav = time.perf_counter() - t0
    print(f"[time] write_wav {_fmt_sec(sec_wav)}", flush=True)

    total = sec_cfg + sec_decode + sec_open + sec_dsp + sec_wav
    print(f"[time] TOTAL    {_fmt_sec(total)}", flush=True)
    print(out_path.resolve(), flush=True)
    return out_path.resolve()
