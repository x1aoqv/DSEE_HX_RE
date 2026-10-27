mod debug_log;

use dsee_kern::{
    analyze_track, clear_prepared, default_output_path, default_slots,
    detect_media_tools_in_app_dir, drop_prepared, file_resample_rate, media_tools_info,
    plan_paths_rates, process_batch, set_advanced_dsp, set_ffmpeg_override, set_ffprobe_override,
    MediaToolsInfo, TrackAnalysis, TrackHopProgress,
};
use serde::Deserialize;
use serde::Serialize;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Instant;
use tauri::image::Image;
use tauri::{AppHandle, Emitter, Manager};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct QueueTrack {
    path: String,
    output_rate: i32,
    output_bits: String,
    slot_cost: Option<u32>,
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct ProcessProgress {
    stage: String,
    percent: u32,
    message: String,
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct TrackProgress {
    input: String,
    hops_done: usize,
    hops_total: usize,
    percent: u32,
    failed: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct BatchResult {
    ok: usize,
    fail: usize,
    slots: usize,
    peak_tracks: usize,
    jobs: usize,
    wall_secs: f64,
    outputs: Vec<String>,
    errors: Vec<String>,
}

#[tauri::command]
fn debug_log_append(line: String) {
    debug_log::append(&line);
}

#[tauri::command]
fn debug_log_read() -> Result<String, String> {
    debug_log::read_all()
}

#[tauri::command]
fn debug_log_path() -> Option<String> {
    debug_log::path_display()
}

#[tauri::command]
fn debug_log_export(path: String) -> Result<(), String> {
    debug_log::export_to(PathBuf::from(path).as_path())
}

#[tauri::command]
async fn analyze_queue_track(
    path: String,
    up_factor: Option<i32>,
    output_bits: String,
    level: Option<i32>,
) -> Result<TrackAnalysis, String> {
    let up_s = up_factor
        .map(|u| u.to_string())
        .unwrap_or_else(|| "auto".into());
    let lv_s = level
        .map(|l| l.to_string())
        .unwrap_or_else(|| "-".into());
    debug_log::evt(
        "analyze",
        &format!("path={path} up={up_s} bits={output_bits} level={lv_s}"),
    );
    let result = tauri::async_runtime::spawn_blocking(move || {
        let p = PathBuf::from(&path);
        analyze_track(&p, up_factor, &output_bits, level).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| format!("worker join: {e}"))?;
    match &result {
        Ok(a) => {
            let err = a.error.as_deref().unwrap_or("-");
            let allowed = a
                .allowed_ups
                .iter()
                .map(|u| u.to_string())
                .collect::<Vec<_>>()
                .join(",");
            debug_log::evt(
                "analyze",
                &format!(
                    "ok path={} active={} processable={} in_fs={} out_fs={} up={} bits={} level={} same={} allowed=[{}] slots={} err={}",
                    a.path,
                    a.active,
                    a.processable,
                    a.input_rate,
                    a.output_rate,
                    a.up_factor,
                    a.output_bits,
                    a.level,
                    a.same_rate_repair,
                    allowed,
                    a.slot_cost,
                    err
                ),
            );
        }
        Err(e) => debug_log::evt("err", &format!("analyze {e}")),
    }
    result
}

#[tauri::command]
fn drop_queue_track(path: String) {
    debug_log::evt("ingest", &format!("drop path={path}"));
    drop_prepared(PathBuf::from(path).as_path());
}

#[tauri::command]
fn clear_queue_prep() {
    debug_log::evt("ingest", "clear");
    clear_prepared();
}

#[tauri::command]
fn get_media_tools() -> MediaToolsInfo {
    media_tools_info()
}

#[tauri::command]
fn set_media_tools(ffmpeg: Option<String>, ffprobe: Option<String>) -> MediaToolsInfo {
    set_ffmpeg_override(ffmpeg.filter(|s| !s.trim().is_empty()).map(PathBuf::from));
    set_ffprobe_override(ffprobe.filter(|s| !s.trim().is_empty()).map(PathBuf::from));
    let info = media_tools_info();
    debug_log::evt(
        "media",
        &format!(
            "ffmpeg={} ok={} ffprobe={} ok={}",
            info.ffmpeg.as_deref().unwrap_or("-"),
            info.ffmpeg_ok,
            info.ffprobe.as_deref().unwrap_or("-"),
            info.ffprobe_ok
        ),
    );
    info
}

/// Scan app folder + env vars + system PATH; persist results via overrides.
#[tauri::command]
fn detect_media_tools() -> MediaToolsInfo {
    let info = detect_media_tools_in_app_dir();
    debug_log::evt(
        "media",
        &format!(
            "detect ffmpeg={} ok={} ffprobe={} ok={}",
            info.ffmpeg.as_deref().unwrap_or("-"),
            info.ffmpeg_ok,
            info.ffprobe.as_deref().unwrap_or("-"),
            info.ffprobe_ok
        ),
    );
    info
}

/// advanced=false → 恢复原版自动；true 时 heq / f80 / fileRate 强制生效（level 仍经 analyze）
#[tauri::command]
fn set_advanced_overrides(advanced: bool, heq: bool, f80: i32, file_rate: i32) {
    if advanced {
        let f80o = if matches!(f80, 1 | 2 | 4) {
            Some(f80)
        } else {
            None
        };
        set_advanced_dsp(heq, f80o, file_rate);
        debug_log::evt(
            "settings",
            &format!("advanced=1 heq={heq} f80={f80} fileRate={file_rate}"),
        );
    } else {
        set_advanced_dsp(true, None, 0);
        debug_log::evt("settings", "advanced=0");
    }
}

#[tauri::command]
async fn process_queue(
    app: AppHandle,
    tracks: Vec<QueueTrack>,
    output_dir: String,
) -> Result<BatchResult, String> {
    debug_log::evt(
        "run",
        &format!("start n={} out={output_dir}", tracks.len()),
    );
    for t in &tracks {
        debug_log::evt(
            "run",
            &format!(
                "track path={} rate={} bits={} slots={}",
                t.path,
                t.output_rate,
                t.output_bits,
                t.slot_cost.unwrap_or(0)
            ),
        );
    }
    let result = tauri::async_runtime::spawn_blocking(move || {
        let out_dir = PathBuf::from(&output_dir);
        std::fs::create_dir_all(&out_dir).map_err(|e| e.to_string())?;
        let paths: Vec<PathBuf> = tracks.iter().map(|t| PathBuf::from(&t.path)).collect();
        let rates: Vec<i32> = tracks.iter().map(|t| t.output_rate).collect();
        // Bits may differ per track — plan with first, then stamp each item.
        let default_bits = tracks
            .first()
            .map(|t| t.output_bits.clone())
            .unwrap_or_else(|| "24".into());
        let mut items =
            plan_paths_rates(&paths, &rates, &out_dir, &default_bits).map_err(|e| e.to_string())?;
        let file_rate = file_resample_rate();
        for (it, t) in items.iter_mut().zip(tracks.iter()) {
            it.output_bits = t.output_bits.clone();
            it.output_rate = t.output_rate;
            it.slot_cost = t.slot_cost.unwrap_or(0) as usize;
            // Extension / lossy→WAV gate uses final file rate when set.
            let export_rate = if file_rate > 0 {
                file_rate
            } else {
                t.output_rate
            };
            it.output = default_output_path(&it.input, &out_dir, export_rate);
        }
        let slots = default_slots();
        let app2 = app.clone();
        let app_hop = app.clone();
        let t0 = Instant::now();
        let last = Arc::new(Mutex::new(0usize));
        let total = items.len();
        debug_log::evt("run", &format!("batch total={total} slots={slots}"));
        let _ = app2.emit(
            "process-progress",
            ProcessProgress {
                stage: "batch".into(),
                percent: 0,
                message: format!("开始 0/{total}（{slots} 槽）"),
            },
        );
        let report = process_batch(
            &items,
            Some(slots),
            Some(|done: usize, tot: usize| {
                let mut g = last.lock().unwrap();
                if *g != done {
                    *g = done;
                    let pct = if tot > 0 {
                        ((done as u64 * 100) / tot as u64) as u32
                    } else {
                        0
                    };
                    debug_log::evt("run", &format!("progress done={done} total={tot} pct={pct}"));
                    let _ = app2.emit(
                        "process-progress",
                        ProcessProgress {
                            stage: "batch".into(),
                            percent: pct.min(99),
                            message: format!("曲目 {done}/{tot}"),
                        },
                    );
                }
            }),
            Some(|p: TrackHopProgress| {
                let percent = if p.failed {
                    0
                } else if p.hops_total > 0 {
                    ((p.hops_done as u64 * 100) / p.hops_total as u64).min(100) as u32
                } else {
                    0
                };
                // 仅失败或整轨完成时记 hop，避免刷屏
                if p.failed || (p.hops_total > 0 && p.hops_done >= p.hops_total) {
                    debug_log::evt(
                        "hop",
                        &format!(
                            "path={} done={}/{} fail={} pct={}",
                            p.input.display(),
                            p.hops_done,
                            p.hops_total,
                            p.failed,
                            percent
                        ),
                    );
                }
                let _ = app_hop.emit(
                    "track-progress",
                    TrackProgress {
                        input: p.input.display().to_string(),
                        hops_done: p.hops_done,
                        hops_total: p.hops_total,
                        percent,
                        failed: p.failed,
                    },
                );
            }),
        );
        let wall = t0.elapsed().as_secs_f64();
        debug_log::evt(
            "run",
            &format!(
                "done ok={} fail={} wall_s={wall:.3} slots={} peak={} jobs={}",
                report.ok.len(),
                report.errors.len(),
                report.slots,
                report.peak_tracks,
                report.jobs
            ),
        );
        for (p, e) in &report.errors {
            debug_log::evt("err", &format!("run path={} err={e}", p.display()));
        }
        Ok(BatchResult {
            ok: report.ok.len(),
            fail: report.errors.len(),
            slots: report.slots,
            peak_tracks: report.peak_tracks,
            jobs: report.jobs,
            wall_secs: wall,
            outputs: report
                .ok
                .iter()
                .map(|r| r.output.display().to_string())
                .collect(),
            errors: report
                .errors
                .iter()
                .map(|(p, e)| format!("{}: {e}", p.display()))
                .collect(),
        })
    })
    .await
    .map_err(|e| format!("worker join: {e}"))?;
    if let Err(ref e) = result {
        debug_log::evt("err", &format!("run {e}"));
    }
    result
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            let log_dir = app.path().app_log_dir()?;
            debug_log::init(log_dir);
            // MinGW 链接常丢 .rsrc 图标；运行时强制写入任务栏/窗口图标
            if let Some(win) = app.get_webview_window("main") {
                let icon = Image::from_bytes(include_bytes!("../icons/icon.png"))
                    .map_err(|e| format!("load window icon: {e}"))?;
                win.set_icon(icon)?;
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            process_queue,
            analyze_queue_track,
            drop_queue_track,
            clear_queue_prep,
            get_media_tools,
            set_media_tools,
            detect_media_tools,
            set_advanced_overrides,
            debug_log_append,
            debug_log_read,
            debug_log_path,
            debug_log_export
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
