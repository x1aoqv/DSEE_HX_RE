//! Multi-track batch with CPU-slot admission (look-ahead fill).
//!
//! Stereo F76-only costs 1 slot; any path with F80 costs 2 (OpenMP L/R).
//! When a free slot cannot start the head-of-queue job, scan forward for a
//! cheaper job that fits — do not idle-wait on a 2-slot head.

use crate::error::{DseeError, Result};
use crate::prepare::{ensure_prepared, process_file_cached};
use crate::process::{ProcessReport, ProcessRequest};
use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::thread;

const AUDIO_EXTS: &[&str] = &[
    "mp3", "m4a", "flac", "wav", "aac", "ogg", "wma", "aiff", "ape", "alac",
];

#[derive(Clone)]
pub struct BatchItem {
    pub input: PathBuf,
    pub output: PathBuf,
    pub output_rate: i32,
    pub output_bits: String,
    /// From import analyze; F80 → 2, else 1. 0 = compute via ensure_prepared.
    pub slot_cost: usize,
}

#[derive(Clone)]
pub struct BatchItemReport {
    pub input: PathBuf,
    pub output: PathBuf,
    pub report: ProcessReport,
}

pub struct BatchReport {
    pub ok: Vec<BatchItemReport>,
    pub errors: Vec<(PathBuf, String)>,
    /// Logical CPU slot budget used for admission.
    pub slots: usize,
    /// Peak number of tracks running at once.
    pub peak_tracks: usize,
    /// Alias of [`Self::peak_tracks`] (legacy BENCH_JSON / UI field).
    pub jobs: usize,
}

/// Per-track hop progress during batch.
#[derive(Clone, Debug)]
pub struct TrackHopProgress {
    pub input: PathBuf,
    pub hops_done: usize,
    pub hops_total: usize,
    pub failed: bool,
}

/// Default slot budget = logical CPU count.
pub fn default_slots() -> usize {
    thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(2)
        .max(1)
}

/// Deprecated alias: old “half CPUs, max 4” worker default.
pub fn default_jobs() -> usize {
    (default_slots() / 2).clamp(1, 4)
}

pub fn is_batch_source(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|s| s.to_str()) else {
        return false;
    };
    if name.starts_with('.') {
        return false;
    }
    let lower = name.to_ascii_lowercase();
    // Skip our own exports (`track_dsee.flac`, legacy `*_dsee_88200.wav`).
    if lower.contains("_dsee.") || lower.contains("_dsee_") || lower.ends_with("_dsee.wav") {
        return false;
    }
    let Some(ext) = path.extension().and_then(|s| s.to_str()) else {
        return false;
    };
    let ext = ext.to_ascii_lowercase();
    AUDIO_EXTS.iter().any(|e| *e == ext)
}

/// Non-recursive listing of processable audio in `dir`, sorted by name.
pub fn list_audio_files(dir: &Path) -> Result<Vec<PathBuf>> {
    if !dir.is_dir() {
        return Err(DseeError::Msg(format!(
            "not a directory: {}",
            dir.display()
        )));
    }
    let mut out = Vec::new();
    for ent in std::fs::read_dir(dir).map_err(|e| DseeError::Msg(e.to_string()))? {
        let ent = ent.map_err(|e| DseeError::Msg(e.to_string()))?;
        let p = ent.path();
        if p.is_file() && is_batch_source(&p) {
            out.push(p);
        }
    }
    out.sort();
    Ok(out)
}

pub use crate::export::default_output_path;

pub fn plan_dir(in_dir: &Path, out_dir: &Path, rate: i32) -> Result<Vec<BatchItem>> {
    plan_dir_repeat(in_dir, out_dir, rate, 1)
}

