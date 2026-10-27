import type { Locale } from "./i18n";

const KEY = "dsee-hx-settings";

export type ThemeMode = "dark" | "light";

/** 基础色：系统主题色 + 黑白 + 红橙黄绿蓝紫 */
export const BASE_COLORS = [
  { id: "system", hue: 210, sat: 1, swatch: "" },
  { id: "black", hue: 0, sat: 0, theme: "dark" as const, swatch: "#1a1a1a" },
  { id: "white", hue: 0, sat: 0, theme: "light" as const, swatch: "#f2f2f2" },
  { id: "red", hue: 0, sat: 1, swatch: "#c62828" },
  { id: "orange", hue: 28, sat: 1, swatch: "#ef6c00" },
  { id: "yellow", hue: 48, sat: 1, swatch: "#f9a825" },
  { id: "green", hue: 140, sat: 1, swatch: "#2e7d32" },
  { id: "blue", hue: 220, sat: 1, swatch: "#1565c0" },
  { id: "purple", hue: 285, sat: 1, swatch: "#6a1b9a" },
] as const;

export type BaseColorId = (typeof BASE_COLORS)[number]["id"];

export type AppSettings = {
  language: Locale;
  theme: ThemeMode;
  baseColor: BaseColorId;
  ffmpegPath: string;
  ffprobePath: string;
  /** 是否已做过首次自动检测；之后只在设置里点「自动检测」才再扫 */
  mediaToolsBootstrapped: boolean;
  /** 高级设置总开关；关闭时 DSP 与原版一致，打开后解锁主表 Level / 1× */
  advanced: boolean;
  /** HEQ 前置整形；false = 绕过 */
  advHeq: boolean;
  /**
   * 导出文件目标采样率（Hz）。0 = 跟随 DSEE 输出（cfg.dst）；
   * 非 0 时 DSEE 仍按分析倍率跑完，再 soxr 重采样到该率写入文件。
   */
  advOutputRate: number;
  /** 导出位深（高级开启时生效） */
  advOutputBits: "16" | "24" | "32";
  /** 导出编码（高级开启时生效） */
  advOutputCodec: AdvOutputCodec;
};

export type AdvOutputCodec = "pcm" | "flac" | "alac";

/** 高级「输出采样率」可选值（Hz）；0 = 跟随 DSEE */
export const ADV_OUTPUT_RATES = [
  0, 44100, 48000, 88200, 96000, 176400, 192000,
] as const;

export const ADV_OUTPUT_BITS = ["16", "24", "32"] as const;
export const ADV_OUTPUT_CODECS: AdvOutputCodec[] = ["pcm", "flac", "alac"];

/** 首次启动跟系统语言：非中文环境默认英文 */
export function detectLocale(): Locale {
  try {
    const lang = (
      navigator.language ||
      (navigator as Navigator & { userLanguage?: string }).userLanguage ||
      ""
    ).toLowerCase();
    return lang.startsWith("zh") ? "zh" : "en";
  } catch {
    return "en";
  }
}

export const DEFAULT_SETTINGS: AppSettings = {
  language: "en",
  theme: "dark",
  baseColor: "black",
  ffmpegPath: "",
  ffprobePath: "",
  mediaToolsBootstrapped: false,
  advanced: false,
  advHeq: true,
  advOutputRate: 0,
  advOutputBits: "32",
  advOutputCodec: "flac",
};

function isBaseColor(v: unknown): v is BaseColorId {
  return BASE_COLORS.some((c) => c.id === v);
}

export function getBaseColor(id: BaseColorId) {
  return BASE_COLORS.find((c) => c.id === id) ?? BASE_COLORS[1];
}

