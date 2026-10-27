import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type MouseEvent as ReactMouseEvent,
} from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWebview } from "@tauri-apps/api/webview";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { open } from "@tauri-apps/plugin-dialog";
import { t } from "./i18n";
import { dlog } from "./debugLog";
import {
  loadDialogMemory,
  resolveDialogStart,
  saveDialogMemory,
} from "./dialogMemory";
import RoundSelect from "./RoundSelect";
import SettingsPage from "./SettingsPage";
import {
  applyTheme,
  loadSettings,
  saveSettings,
  type AppSettings,
} from "./settings";
import WindowControls from "./WindowControls";
import "./App.css";

function onTitlebarDblClick(e: ReactMouseEvent) {
  if ((e.target as HTMLElement).closest("button")) return;
  void getCurrentWindow().toggleMaximize();
}

type BatchResult = {
  ok: number;
  fail: number;
  slots: number;
  peakTracks: number;
  jobs: number;
  wallSecs: number;
  outputs: string[];
  errors: string[];
};

type ProcessProgress = {
  stage: string;
  percent: number;
  message: string;
};

type TrackProgress = {
  input: string;
  hopsDone: number;
  hopsTotal: number;
  percent: number;
  failed: boolean;
};

type TrackAnalysis = {
  path: string;
  inputRate: number;
  inputBits: number;
  duration: number;
  level: number;
  f76: boolean;
  f80: boolean;
  mode: number;
  upFactor: number;
  outputRate: number;
  outputBits: string;
  sameRateRepair: boolean;
  allowedUps: number[];
  slotCost: number;
  active: boolean;
  /** Session ready (DSX and/or HEQ-only, matches MC). */
  processable: boolean;
  error?: string | null;
};

type RowProg = {
  percent: number;
  hopsDone: number;
  hopsTotal: number;
  failed: boolean;
  done: boolean;
};

type Track = {
  path: string;
  bits: string;
  analysis: TrackAnalysis | null;
  loading: boolean;
  /** 高级模式下用户强制的 F76 Level；null = 自动 */
  levelOverride: number | null;
};

/** 展示用：16（关）在前，数值递减 */
const LEVELS = [16, 15, 14, 13, 12, 11, 10, 9, 8] as const;
const DEFAULT_UP = 4;
const DEFAULT_BITS = "32";
const AUDIO_EXT = ["wav", "flac", "mp3", "m4a", "aac", "ogg", "wma", "aiff", "ape"];

/** 高级开启用设置里的输出位深；关闭时固定 32（DSP 默认）。 */
function effectiveBits(s: AppSettings): string {
  return s.advanced ? s.advOutputBits : DEFAULT_BITS;
}

/** 该轨允许的最高上采样倍率（不含 1× 同率） */
function maxAllowedUp(a: TrackAnalysis): number {
  const ups = (a.allowedUps ?? []).filter((u) => u > 1);
  return ups.length ? Math.max(...ups) : 1;
}

function fileName(path: string) {
  const i = Math.max(path.lastIndexOf("\\"), path.lastIndexOf("/"));
  return i >= 0 ? path.slice(i + 1) : path;
}

function parentDir(path: string) {
  const i = Math.max(path.lastIndexOf("\\"), path.lastIndexOf("/"));
  return i > 0 ? path.slice(0, i) : "";
}

function isDseeOutput(path: string) {
  const n = fileName(path).toLowerCase();
  // `track_dsee.flac` / legacy `track_dsee_88200.wav`
  return n.includes("_dsee.") || n.includes("_dsee_") || n.endsWith("_dsee.wav");
}

function isAudioPath(path: string) {
  const n = fileName(path);
  const dot = n.lastIndexOf(".");
  if (dot < 0) return false;
  const ext = n.slice(dot + 1).toLowerCase();
  return (AUDIO_EXT as readonly string[]).includes(ext);
}

