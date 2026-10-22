# Sony DSEE HX — Reverse Engineering & Reimplementation

**Sony DSEE HX：逆向与实现**

Offline rebuild of the Sony **DSEE HX** bandwidth-extension filter from Music Center: ffmpeg decode + a C kernel (HEQ / DSX), writing 16/24/32-bit WAV.

Sony Music Center 里 **DSEE HX** 带宽扩展滤镜的离线实现：ffmpeg 解码 + C 内核（HEQ / DSX），输出 16/24/32-bit WAV。

---

## Layout

## 结构

```
cli.py          CLI entry / 命令行入口
run_song.py     open → process → write WAV / 建流→处理→写 WAV
ffmpeg_bin.py   locate ffmpeg / ffprobe / 定位 ffmpeg、ffprobe
bridge/         ffprobe facts + ctypes → config / 取事实并调 config
config/         SetConfiguration rules (C) / SetConfiguration 规则（C）
decoder/        ffmpeg → stereo s32le / 解码为立体声 s32le
kern/           DSP (open → heq → frame → dsx → ext80) / DSP 内核
build.bat       build/dsee_kern.dll / 编译 DLL
```

---

## Requirements

## 依赖

| Component / 组件 | Notes / 说明 |
|--|--|
| Windows x64 | `build.bat` produces the DLL / 产出 DLL |
| Python 3.10+ | `numpy` |
| MinGW-w64 `gcc` | on PATH, or set `DSEE_GCC` / 在 PATH 中，或设置 `DSEE_GCC` |
| ffmpeg + ffprobe | on PATH, or set `DSEE_FFMPEG` / `DSEE_FFPROBE` / 在 PATH 中，或设置对应环境变量 |

```bat
pip install numpy
```

```bat
set DSEE_FFMPEG=C:\path\to\ffmpeg.exe
set DSEE_FFPROBE=C:\path\to\ffprobe.exe
```

## Build

## 编译

```bat
build.bat
```

→ `build\dsee_kern.dll` (the CLI also rebuilds when sources are newer)

→ `build\dsee_kern.dll`（源码更新时 CLI 也会自动重编）

## Usage

## 用法

```bat
python cli.py <input> <output.wav> <output_rate> <bits>
```

- **output_rate**: same rate family as content, and 1× / 2× / 4× only  
  **输出采样率**：须与内容率同族，且仅为 1× / 2× / 4×  
  - 44100 → 44100 / 88200 / **176400**  
  - 48000 → 48000 / 96000 / 192000  
  - 96000 → 96000 / 192000 (not straight to 176400) /（不能直接到 176400）
- **bits**: `16` | `24` | `32`  
  **位深**：`16` | `24` | `32`

```bat
python cli.py track.flac out.wav 176400 24
```

```bat
set DSEE_PROFILE=1
python cli.py track.flac out.wav 176400 24
```

## Pipeline

## 管线

1. **bridge + config**: ffprobe facts → `dsee_fill_source` → `DseeSource`  
   **bridge + config**：ffprobe 事实 → `dsee_fill_source` → `DseeSource`
2. **decoder**: ffmpeg → s32le  
   **decoder**：ffmpeg → s32le
3. **kern**: `dsee_open_source` → HEQ → DSX (mem+76 / mem+80) → quant-style out → WAV  
   **kern**：`dsee_open_source` → HEQ → DSX（mem+76 / mem+80）→ 量化风格出口 → WAV

`codec_map` only covers common Music Center enums; formats ffmpeg can open but MC never mapped often fall back to codec `16` — not guaranteed to match “what stock would do” for unsupported types.

`codec_map` 只覆盖 Music Center 常见枚举；ffmpeg 能开但原版未映射的格式常回落 codec `16`——不保证与「原版对未支持格式会怎么做」一致。

---

An earlier open-source project, **[DSRE — Digital Sound Resolution Enhancer](https://github.com/x1aoqv/DSRE---Digital-Sound-Resolution-Enhancer)**, was mostly built around repeated **SSB (single-sideband)** modulation. That is **not** how DSEE HX works — it only looked a bit similar in the spectrum.

早先的开源项目 **[DSRE — Digital Sound Resolution Enhancer](https://github.com/x1aoqv/DSRE---Digital-Sound-Resolution-Enhancer)** 主要原理为多次 **SSB（单边带）调制**，但 DSEE HX 的原理并不是那样，只是频谱上看起来有点像。

---

## Thanks: REA

## 致谢：REA

Reverse engineering used **[REA — Reverse Engineer Anything](https://github.com/morluto/rea)** ([site](https://reverseengineeranything.app/get-started/)): disassembly, pseudocode, call graphs, runtime evidence — to pin down `OmgDseeHxFilter.ax`, `Opc*`.

逆向与对照依赖 **[REA — Reverse Engineer Anything](https://github.com/morluto/rea)**（[网站](https://reverseengineeranything.app/get-started/)）：反汇编 / 伪代码 / 调用关系 / 运行时证据等，用来确认 `OmgDseeHxFilter.ax`、`Opc*`。

## License

## 许可说明

For research / interoperability study. DSEE HX, Music Center, and related marks belong to their owners. Check local law and licenses before your use case.

研究 / 互操作学习用途。DSEE HX、Music Center 等商标归权利人所有。请自行确认当地法律与许可是否允许你的用途。