/** 读取系统主题色（CSS AccentColor → 色相） */
export function readSystemAccent(): { hue: number; sat: number } {
  try {
    const el = document.createElement("div");
    el.style.cssText =
      "position:fixed;left:-9999px;top:0;pointer-events:none;color:AccentColor";
    document.documentElement.appendChild(el);
    const raw = getComputedStyle(el).color;
    el.remove();
    const m = raw.match(/rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)/i);
    if (!m) return { hue: 210, sat: 1 };
    const r = Number(m[1]) / 255;
    const g = Number(m[2]) / 255;
    const b = Number(m[3]) / 255;
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const d = max - min;
    const l = (max + min) / 2;
    let h = 0;
    let s = 0;
    if (d > 1e-6) {
      s = d / (1 - Math.abs(2 * l - 1));
      switch (max) {
        case r:
          h = ((g - b) / d + (g < b ? 6 : 0)) * 60;
          break;
        case g:
          h = ((b - r) / d + 2) * 60;
          break;
        default:
          h = ((r - g) / d + 4) * 60;
          break;
      }
    }
    return {
      hue: Math.round(((h % 360) + 360) % 360),
      sat: s < 0.08 ? 0 : 1,
    };
  } catch {
    return { hue: 210, sat: 1 };
  }
}

export function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) {
      return { ...DEFAULT_SETTINGS, language: detectLocale() };
    }
    const parsed = JSON.parse(raw) as Partial<AppSettings>;
    const baseColor = isBaseColor(parsed.baseColor)
      ? parsed.baseColor
      : DEFAULT_SETTINGS.baseColor;
    const preset = getBaseColor(baseColor);
    let theme: ThemeMode = parsed.theme === "light" ? "light" : "dark";
    if ("theme" in preset && preset.theme) theme = preset.theme;
    return {
      language:
        parsed.language === "zh"
          ? "zh"
          : parsed.language === "en"
            ? "en"
            : detectLocale(),
      theme,
      baseColor,
      ffmpegPath: typeof parsed.ffmpegPath === "string" ? parsed.ffmpegPath : "",
      ffprobePath:
        typeof parsed.ffprobePath === "string" ? parsed.ffprobePath : "",
      // Existing installs with saved paths count as already bootstrapped.
      mediaToolsBootstrapped:
        parsed.mediaToolsBootstrapped === true ||
        (typeof parsed.ffmpegPath === "string" && parsed.ffmpegPath.trim() !== "") ||
        (typeof parsed.ffprobePath === "string" && parsed.ffprobePath.trim() !== ""),
      advanced: parsed.advanced === true,
      advHeq: parsed.advHeq !== false,
      advOutputRate: (() => {
        const legacy = (parsed as { advResampleRate?: number }).advResampleRate;
        const rate = parsed.advOutputRate ?? legacy ?? 0;
        return ADV_OUTPUT_RATES.includes(rate as (typeof ADV_OUTPUT_RATES)[number])
          ? rate
          : 0;
      })(),
      advOutputBits: ADV_OUTPUT_BITS.includes(
        parsed.advOutputBits as (typeof ADV_OUTPUT_BITS)[number],
      )
        ? (parsed.advOutputBits as "16" | "24" | "32")
        : "32",
      advOutputCodec: ADV_OUTPUT_CODECS.includes(
        parsed.advOutputCodec as AdvOutputCodec,
      )
        ? (parsed.advOutputCodec as AdvOutputCodec)
        : "flac",
    };
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

export function saveSettings(s: AppSettings) {
  localStorage.setItem(KEY, JSON.stringify(s));
}

/** 把主题写到 documentElement，供 CSS 变量模板使用 */
export function applyTheme(s: Pick<AppSettings, "theme" | "baseColor">) {
  const root = document.documentElement;
  const preset = getBaseColor(s.baseColor);
  const theme =
    "theme" in preset && preset.theme ? preset.theme : s.theme;
  root.setAttribute("data-theme", theme);
  if (preset.id === "system") {
    const accent = readSystemAccent();
    root.style.setProperty("--hue", String(accent.hue));
    root.style.setProperty("--sat", String(accent.sat));
    root.setAttribute("data-base", "system");
  } else {
    root.style.setProperty("--hue", String(preset.hue));
    root.style.setProperty("--sat", String(preset.sat));
    root.removeAttribute("data-base");
  }
}
