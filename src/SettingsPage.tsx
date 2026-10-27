import { useCallback, useEffect, useState } from "react";
import { getVersion } from "@tauri-apps/api/app";
import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";
import { openUrl } from "@tauri-apps/plugin-opener";
import { dlog, exportDebugLog } from "./debugLog";
import {
  loadDialogMemory,
  resolveDialogStart,
  saveDialogMemory,
} from "./dialogMemory";
import type { Dict, Locale } from "./i18n";
import RoundSelect from "./RoundSelect";
import {
  ADV_RESAMPLE_RATES,
  type AppSettings,
  type BaseColorId,
} from "./settings";
import { BASE_COLORS, getBaseColor, saveSettings } from "./settings";

const REPO_URL = "https://github.com/x1aoqv/DSEE_HX_RE";
const MAIL_AUTHOR = "mailto:Le_Fan_Qv@outlook.com";
const MAIL_THANKS = "mailto:1250335352@qq.com";

function GitHubIcon() {
  return (
    <svg
      className="settings-gh-icon"
      viewBox="0 0 16 16"
      width="16"
      height="16"
      aria-hidden
    >
      <path
        fill="currentColor"
        d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8"
      />
    </svg>
  );
}

const BASE_LABEL_KEY = {
  system: "hueSystem",
  black: "hueBlack",
  white: "hueWhite",
  red: "hueRed",
  orange: "hueOrange",
  yellow: "hueYellow",
  green: "hueGreen",
  blue: "hueBlue",
  purple: "huePurple",
} as const;

type MediaToolsInfo = {
  ffmpeg: string | null;
  ffmpegOk: boolean;
  ffprobe: string | null;
  ffprobeOk: boolean;
};

type Props = {
  dict: Dict;
  settings: AppSettings;
  onChange: (next: AppSettings) => void;
  /** 高级参数变更后通知主界面重分析（传入刚保存的设置） */
  onAdvancedApplied?: (next: AppSettings) => void;
};