function App() {
  const [tracks, setTracks] = useState<Track[]>([]);
  const [outputDir, setOutputDir] = useState("");
  const [busy, setBusy] = useState(false);
  const [percent, setPercent] = useState(0);
  const [rowProg, setRowProg] = useState<Record<string, RowProg>>({});
  /** 工具栏批量上采样；非 null 时覆盖下行；全员最高一致才填，否则空 */
  const [globalUp, setGlobalUp] = useState<number | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [settings, setSettings] = useState<AppSettings>(() => loadSettings());
  const busyRef = useRef(busy);
  busyRef.current = busy;
  const settingsRef = useRef(settings);
  settingsRef.current = settings;
  const ingestRef = useRef<(paths: string[]) => Promise<void>>(async () => {});
  const tableWrapRef = useRef<HTMLDivElement>(null);
  const theadRef = useRef<HTMLTableSectionElement>(null);
  const d = useMemo(() => t(settings.language), [settings.language]);

  useEffect(() => {
    applyTheme(settings);
    if (settings.baseColor !== "system") return;
    const refresh = () => applyTheme(settings);
    window.addEventListener("focus", refresh);
    document.addEventListener("visibilitychange", refresh);
    return () => {
      window.removeEventListener("focus", refresh);
      document.removeEventListener("visibilitychange", refresh);
    };
  }, [settings.theme, settings.baseColor]);

  useEffect(() => {
    if (!settingsOpen) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") setSettingsOpen(false);
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [settingsOpen]);

  /** 实测表头两行高度，校正 sticky top，避免与首行数据重叠 */
  useEffect(() => {
    const wrap = tableWrapRef.current;
    const thead = theadRef.current;
    if (!wrap || !thead) return;
    const sync = () => {
      const tool = thead.querySelector(
        "tr.toolbar-row",
      ) as HTMLTableRowElement | null;
      const labels = thead.querySelector(
        "tr.labels-row",
      ) as HTMLTableRowElement | null;
      const toolH = tool?.getBoundingClientRect().height ?? 0;
      const labelsH = labels?.getBoundingClientRect().height ?? 0;
      wrap.style.setProperty("--thead-tool-h", `${toolH}px`);
      wrap.style.setProperty("--thead-labels-h", `${labelsH}px`);
      wrap.style.setProperty("--thead-total-h", `${toolH + labelsH}px`);
    };
    sync();
    const ro = new ResizeObserver(sync);
    ro.observe(thead);
    return () => ro.disconnect();
  }, [settings.language, tracks.length, busy, percent]);

  const canRun = useMemo(
    () =>
      tracks.length > 0 &&
      Boolean(outputDir) &&
      !busy &&
      tracks.every(
        (t) =>
          Boolean(t.analysis?.processable ?? t.analysis?.active) && !t.loading,
      ),
    [tracks, outputDir, busy],
  );

  /**
   * 总选框上采样选项：
   * - 默认：≤48k 给 F80；>48k / 同率修复给 1×（MC）
   * - 高级：始终解锁 1× / 2× / 4× 供强制
   */
  const headerUpOptions = useMemo(() => {
    if (settings.advanced) return [1, 2, 4];
    const s = new Set<number>();
    for (const t of tracks) {
      const a = t.analysis;
      if (!a) continue;
      if (a.inputRate > 48000 || a.sameRateRepair || a.upFactor === 1) {
        s.add(1);
      }
      if (a.inputRate <= 48000) {
        for (const u of a.allowedUps) {
          if (u > 1) s.add(u);
        }
      }
    }
    if (s.size === 0) {
      s.add(2);
      s.add(4);
    }
    return [...s].sort((a, b) => a - b);
  }, [tracks, settings.advanced]);

  /** 高级模式下总控 Level：全员一致才显示，否则空 */
  const headerLevel = useMemo(() => {
    if (!settings.advanced || !tracks.length) return null;
    const levels = tracks.map((t) => {
      if (t.levelOverride != null) return t.levelOverride;
      const a = t.analysis;
      if (!a) return 16;
      return a.f76 ? a.level : 16;
    });
    const first = levels[0];
    return levels.every((l) => l === first) ? first : null;
  }, [tracks, settings.advanced]);

  useEffect(() => {
    let unlistenBatch: (() => void) | undefined;
    let unlistenTrack: (() => void) | undefined;
    let unlistenDrop: (() => void) | undefined;
    listen<ProcessProgress>("process-progress", (e) => {
      setPercent(e.payload.percent);
    }).then((fn) => {
      unlistenBatch = fn;
    });
    listen<TrackProgress>("track-progress", (e) => {
      const p = e.payload;
      setRowProg((prev) => ({
        ...prev,
        [p.input]: {
          percent: p.failed ? 0 : p.percent,
          hopsDone: p.hopsDone,
          hopsTotal: p.hopsTotal,
          failed: p.failed,
          done: !p.failed && p.hopsTotal > 0 && p.hopsDone >= p.hopsTotal,
        },
      }));
    }).then((fn) => {
      unlistenTrack = fn;
    });
    getCurrentWebview()
      .onDragDropEvent((e) => {
        const p = e.payload;
        if (p.type === "enter" || p.type === "over") {
          setDragOver(true);
        } else if (p.type === "leave") {
          setDragOver(false);
        } else if (p.type === "drop") {
          setDragOver(false);
          void ingestRef.current(p.paths);
        }
      })
      .then((fn) => {
        unlistenDrop = fn;
      });
    const s = loadSettings();
    dlog("boot", {
      lang: s.language,
      theme: s.theme,
      base: s.baseColor,
      advanced: s.advanced,
    });
    void (async () => {
      type MediaToolsInfo = {
        ffmpeg: string | null;
        ffmpegOk: boolean;
        ffprobe: string | null;
        ffprobeOk: boolean;
      };
      if (!s.mediaToolsBootstrapped) {
        // First launch: scan app folder → env → PATH once, then persist.
        const info = await invoke<MediaToolsInfo>("detect_media_tools");
        const next: AppSettings = {
          ...s,
          ffmpegPath: info.ffmpeg ?? "",
          ffprobePath: info.ffprobe ?? "",
          mediaToolsBootstrapped: true,
        };
        saveSettings(next);
        setSettings(next);
        dlog("media", {
          boot: "detect",
          ffmpeg: next.ffmpegPath || "-",
          ffprobe: next.ffprobePath || "-",
        });
      } else {
        await invoke("set_media_tools", {
          ffmpeg: s.ffmpegPath.trim() || null,
          ffprobe: s.ffprobePath.trim() || null,
        });
      }
    })();
    void invoke("set_advanced_overrides", {
      advanced: s.advanced,
      heq: s.advHeq,
      f80: 0,
      fileRate: s.advanced ? s.advOutputRate : 0,
      fileBits: s.advanced ? Number(s.advOutputBits) : 0,
      fileCodec: s.advanced ? s.advOutputCodec : "",
    });
    return () => {
      unlistenBatch?.();
      unlistenTrack?.();
      void unlistenDrop?.();
    };
  }, []);

  async function analyzeOne(
    path: string,
    bits: string,
    upFactor?: number,
    levelOverride?: number | null,
  ): Promise<TrackAnalysis> {
    const s = settingsRef.current;
    const level = s.advanced ? (levelOverride ?? null) : null;
    return invoke<TrackAnalysis>("analyze_queue_track", {
      path,
      upFactor: upFactor ?? null,
      outputBits: bits,
      level,
    });
  }

  /**
   * 上采样默认策略（对齐 Music Center）：
   * - 内容 ≤48 kHz：可上采样轨取最高 F80；全员相同则写入总控
   * - 内容 >48 kHz：默认同率，不自动抬 F80（高级模式里再手动强制）
   */
  async function applyUpsPolicy(list: Track[]) {
    if (list.some((t) => t.loading)) return;
    const adv = settingsRef.current.advanced;
    const upsTracks = list.filter(
      (t) =>
        t.analysis?.active &&
        maxAllowedUp(t.analysis) > 1 &&
        (adv || t.analysis.inputRate <= 48000),
    );
    if (!upsTracks.length) {
      setGlobalUp(null);
      dlog("up_policy", { n: list.length, global: "null", reason: "no_ups" });
      return;
    }
    const maxes = upsTracks.map((t) => maxAllowedUp(t.analysis!));
    const common = maxes[0];
    const allSame = maxes.every((m) => m === common);
    const bits = effectiveBits(settingsRef.current);
    dlog("up_policy", {
      n: upsTracks.length,
      maxes,
      allSame,
      global: allSame ? common : null,
    });

    if (allSame) {
      setGlobalUp(common);
      await Promise.all(
        upsTracks.map(async (t) => {
          if (t.analysis!.upFactor === common) return;
          try {
            const a = await analyzeOne(
              t.path,
              bits,
              common,
              t.levelOverride,
            );
            setTracks((prev) =>
              prev.map((x) =>
                x.path === t.path ? { ...x, analysis: a, bits: a.outputBits } : x,
              ),
            );
          } catch {
            /* keep prior analysis */
          }
        }),
      );
      return;
    }

    setGlobalUp(null);
    await Promise.all(
      upsTracks.map(async (t) => {
        const want = maxAllowedUp(t.analysis!);
        if (t.analysis!.upFactor === want) return;
        try {
          const a = await analyzeOne(
            t.path,
            bits,
            want,
            t.levelOverride,
          );
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path ? { ...x, analysis: a, bits: a.outputBits } : x,
            ),
          );
        } catch {
          /* keep prior analysis */
        }
      }),
    );
  }

  async function ingestPaths(raw: string[]) {
    if (busyRef.current) return;
    const list = raw.filter(
      (p) => typeof p === "string" && isAudioPath(p) && !isDseeOutput(p),
    );
    if (!list.length) {
      dlog("ingest", { n: 0, skipped: raw.length });
      return;
    }
    const bits = effectiveBits(settingsRef.current);
    dlog("ingest", { n: list.length, bits });

    let working: Track[] = [];

    setTracks((prev) => {
      const seen = new Set(prev.map((t) => t.path));
      const next = [...prev];
      for (const p of list) {
        if (!seen.has(p)) {
          seen.add(p);
          next.push({
            path: p,
            bits,
            analysis: null,
            loading: true,
            levelOverride: null,
          });
        }
      }
      working = next;
      return next;
    });

    for (const p of list) {
      try {
        // 先按轨最高倍率探，再由 applyUpsPolicy 决定总控
        const a = await analyzeOne(p, bits, undefined, null);
        working = working.map((t) =>
          t.path === p
            ? { ...t, analysis: a, loading: false, bits: a.outputBits }
            : t,
        );
        setTracks(working);
      } catch (e) {
        dlog("err", { event: "ingest", path: p, err: String(e) });
        working = working.map((t) =>
          t.path === p
            ? {
                ...t,
                loading: false,
                analysis: {
                  path: p,
                  inputRate: 0,
                  inputBits: 0,
                  duration: 0,
                  level: 16,
                  f76: false,
                  f80: false,
                  mode: 0,
                  upFactor: 1,
                  outputRate: 0,
                  outputBits: bits,
                  sameRateRepair: false,
                  allowedUps: [],
                  slotCost: 1,
                  active: false,
                  processable: false,
                  error: String(e),
                },
              }
            : t,
        );
        setTracks(working);
      }
    }

    await applyUpsPolicy(working);
  }

  ingestRef.current = ingestPaths;

  async function addTracks() {
    const mem = loadDialogMemory();
    const defaultPath = await resolveDialogStart(mem.addDir);
    const picked = await open({
      multiple: true,
      filters: [{ name: "Audio", extensions: [...AUDIO_EXT] }],
      ...(defaultPath ? { defaultPath } : {}),
    });
    if (!picked) return;
    const list = (Array.isArray(picked) ? picked : [picked]).filter(
      (p): p is string => typeof p === "string",
    );
    if (list[0]) {
      const dir = parentDir(list[0]);
      if (dir) saveDialogMemory({ addDir: dir });
    }
    await ingestPaths(list);
  }

  function removeTrack(idx: number) {
    const t = tracks[idx];
    if (t) {
      dlog("ingest", { drop: t.path });
      void invoke("drop_queue_track", { path: t.path });
    }
    const next = tracks.filter((_, i) => i !== idx);
    setTracks(next);
    void applyUpsPolicy(next);
  }

  function clearTracks() {
    dlog("ingest", { clear: tracks.length });
    void invoke("clear_queue_prep");
    setTracks([]);
    setRowProg({});
    setGlobalUp(null);
  }

  async function reanalyzeAll(up: number | undefined, bits: string) {
    setTracks((prev) => prev.map((t) => ({ ...t, bits, loading: true })));
    const snapshot = tracks;
    await Promise.all(
      snapshot.map(async (t) => {
        try {
          const a = await analyzeOne(t.path, bits, up, t.levelOverride);
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path
                ? { ...x, bits, analysis: a, loading: false }
                : x,
            ),
          );
        } catch (e) {
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path ? { ...x, loading: false } : x,
            ),
          );
          dlog("err", { event: "reanalyze", path: t.path, err: String(e) });
        }
      }),
    );
  }

  async function applyGlobalUp(up: number) {
    dlog("up_policy", { global_set: up, n: tracks.length });
    setGlobalUp(up);
    await reanalyzeAll(up, effectiveBits(settingsRef.current));
  }

  async function applyGlobalLevel(level: number) {
    if (!settingsRef.current.advanced) return;
    dlog("up_policy", { global_level: level, n: tracks.length });
    setTracks((prev) =>
      prev.map((t) => ({ ...t, levelOverride: level, loading: true })),
    );
    const snapshot = tracks;
    await Promise.all(
      snapshot.map(async (t) => {
        const up =
          globalUp ??
          (t.analysis?.sameRateRepair ? 1 : t.analysis?.upFactor);
        try {
          const a = await analyzeOne(
            t.path,
            effectiveBits(settingsRef.current),
            up,
            level,
          );
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path
                ? {
                    ...x,
                    bits: a.outputBits,
                    levelOverride: level,
                    analysis: a,
                    loading: false,
                  }
                : x,
            ),
          );
        } catch (e) {
          dlog("err", { event: "set_level", path: t.path, err: String(e) });
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path ? { ...x, loading: false } : x,
            ),
          );
        }
      }),
    );
  }

  async function setTrackUp(idx: number, up: number) {
    const t = tracks[idx];
    if (!t) return;
    dlog("up_policy", { row: t.path, up });
    setGlobalUp(null);
    // Optimistic: show chosen factor immediately (avoids snap-back while analyzing).
    setTracks((prev) =>
      prev.map((x, i) =>
        i === idx
          ? {
              ...x,
              loading: true,
              analysis: x.analysis
                ? { ...x.analysis, upFactor: up }
                : x.analysis,
            }
          : x,
      ),
    );
    try {
      const a = await analyzeOne(
        t.path,
        effectiveBits(settingsRef.current),
        up,
        t.levelOverride,
      );
      setTracks((prev) =>
        prev.map((x, i) =>
          i === idx
            ? { ...x, bits: a.outputBits, analysis: a, loading: false }
            : x,
        ),
      );
    } catch (e) {
      setTracks((prev) =>
        prev.map((x, i) => (i === idx ? { ...x, loading: false } : x)),
      );
      dlog("err", { event: "set_up", path: t.path, err: String(e) });
    }
  }

  async function setTrackLevel(idx: number, level: number) {
    const t = tracks[idx];
    if (!t || !settingsRef.current.advanced) return;
    dlog("up_policy", { row: t.path, level });
    const up =
      globalUp ??
      (t.analysis?.sameRateRepair ? 1 : t.analysis?.upFactor);
    setTracks((prev) =>
      prev.map((x, i) =>
        i === idx ? { ...x, levelOverride: level, loading: true } : x,
      ),
    );
    try {
      const a = await analyzeOne(
        t.path,
        effectiveBits(settingsRef.current),
        up,
        level,
      );
      setTracks((prev) =>
        prev.map((x, i) =>
          i === idx
            ? {
                ...x,
                bits: a.outputBits,
                levelOverride: level,
                analysis: a,
                loading: false,
              }
            : x,
        ),
      );
    } catch (e) {
      setTracks((prev) =>
        prev.map((x, i) => (i === idx ? { ...x, loading: false } : x)),
      );
      dlog("err", { event: "set_level", path: t.path, err: String(e) });
    }
  }

  async function pickOutputDir() {
    const mem = loadDialogMemory();
    // 只记「浏览输出」路径，勿用 outputDir（添加文件时常被自动填成 addDir）
    const defaultPath = await resolveDialogStart(mem.outDir);
    const dir = await open({
      directory: true,
      multiple: false,
      ...(defaultPath ? { defaultPath } : {}),
    });
    if (typeof dir === "string") {
      setOutputDir(dir);
      saveDialogMemory({ outDir: dir });
      dlog("ingest", { out: dir });
    }
  }

  async function run() {
    setBusy(true);
    setPercent(0);
    setRowProg({});
    dlog("run", { n: tracks.length, out: outputDir });
    const t0 = performance.now();
    await new Promise((r) => requestAnimationFrame(() => r(null)));
    try {
      const r = await invoke<BatchResult>("process_queue", {
        tracks: tracks.map((t) => ({
          path: t.path,
          outputRate: t.analysis!.outputRate,
          outputBits: effectiveBits(settingsRef.current),
          slotCost: t.analysis!.slotCost,
        })),
        outputDir,
      });
      const sec = ((performance.now() - t0) / 1000).toFixed(2);
      setPercent(100);
      dlog("run", {
        ok: r.ok,
        fail: r.fail,
        wall_s: r.wallSecs,
        ui_s: Number(sec),
        slots: r.slots,
        peak: r.peakTracks,
        out: outputDir,
      });
      for (const e of r.errors) {
        dlog("err", { event: "run", err: e });
      }
      // Sessions were consumed; re-analyze so a second run stays cache-hot.
      for (const t of tracks) {
        const up = t.analysis?.sameRateRepair ? 1 : t.analysis?.upFactor;
        void analyzeOne(
          t.path,
          effectiveBits(settingsRef.current),
          up,
          t.levelOverride,
        ).then((a) => {
          setTracks((prev) =>
            prev.map((x) =>
              x.path === t.path
                ? { ...x, bits: a.outputBits, analysis: a }
                : x,
            ),
          );
        });
      }
    } catch (e) {
      dlog("err", { event: "run", err: String(e) });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="app" data-theme={settings.theme}>
      <header
        className="titlebar"
        data-tauri-drag-region
        onDoubleClick={onTitlebarDblClick}
      >
        <div className="brand" data-tauri-drag-region>
          <span className="brand-mark" data-tauri-drag-region>
            DSEE HX RE
          </span>
        </div>
        <div className="titlebar-right">
          <WindowControls />
        </div>
      </header>

      <div
        ref={tableWrapRef}
        className={`table-wrap${dragOver ? " is-dragover" : ""}`}
      >
        {tracks.length === 0 ? (
          <div className="empty-hint">
            <p>{d.emptyDrag}</p>
            <p>{d.emptyOrAdd}</p>
          </div>
        ) : null}
        <table className="track-table">
          <colgroup>
            <col className="col-idx" />
            <col className="col-name" />
            <col className="col-meta" />
            <col className="col-mode" />
            <col className="col-mode" />
            <col className="col-status" />
            <col className="col-act" />
          </colgroup>
          <thead ref={theadRef}>
            <tr className="toolbar-row">
              <th colSpan={3}>
                <div className="toolbar-span-left">
                  <button type="button" onClick={addTracks} disabled={busy}>
                    {d.add}
                  </button>
                  <div className="out-field">
                    <input
                      readOnly
                      value={outputDir}
                      placeholder={d.outputPlaceholder}
                      title={outputDir}
                    />
                    <button type="button" onClick={pickOutputDir} disabled={busy}>
                      {d.browse}
                    </button>
                  </div>
                </div>
              </th>
              <th className="col-mode toolbar-batch-cell">
                <RoundSelect
                  className={`toolbar-batch${headerLevel === null ? " is-blank" : ""}`}
                  value={String(headerLevel ?? 16)}
                  disabled={
                    busy || tracks.length === 0 || !settings.advanced
                  }
                  onChange={(v) => void applyGlobalLevel(Number(v))}
                  aria-label={d.batchLevel}
                  title={d.batchLevel}
                  options={LEVELS.map((lv) => ({
                    value: String(lv),
                    label: `Level ${lv}`,
                  }))}
                />
              </th>
              <th className="col-mode toolbar-batch-cell">
                <RoundSelect
                  className={`toolbar-batch${globalUp === null ? " is-blank" : ""}`}
                  value={String(
                    globalUp !== null && headerUpOptions.includes(globalUp)
                      ? globalUp
                      : headerUpOptions.includes(DEFAULT_UP)
                        ? DEFAULT_UP
                        : (headerUpOptions[0] ?? DEFAULT_UP),
                  )}
                  disabled={busy || tracks.length === 0}
                  onChange={(v) => void applyGlobalUp(Number(v))}
                  aria-label={d.batchUp}
                  title={d.batchUp}
                  options={headerUpOptions.map((u) => ({
                    value: String(u),
                    label: `${u}×`,
                  }))}
                />
              </th>
              <th className="col-status toolbar-status-cell">
                <div className="toolbar-span-right">
                  <button
                    type="button"
                    className="toolbar-settings"
                    disabled={busy || settingsOpen}
                    onClick={() => setSettingsOpen(true)}
                  >
                    {d.settingsBtn}
                  </button>
                </div>
              </th>
              <th className="col-act">
                <div className="act-btns">
                  <button
                    type="button"
                    onClick={clearTracks}
                    disabled={busy || tracks.length === 0}
                  >
                    {d.clear}
                  </button>
                  <button
                    type="button"
                    className="toolbar-start"
                    disabled={!canRun}
                    onClick={run}
                  >
                    {busy ? d.processing : d.start}
                  </button>
                </div>
              </th>
            </tr>
            <tr className="labels-row">
              <th className="col-idx">{d.colIdx}</th>
              <th className="col-name">{d.colName}</th>
              <th className="col-meta">{d.colRate}</th>
              <th className="col-mode">{d.colSame}</th>
              <th className="col-mode">{d.colUp}</th>
              <th className="col-status">{d.colStatus}</th>
              <th className="col-act" />
            </tr>
          </thead>
          <tbody>
            {tracks.length === 0
              ? null
              : tracks.map((track, idx) => {
                const rp = rowProg[track.path];
                const a = track.analysis;
                const pct = rp?.failed ? 0 : (rp?.percent ?? 0);
                const heqOnly = Boolean(
                  a && !a.active && (a.processable ?? false),
                );
                const rowClass = [
                  "track-row",
                  rp?.failed || (a && !a.processable && !a.active)
                    ? "is-failed"
                    : "",
                  rp?.done ? "is-done" : "",
                  busy && rp && !rp.done && !rp.failed ? "is-running" : "",
                ]
                  .filter(Boolean)
                  .join(" ");
                // Closed F76 ≡ level 16；高级模式可用用户覆盖
                const levelShow =
                  track.levelOverride != null
                    ? track.levelOverride
                    : a
                      ? a.f76
                        ? a.level
                        : 16
                      : 16;
                const upOptions: number[] = [];
                if (a) {
                  if (settings.advanced) {
                    upOptions.push(1, 2, 4);
                  } else if (a.inputRate > 48000) {
                    // Default MC: hi-res stays same-rate only.
                    upOptions.push(1);
                  } else {
                    if (a.sameRateRepair || a.upFactor === 1) {
                      upOptions.push(1);
                    }
                    for (const u of a.allowedUps.length ? a.allowedUps : [2, 4]) {
                      if (!upOptions.includes(u)) upOptions.push(u);
                    }
                  }
                  if (!upOptions.length) upOptions.push(1, 2, 4);
                }
                return (
                  <tr
                    key={track.path}
                    className={rowClass}
                    style={{ "--row-pct": `${pct}%` } as CSSProperties}
                  >
                    <td className="col-idx">{idx + 1}</td>
                    <td className="col-name" title={track.path}>
                      {fileName(track.path)}
                    </td>
                    <td className="col-meta" title={parentDir(track.path)}>
                      {track.loading
                        ? d.analyzing
                        : a
                          ? `${a.inputRate} Hz`
                          : "—"}
                    </td>
                    <td className="col-mode">
                      {track.loading ? (
                        "…"
                      ) : (
                        <RoundSelect
                          value={String(levelShow)}
                          disabled={busy || !settings.advanced || !a}
                          onChange={(v) =>
                            void setTrackLevel(idx, Number(v))
                          }
                          aria-label={d.colSame}
                          title={
                            a?.f76
                              ? `${d.colSame} Level ${a.level}`
                              : `${d.colSame} off (Level 16)`
                          }
                          options={LEVELS.map((lv) => ({
                            value: String(lv),
                            label: `Level ${lv}`,
                          }))}
                        />
                      )}
                    </td>
                    <td className="col-mode">
                      {track.loading ? (
                        "…"
                      ) : (
                        <RoundSelect
                          value={String(
                            upOptions.includes(a?.upFactor ?? -1)
                              ? (a?.upFactor as number)
                              : (upOptions[0] ?? 1),
                          )}
                          // Keep enabled when inactive so advanced can force 2×/4× / Level.
                          disabled={busy || !a}
                          onChange={(v) => void setTrackUp(idx, Number(v))}
                          aria-label={d.colUp}
                          options={upOptions.map((u) => ({
                            value: String(u),
                            label: `${u}×`,
                          }))}
                        />
                      )}
                    </td>
                    <td
                      className={[
                        "col-status",
                        a && !a.processable && !a.active
                          ? "is-error"
                          : heqOnly
                            ? "is-heq-only"
                            : rp?.failed
                              ? "is-error"
                              : rp?.done
                                ? "is-done"
                                : rp
                                  ? "is-processing"
                                  : "",
                      ]
                        .filter(Boolean)
                        .join(" ")}
                    >
                      {track.loading
                        ? d.analyzing
                        : heqOnly
                          ? d.inactiveHiRes
                          : a && !a.processable && !a.active
                            ? d.unavailable
                            : rp?.failed
                              ? d.statusError
                              : rp?.done
                                ? d.statusDone
                                : rp
                                  ? d.statusProcessing
                                  : d.statusPending}
                    </td>
                    <td className="col-act">
                      <div className="act-btns">
                        <button
                          type="button"
                          className="ghost"
                          disabled={busy}
                          onClick={() => removeTrack(idx)}
                        >
                          {d.delete}
                        </button>
                        <button
                          type="button"
                          className="ghost"
                          disabled
                          title={d.detail}
                        >
                          {d.detail}
                        </button>
                      </div>
                    </td>
                  </tr>
                );
              })}
          </tbody>
        </table>
      </div>

      {settingsOpen ? (
        <div
          className="settings-modal-backdrop"
          role="presentation"
          onMouseDown={(e) => {
            if (e.target === e.currentTarget) setSettingsOpen(false);
          }}
        >
          <div
            className="settings-modal"
            role="dialog"
            aria-modal="true"
            aria-label={d.settingsTitle}
          >
            <SettingsPage
              dict={d}
              settings={settings}
              onChange={setSettings}
              onAdvancedApplied={(next) => {
                settingsRef.current = next;
                if (!tracks.length) return;
                const bits = effectiveBits(next);
                const clearLevel = !next.advanced;
                void (async () => {
                  setTracks((prev) =>
                    prev.map((t) => ({
                      ...t,
                      bits,
                      loading: true,
                      levelOverride: clearLevel ? null : t.levelOverride,
                    })),
                  );
                  await Promise.all(
                    tracks.map(async (t) => {
                      const lv = clearLevel ? null : t.levelOverride;
                      const up =
                        globalUp ??
                        (t.analysis?.sameRateRepair
                          ? 1
                          : t.analysis?.upFactor);
                      try {
                        const a = await analyzeOne(
                          t.path,
                          bits,
                          up,
                          lv,
                        );
                        setTracks((prev) =>
                          prev.map((x) =>
                            x.path === t.path
                              ? {
                                  ...x,
                                  bits: a.outputBits,
                                  analysis: a,
                                  levelOverride: lv,
                                  loading: false,
                                }
                              : x,
                          ),
                        );
                      } catch (e) {
                        dlog("err", {
                          event: "advanced_reanalyze",
                          path: t.path,
                          err: String(e),
                        });
                        setTracks((prev) =>
                          prev.map((x) =>
                            x.path === t.path
                              ? { ...x, loading: false }
                              : x,
                          ),
                        );
                      }
                    }),
                  );
                  setTracks((prev) => {
                    void applyUpsPolicy(prev);
                    return prev;
                  });
                })();
              }}
            />
          </div>
        </div>
      ) : null}
    </div>
  );
}

export default App;