/// Like [`plan_dir`], but enqueue each source `repeat` times with unique outputs.
pub fn plan_dir_repeat(
    in_dir: &Path,
    out_dir: &Path,
    rate: i32,
    repeat: usize,
) -> Result<Vec<BatchItem>> {
    let files = list_audio_files(in_dir)?;
    if files.is_empty() {
        return Err(DseeError::Msg(format!(
            "no audio sources in {}",
            in_dir.display()
        )));
    }
    let repeat = repeat.max(1);
    let mut items = Vec::with_capacity(files.len() * repeat);
    for input in &files {
        let base = default_output_path(input, out_dir, rate);
        let stem = base
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("out");
        let ext = base
            .extension()
            .and_then(|s| s.to_str())
            .unwrap_or("flac");
        for r in 0..repeat {
            let dest = if repeat == 1 {
                base.clone()
            } else {
                out_dir.join(format!("{stem}_r{r:02}.{ext}"))
            };
            items.push(BatchItem {
                input: input.clone(),
                output: dest,
                output_rate: rate,
                output_bits: "24".into(),
                slot_cost: 0,
            });
        }
    }
    Ok(items)
}

/// Build batch items from an explicit file list (keeps caller order; skips junk).
pub fn plan_paths(paths: &[PathBuf], out_dir: &Path, rate: i32) -> Result<Vec<BatchItem>> {
    let rates: Vec<i32> = paths.iter().map(|_| rate).collect();
    plan_paths_rates(paths, &rates, out_dir, "24")
}

/// Like [`plan_paths`], but each path may have its own device/output rate.
pub fn plan_paths_rates(
    paths: &[PathBuf],
    rates: &[i32],
    out_dir: &Path,
    bits: &str,
) -> Result<Vec<BatchItem>> {
    if paths.len() != rates.len() {
        return Err(DseeError::Msg(format!(
            "paths/rates length mismatch: {} vs {}",
            paths.len(),
            rates.len()
        )));
    }
    let mut items = Vec::new();
    for (input, &rate) in paths.iter().zip(rates.iter()) {
        if !input.is_file() {
            return Err(DseeError::Msg(format!("not a file: {}", input.display())));
        }
        if !is_batch_source(input) {
            continue;
        }
        items.push(BatchItem {
            output: default_output_path(input, out_dir, rate),
            input: input.clone(),
            output_rate: rate,
            output_bits: bits.to_string(),
            slot_cost: 0,
        });
    }
    if items.is_empty() {
        return Err(DseeError::Msg("queue is empty (no processable audio)".into()));
    }
    Ok(items)
}

struct Planned {
    item: BatchItem,
    cost: usize,
}

enum BatchEvent {
    Hop(TrackHopProgress),
    Done {
        item: BatchItem,
        cost: usize,
        result: std::result::Result<ProcessReport, String>,
    },
}

fn start_fitting<'scope, 'env>(
    queue: &mut VecDeque<Planned>,
    free: &mut usize,
    inflight: &mut usize,
    peak: &mut usize,
    scope: &'scope thread::Scope<'scope, 'env>,
    tx: &mpsc::Sender<BatchEvent>,
) {
    loop {
        let Some(idx) = queue.iter().position(|p| p.cost <= *free) else {
            break;
        };
        let Planned { item, cost } = queue.remove(idx).expect("index from position");
        *free -= cost;
        *inflight += 1;
        if *inflight > *peak {
            *peak = *inflight;
        }
        let tx = tx.clone();
        scope.spawn(move || {
            let input_for_hop = item.input.clone();
            let tx_hop = tx.clone();
            let bits = item.output_bits.clone();
            let req = ProcessRequest {
                input: &item.input,
                output: &item.output,
                output_rate: item.output_rate,
                output_bits: &bits,
                enable: true,
            };
            // Prefer import-cached Session (skips open_source).
            let _ = ensure_prepared(&item.input, item.output_rate, &bits);
            let result = process_file_cached(
                &req,
                Some(|hops_done, hops_total| {
                    let _ = tx_hop.send(BatchEvent::Hop(TrackHopProgress {
                        input: input_for_hop.clone(),
                        hops_done,
                        hops_total,
                        failed: false,
                    }));
                }),
            )
            .map_err(|e| e.to_string());
            let _ = tx.send(BatchEvent::Done { item, cost, result });
        });
    }
}