export default function SettingsPage({
  dict: d,
  settings,
  onChange,
  onAdvancedApplied,
}: Props) {
  const [tools, setTools] = useState<MediaToolsInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [exportBusy, setExportBusy] = useState(false);
  const [exportMsg, setExportMsg] = useState("");
  const [appVersion, setAppVersion] = useState("1.0.0");

  useEffect(() => {
    void getVersion()
      .then(setAppVersion)
      .catch(() => setAppVersion("1.0.0"));
  }, []);

  const openExternal = useCallback((url: string, event: string) => {
    void openUrl(url).catch((e) =>
      dlog("err", { event, err: String(e) }),
    );
  }, []);

  const refreshTools = useCallback(async () => {
    try {
      const info = await invoke<MediaToolsInfo>("get_media_tools");
      setTools(info);
    } catch {
      setTools(null);
    }
  }, []);

  const applyTools = useCallback(
    async (ffmpegPath: string, ffprobePath: string) => {
      setBusy(true);
      try {
        const info = await invoke<MediaToolsInfo>("set_media_tools", {
          ffmpeg: ffmpegPath.trim() || null,
          ffprobe: ffprobePath.trim() || null,
        });
        setTools(info);
      } catch {
        await refreshTools();
      } finally {
        setBusy(false);
      }
    },
    [refreshTools],
  );

  const pushAdvanced = useCallback(
    async (s: AppSettings) => {
      try {
        await invoke("set_advanced_overrides", {
          advanced: s.advanced,
          heq: s.advHeq,
          f80: 0,
          fileRate: s.advanced ? s.advResampleRate : 0,
        });
        dlog("settings", {
          advanced: s.advanced,
          heq: s.advHeq,
          fileRate: s.advanced ? s.advResampleRate : 0,
        });
      } catch (e) {
        dlog("err", { event: "advanced", err: String(e) });
      }
    },
    [],
  );

  useEffect(() => {
    // Prefer saved paths; first-boot detect is owned by App.
    if (settings.mediaToolsBootstrapped) {
      void applyTools(settings.ffmpegPath, settings.ffprobePath);
    } else {
      void refreshTools();
    }
    void pushAdvanced(settings);
  }, []); // eslint-disable-line react-hooks/exhaustive-deps -- mount sync only

  function patch(partial: Partial<AppSettings>) {
    const next = { ...settings, ...partial };
    saveSettings(next);
    onChange(next);
    return next;
  }

  async function runAutoDetect() {
    setBusy(true);
    dlog("media", { auto: "both" });
    try {
      const info = await invoke<MediaToolsInfo>("detect_media_tools");
      const next = patch({
        ffmpegPath: info.ffmpeg ?? "",
        ffprobePath: info.ffprobe ?? "",
        mediaToolsBootstrapped: true,
      });
      setTools(info);
      dlog("media", {
        detected: true,
        ffmpeg: next.ffmpegPath || "-",
        ffprobe: next.ffprobePath || "-",
      });
    } catch {
      await refreshTools();
    } finally {
      setBusy(false);
    }
  }

  async function patchAdvanced(partial: Partial<AppSettings>) {
    const next = patch(partial);
    await pushAdvanced(next);
    onAdvancedApplied?.(next);
  }

  async function pickExe(kind: "ffmpeg" | "ffprobe") {
    const mem = loadDialogMemory();
    const remembered =
      kind === "ffmpeg" ? mem.ffmpegPath : mem.ffprobePath;
    const resolved =
      kind === "ffmpeg" ? tools?.ffmpeg : tools?.ffprobe;
    const defaultPath = await resolveDialogStart(
      remembered,
      resolved,
    );
    const picked = await open({
      multiple: false,
      ...(defaultPath ? { defaultPath } : {}),
      filters: [
        { name: kind, extensions: ["exe"] },
        { name: "All", extensions: ["*"] },
      ],
    });
    if (typeof picked !== "string") return;
    dlog("media", { pick: kind, path: picked });
    if (kind === "ffmpeg") {
      saveDialogMemory({ ffmpegPath: picked });
      const next = patch({ ffmpegPath: picked });
      await applyTools(next.ffmpegPath, next.ffprobePath);
    } else {
      saveDialogMemory({ ffprobePath: picked });
      const next = patch({ ffprobePath: picked });
      await applyTools(next.ffmpegPath, next.ffprobePath);
    }
  }

  async function clearPath(_kind: "ffmpeg" | "ffprobe") {
    // Both buttons re-run full detect (app dir → env → PATH).
    await runAutoDetect();
  }

  async function onPathBlur(kind: "ffmpeg" | "ffprobe", value: string) {
    dlog("media", { edit: kind, path: value.trim() || "-" });
    if (kind === "ffmpeg") {
      const next = patch({ ffmpegPath: value });
      await applyTools(next.ffmpegPath, next.ffprobePath);
    } else {
      const next = patch({ ffprobePath: value });
      await applyTools(next.ffmpegPath, next.ffprobePath);
    }
  }

  function setLanguage(language: Locale) {
    dlog("settings", { lang: language });
    patch({ language });
  }

  function setBaseColor(baseColor: BaseColorId) {
    dlog("settings", { base: baseColor });
    const preset = getBaseColor(baseColor);
    if ("theme" in preset && preset.theme) {
      patch({ baseColor, theme: preset.theme });
      return;
    }
    patch({ baseColor });
  }

  async function onExportLog() {
    setExportBusy(true);
    setExportMsg("");
    try {
      const path = await exportDebugLog();
      setExportMsg(path ?? "");
    } catch (e) {
      dlog("err", { event: "export", err: String(e) });
      setExportMsg(String(e));
    } finally {
      setExportBusy(false);
    }
  }

  return (
    <div className="settings-page">
      <header className="settings-bar">
        <h1 className="settings-title">{d.settingsTitle}</h1>
      </header>

      <div className="settings-body">
        <section className="settings-section">
          <span className="settings-label" id="settings-lang-label">
            {d.language}
          </span>
          <RoundSelect
            className="settings-control"
            value={settings.language}
            aria-label={d.language}
            onChange={(v) => setLanguage(v as Locale)}
            options={[
              { value: "zh", label: d.langZh },
              { value: "en", label: d.langEn },
            ]}
          />
        </section>

        <section className="settings-section">
          <span className="settings-label" id="settings-theme-label">
            {d.theme}
          </span>
          <div
            className="hue-swatches"
            role="radiogroup"
            aria-labelledby="settings-theme-label"
          >
            {BASE_COLORS.map((p) => {
              const active = settings.baseColor === p.id;
              const isSystem = p.id === "system";
              return (
                <button
                  key={p.id}
                  type="button"
                  role="radio"
                  aria-checked={active}
                  className={[
                    "hue-swatch",
                    active ? "is-active" : "",
                    p.id === "white" ? "is-light" : "",
                    isSystem ? "is-system" : "",
                  ]
                    .filter(Boolean)
                    .join(" ")}
                  title={d[BASE_LABEL_KEY[p.id]]}
                  style={isSystem ? undefined : { background: p.swatch }}
                  onClick={() => setBaseColor(p.id)}
                />
              );
            })}
          </div>
        </section>

        <section className="settings-section">
          <label className="settings-label" htmlFor="settings-ffmpeg">
            {d.ffmpeg}
          </label>
          <div className="settings-path-row">
            <input
              id="settings-ffmpeg"
              className="settings-control"
              value={settings.ffmpegPath}
              placeholder={d.pathPlaceholder}
              disabled={busy}
              onChange={(e) =>
                onChange({ ...settings, ffmpegPath: e.target.value })
              }
              onBlur={(e) => void onPathBlur("ffmpeg", e.target.value)}
            />
            <button
              type="button"
              disabled={busy}
              onClick={() => void pickExe("ffmpeg")}
            >
              {d.browse}
            </button>
            <button
              type="button"
              disabled={busy}
              onClick={() => void clearPath("ffmpeg")}
            >
              {d.autoDetect}
            </button>
          </div>
          <p className={`settings-status${tools?.ffmpegOk ? " is-ok" : " is-bad"}`}>
            {d.resolved}: {tools?.ffmpeg ?? "—"} ·{" "}
            {tools?.ffmpegOk ? d.ok : d.missing}
          </p>
        </section>

        <section className="settings-section">
          <label className="settings-label" htmlFor="settings-ffprobe">
            {d.ffprobe}
          </label>
          <div className="settings-path-row">
            <input
              id="settings-ffprobe"
              className="settings-control"
              value={settings.ffprobePath}
              placeholder={d.pathPlaceholder}
              disabled={busy}
              onChange={(e) =>
                onChange({ ...settings, ffprobePath: e.target.value })
              }
              onBlur={(e) => void onPathBlur("ffprobe", e.target.value)}
            />
            <button
              type="button"
              disabled={busy}
              onClick={() => void pickExe("ffprobe")}
            >
              {d.browse}
            </button>
            <button
              type="button"
              disabled={busy}
              onClick={() => void clearPath("ffprobe")}
            >
              {d.autoDetect}
            </button>
          </div>
          <p
            className={`settings-status${tools?.ffprobeOk ? " is-ok" : " is-bad"}`}
          >
            {d.resolved}: {tools?.ffprobe ?? "—"} ·{" "}
            {tools?.ffprobeOk ? d.ok : d.missing}
          </p>
        </section>

        <section className="settings-section">
          <div className="settings-advanced-head">
            <span className="settings-label" id="settings-adv-label">
              {d.advanced}
            </span>
            <label className="settings-toggle">
              <input
                type="checkbox"
                checked={settings.advanced}
                disabled={busy}
                aria-labelledby="settings-adv-label"
                onChange={(e) =>
                  void patchAdvanced({ advanced: e.target.checked })
                }
              />
            </label>
          </div>
          <p className="settings-warn">{d.advancedWarn}</p>

          {settings.advanced ? (
            <div className="settings-advanced-body">
              <div className="settings-adv-row">
                <span className="settings-label">{d.advHeq}</span>
                <RoundSelect
                  className="settings-control"
                  value={settings.advHeq ? "1" : "0"}
                  disabled={busy}
                  aria-label={d.advHeq}
                  onChange={(v) =>
                    void patchAdvanced({ advHeq: v === "1" })
                  }
                  options={[
                    { value: "1", label: d.advHeqOn },
                    { value: "0", label: d.advHeqOff },
                  ]}
                />
              </div>
              <div className="settings-adv-row">
                <span className="settings-label">{d.advResampleRate}</span>
                <RoundSelect
                  className="settings-control"
                  value={String(settings.advResampleRate)}
                  disabled={busy}
                  aria-label={d.advResampleRate}
                  onChange={(v) =>
                    void patchAdvanced({
                      advResampleRate: Number(v) || 0,
                    })
                  }
                  options={ADV_RESAMPLE_RATES.map((hz) => ({
                    value: String(hz),
                    label:
                      hz === 0
                        ? d.advResampleFollow
                        : hz >= 1000
                          ? `${(hz / 1000).toFixed(hz % 1000 === 0 ? 0 : 1)} kHz`
                          : `${hz} Hz`,
                  }))}
                />
              </div>
              <p className="settings-hint">{d.advResampleHint}</p>
            </div>
          ) : null}
        </section>

        <section className="settings-section settings-about">
          <span className="settings-label">{d.about}</span>
          <div className="settings-about-meta">
            <button
              type="button"
              className="settings-gh-btn"
              title={d.aboutRepo}
              aria-label={d.aboutRepo}
              onClick={() => openExternal(REPO_URL, "open_repo")}
            >
              <GitHubIcon />
            </button>
            <span
              className="settings-about-ver"
              title={`${d.aboutVersion} ${appVersion}`}
            >
              v{appVersion}
            </span>
          </div>
          <p className="settings-about-line">
            <span className="settings-muted">{d.aboutBy}</span>
            <button
              type="button"
              className="settings-link"
              onClick={() => openExternal(MAIL_AUTHOR, "mail_author")}
            >
              {d.aboutAuthor}
            </button>
          </p>
          <p className="settings-about-line">
            <span className="settings-muted">{d.aboutThanks}</span>
            <button
              type="button"
              className="settings-link"
              onClick={() => openExternal(MAIL_THANKS, "mail_thanks")}
            >
              {d.aboutThanksName}
            </button>
          </p>
        </section>
      </div>

      <footer className="settings-footer">
        <button
          type="button"
          className="settings-export"
          disabled={exportBusy}
          onClick={() => void onExportLog()}
        >
          {d.exportLog}
        </button>
        {exportMsg ? (
          <p className="settings-export-msg" title={exportMsg}>
            {exportMsg}
          </p>
        ) : null}
      </footer>
    </div>
  );
}