/// Process items with slot-budget admission + look-ahead fill.
/// Each [`BatchItem`] carries its own output rate/bits.
/// `on_done(finished, total)`; `on_hop` for per-track hop bars.
pub fn process_batch<F, H>(
    items: &[BatchItem],
    slot_budget: Option<usize>,
    mut on_done: Option<F>,
    mut on_hop: Option<H>,
) -> BatchReport
where
    F: FnMut(usize, usize),
    H: FnMut(TrackHopProgress),
{
    let total = items.len();
    if total == 0 {
        return BatchReport {
            ok: vec![],
            errors: vec![],
            slots: 0,
            peak_tracks: 0,
            jobs: 0,
        };
    }

    let slots = slot_budget.unwrap_or_else(default_slots).max(1);

    let mut queue: VecDeque<Planned> = VecDeque::with_capacity(total);
    for item in items {
        let cost = if item.slot_cost > 0 {
            item.slot_cost.clamp(1, slots)
        } else if let Ok(a) =
            ensure_prepared(&item.input, item.output_rate, &item.output_bits)
        {
            a.slot_cost.clamp(1, slots)
        } else {
            2usize.min(slots).max(1)
        };
        queue.push_back(Planned {
            item: item.clone(),
            cost,
        });
    }

    let prev_omp = std::env::var_os("OMP_NUM_THREADS");
    std::env::set_var("OMP_NUM_THREADS", "2");

    let (tx, rx) = mpsc::channel::<BatchEvent>();
    let mut free = slots;
    let mut inflight = 0usize;
    let mut peak_tracks = 0usize;
    let mut finished = 0usize;
    let mut ok = Vec::new();
    let mut errors = Vec::new();

    thread::scope(|scope| {
        start_fitting(&mut queue, &mut free, &mut inflight, &mut peak_tracks, scope, &tx);

        while finished < total {
            match rx.recv() {
                Ok(BatchEvent::Hop(p)) => {
                    if let Some(ref mut f) = on_hop {
                        f(p);
                    }
                }
                Ok(BatchEvent::Done { item, cost, result }) => {
                    free += cost;
                    inflight -= 1;
                    finished += 1;
                    match result {
                        Ok(report) => {
                            if let Some(ref mut f) = on_hop {
                                f(TrackHopProgress {
                                    input: item.input.clone(),
                                    hops_done: report.hops,
                                    hops_total: report.hops.max(1),
                                    failed: false,
                                });
                            }
                            ok.push(BatchItemReport {
                                input: item.input,
                                output: item.output,
                                report,
                            });
                        }
                        Err(e) => {
                            if let Some(ref mut f) = on_hop {
                                f(TrackHopProgress {
                                    input: item.input.clone(),
                                    hops_done: 0,
                                    hops_total: 0,
                                    failed: true,
                                });
                            }
                            errors.push((item.input, e));
                        }
                    }
                    if let Some(ref mut f) = on_done {
                        f(finished, total);
                    }
                    start_fitting(
                        &mut queue,
                        &mut free,
                        &mut inflight,
                        &mut peak_tracks,
                        scope,
                        &tx,
                    );
                }
                Err(_) => break,
            }
        }
    });

    match prev_omp {
        Some(v) => std::env::set_var("OMP_NUM_THREADS", v),
        None => std::env::remove_var("OMP_NUM_THREADS"),
    }

    ok.sort_by(|a, b| a.input.cmp(&b.input));
    errors.sort_by(|a, b| a.0.cmp(&b.0));
    BatchReport {
        ok,
        errors,
        slots,
        peak_tracks,
        jobs: peak_tracks,
    }
}
