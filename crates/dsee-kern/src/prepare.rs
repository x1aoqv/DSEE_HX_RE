//! Import-time analyze + Session cache so batch process can skip open_source.

use crate::error::{DseeError, Result};
use crate::ffi::{dsee_set_f80_override, dsee_set_heq_enabled};
use crate::media::{self, MediaInfo};
use crate::process::{self, ProcessReport, ProcessRequest};
use crate::wav;
use serde::Serialize;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicI32, AtomicU8, Ordering};
use std::sync::Mutex;

/// 0 = auto; 1 = force F80 off; 2/4 = force that upsample.
static F80_FORCE: AtomicI32 = AtomicI32::new(0);

/// Post-DSEE file sample rate. 0 = keep DSP `cfg.dst` (no extra resample).
static FILE_RESAMPLE_RATE: AtomicI32 = AtomicI32::new(0);

/// Export bit depth override. 0 = use per-request bits; 16/24/32 when advanced.
static FILE_OUTPUT_BITS: AtomicI32 = AtomicI32::new(0);

/// Export codec: 0 = follow source; 1 = PCM/WAV; 2 = FLAC; 3 = ALAC.
static FILE_OUTPUT_CODEC: AtomicU8 = AtomicU8::new(0);

fn normalize_file_rate(hz: i32) -> i32 {
    match hz {
        44_100 | 48_000 | 88_200 | 96_000 | 176_400 | 192_000 => hz,
        _ => 0,
    }
}

fn normalize_file_bits(bits: i32) -> i32 {
    match bits {
        16 | 24 | 32 => bits,
        _ => 0,
    }
}

fn normalize_file_codec(codec: &str) -> u8 {
    match codec.trim().to_ascii_lowercase().as_str() {
        "pcm" | "wav" => 1,
        "flac" => 2,
        "alac" | "m4a" => 3,
        _ => 0,
    }
}

/// Apply advanced DSP / export overrides. Level is per analyze.
pub fn set_advanced_dsp(
    heq: bool,
    f80: Option<i32>,
    file_rate: i32,
    file_bits: i32,
    file_codec: &str,
) {
    let f80v = match f80 {
        Some(1) | Some(2) | Some(4) => f80.unwrap(),
        _ => 0,
    };
    F80_FORCE.store(f80v, Ordering::SeqCst);
    FILE_RESAMPLE_RATE.store(normalize_file_rate(file_rate), Ordering::SeqCst);
    FILE_OUTPUT_BITS.store(normalize_file_bits(file_bits), Ordering::SeqCst);
    FILE_OUTPUT_CODEC.store(normalize_file_codec(file_codec), Ordering::SeqCst);
    unsafe {
        dsee_set_heq_enabled(if heq { 1 } else { 0 });
        dsee_set_f80_override(f80v);
    }
}

/// Target rate for the exported file after DSEE; 0 = same as DSP output.
pub fn file_resample_rate() -> i32 {
    FILE_RESAMPLE_RATE.load(Ordering::SeqCst)
}

/// Export bit depth; 0 = use request bits.
pub fn file_output_bits() -> i32 {
    FILE_OUTPUT_BITS.load(Ordering::SeqCst)
}

/// Forced export codec name (`pcm` / `flac` / `alac`), or `None` = follow source.
pub fn file_output_codec() -> Option<&'static str> {
    match FILE_OUTPUT_CODEC.load(Ordering::SeqCst) {
        1 => Some("pcm"),
        2 => Some("flac"),
        3 => Some("alac"),
        _ => None,
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TrackAnalysis {
    pub path: String,
    pub input_rate: i32,
    pub input_bits: i32,
    pub duration: f64,
    pub level: i32,
    pub f76: bool,
    pub f80: bool,
    pub mode: i32,
    /// 1 = same-rate F76 repair; 2/4 = F80 upsample factor.
    pub up_factor: i32,
    pub output_rate: i32,
    pub output_bits: String,
    pub same_rate_repair: bool,
    /// Allowed F80 factors (subset of 2,4). Empty when only same-rate repair.
    pub allowed_ups: Vec<i32>,
    pub slot_cost: usize,
    /// DSX path on (F76 and/or F80). False still allows HEQ-only process when `processable`.
    pub active: bool,
    /// Session cached / batch can run (includes MC-style HEQ-only when DSX inactive).
    pub processable: bool,
    pub error: Option<String>,
}

#[derive(Clone, Eq, PartialEq, Hash)]
struct PrepKey {
    path: PathBuf,
    output_rate: i32,
    output_bits: String,
}

struct Prepared {
    info: MediaInfo,
    session: process::Session,
    analysis: TrackAnalysis,
}

static CACHE: Mutex<Option<HashMap<PrepKey, Prepared>>> = Mutex::new(None);

fn cache() -> std::sync::MutexGuard<'static, Option<HashMap<PrepKey, Prepared>>> {
    let mut g = CACHE.lock().unwrap_or_else(|e| e.into_inner());
    if g.is_none() {
        *g = Some(HashMap::new());
    }
    g
}

/// MC work_rates family for UI/output selection (≤48k: %4000→48 else→44.1).
fn family_base(in_fs: i32) -> i32 {
    if in_fs == 44100 || in_fs == 88200 || in_fs == 176400 {
        return 44100;
    }
    if in_fs == 48000 || in_fs == 96000 || in_fs == 192000 {
        return 48000;
    }
    // Same rule as open_source.c work_rates for content ≤48 kHz
    // (32 kHz → 48 kHz family → 4× = 192 kHz, matching MC InputFsconv+F80).
    if in_fs <= 48000 {
        if in_fs % 4000 != 0 {
            44100
        } else {
            48000
        }
    } else if in_fs % 4000 != 0 {
        44100
    } else {
        48000
    }
}

/// Map content rate + F80 factor → device/output rate (same family, ×2/×4).
pub fn output_rate_for_up(in_fs: i32, up: i32) -> i32 {
    let up = if up == 4 { 4 } else { 2 };
    let base = family_base(in_fs);
    if in_fs <= 48000 {
        return base * up;
    }
    // Already at 2×/4× work rate: ×2 means go to 4×; ×4 same.
    if in_fs == base * 2 {
        return base * 4;
    }
    if in_fs == base * 4 {
        return base * 4;
    }
    base * up
}

fn same_rate_out(in_fs: i32) -> i32 {
    if in_fs <= 48000 {
        family_base(in_fs)
    } else {
        in_fs
    }
}

fn level_from_mem(mem: &[u8]) -> i32 {
    let off = 4 + 15 * 4;
    if mem.len() < off + 4 {
        return 16;
    }
    i32::from_le_bytes([mem[off], mem[off + 1], mem[off + 2], mem[off + 3]])
}

fn with_level_override<T>(level: Option<i32>, f: impl FnOnce() -> T) -> T {
    unsafe {
        crate::ffi::dsee_set_level_override(level.unwrap_or(0));
    }
    let out = f();
    unsafe {
        crate::ffi::dsee_set_level_override(0);
    }
    out
}

fn try_open(
    info: &MediaInfo,
    output_rate: i32,
    bits: &str,
    level_override: Option<i32>,
) -> Result<(process::Session, i32)> {
    with_level_override(level_override, || {
        let sess = process::open_session_pub(info, output_rate, bits, true)?;
        let level = level_from_mem(&sess.mem);
        Ok((sess, level))
    })
}

fn inactive_analysis(
    path: &Path,
    info: &MediaInfo,
    bits_str: &str,
    allowed_ups: Vec<i32>,
    up_factor: i32,
    output_rate: i32,
) -> TrackAnalysis {
    TrackAnalysis {
        path: path.display().to_string(),
        input_rate: info.rate,
        input_bits: info.bits,
        duration: info.duration,
        level: 16,
        f76: false,
        f80: false,
        mode: 0,
        up_factor,
        output_rate,
        output_bits: bits_str.to_string(),
        same_rate_repair: false,
        allowed_ups,
        slot_cost: 1,
        active: false,
        processable: false,
        error: Some("DSEE HX inactive for these parameters".into()),
    }
}

/// Probe file, discover F76/F80/level/allowed ups, open once for chosen plan, cache Session.
pub fn analyze_track(
    path: &Path,
    up_factor: Option<i32>,
    output_bits: &str,
    level_override: Option<i32>,
) -> Result<TrackAnalysis> {
    let bits_str = if output_bits.trim().is_empty() {
        "24".to_string()
    } else {
        output_bits.trim().to_string()
    };
    let _ = wav::normalize_bits(&bits_str)?;
    // Drop prior plans for this path (rate/bits changes).
    drop_prepared(path);

    let info = media::probe(path)?;
    let in_fs = info.rate;
    if in_fs <= 0 {
        return Err(DseeError::Msg("invalid input sample rate".into()));
    }

    // Discover allowed F80 ups with minimal opens (×4 then ×2).
    let mut allowed_ups = Vec::new();
    let mut probed: HashMap<i32, (bool, bool, bool, i32, i32)> = HashMap::new(); // out -> active,f76,f80,level,mode
    for up in [4i32, 2] {
        let out = output_rate_for_up(in_fs, up);
        if probed.contains_key(&out) {
            if allowed_ups.contains(&up) || probed[&out].2 {
                if !allowed_ups.contains(&up) && probed[&out].0 && probed[&out].2 {
                    allowed_ups.push(up);
                }
            }
            continue;
        }
        if let Ok((sess, level)) = try_open(&info, out, &bits_str, level_override) {
            let active = sess.cfg.active != 0;
            let f76 = sess.cfg.f76 != 0;
            let f80 = sess.cfg.f80 != 0;
            let mode = sess.source.mode;
            if active && f80 {
                allowed_ups.push(up);
            }
            probed.insert(out, (active, f76, f80, level, mode));
            drop(sess);
        }
    }
    allowed_ups.sort_unstable();
    allowed_ups.dedup();

    let same_out = same_rate_out(in_fs);
    let mut same_rate_repair = false;
    if !probed.contains_key(&same_out) {
        if let Ok((sess, level)) = try_open(&info, same_out, &bits_str, level_override) {
            let active = sess.cfg.active != 0;
            let f76 = sess.cfg.f76 != 0;
            let f80 = sess.cfg.f80 != 0;
            if active && f76 && !f80 {
                same_rate_repair = true;
            }
            probed.insert(
                same_out,
                (active, f76, f80, level, sess.source.mode),
            );
            drop(sess);
        }
    } else if let Some(&(active, f76, f80, _, _)) = probed.get(&same_out) {
        same_rate_repair = active && f76 && !f80;
    }

    let f80_force = F80_FORCE.load(Ordering::SeqCst);
    // Default (auto) matches Music Center:
    //   ≤48 kHz → prefer max F80; hi-res content → same-rate (often inactive for
    //   lossless mode-2), never auto-pick F80 on already-hi-res sources.
    // Explicit up_factor / F80_FORCE (advanced UI) always wins.
    let (chosen_up, out_rate) = if f80_force == 1 {
        (1, same_out)
    } else if f80_force == 2 || f80_force == 4 {
        (f80_force, output_rate_for_up(in_fs, f80_force))
    } else if let Some(u) = up_factor {
        if u <= 1 {
            (1, same_out)
        } else if u == 2 || u == 4 {
            // User/advanced explicit F80 factor — do not require prior probe hit.
            (u, output_rate_for_up(in_fs, u))
        } else if allowed_ups.contains(&u) {
            (u, output_rate_for_up(in_fs, u))
        } else if same_rate_repair {
            (1, same_out)
        } else {
            return Ok(inactive_analysis(
                path,
                &info,
                &bits_str,
                allowed_ups,
                1,
                same_out,
            ));
        }
    } else if in_fs > 48000 {
        (1, same_out)
    } else if let Some(&u) = allowed_ups.iter().rev().next() {
        (u, output_rate_for_up(in_fs, u))
    } else if same_rate_repair {
        (1, same_out)
    } else {
        return Ok(inactive_analysis(
            path,
            &info,
            &bits_str,
            allowed_ups,
            1,
            same_out,
        ));
    };

    // Advanced F80 force: advertise the forced factor in allowed_ups for UI.
    let allowed_ups = if f80_force == 2 || f80_force == 4 {
        vec![f80_force]
    } else if f80_force == 1 {
        Vec::new()
    } else {
        allowed_ups
    };

    // Final open: align C F80 override with chosen factor so advanced
    // 2×/4× can force F80 even when decide() would leave it off (e.g. mode-2).
    let (sess, level) = {
        let prev = F80_FORCE.load(Ordering::SeqCst);
        let hint = if chosen_up == 2 || chosen_up == 4 {
            chosen_up
        } else if chosen_up <= 1 {
            1
        } else {
            prev
        };
        unsafe {
            dsee_set_f80_override(hint);
        }
        let opened = try_open(&info, out_rate, &bits_str, level_override);
        unsafe {
            dsee_set_f80_override(prev);
        }
        match opened {
            Ok(v) => v,
            Err(DseeError::Inactive) => {
                return Ok(inactive_analysis(
                    path,
                    &info,
                    &bits_str,
                    allowed_ups,
                    chosen_up,
                    out_rate,
                ));
            }
            Err(e) => return Err(e),
        }
    };
    let f76 = sess.cfg.f76 != 0;
    let f80 = sess.cfg.f80 != 0;
    let mode = sess.source.mode;
    let dsx_active = sess.cfg.active != 0;
    let slot_cost = if f80 { 2 } else { 1 };
    let analysis = TrackAnalysis {
        path: path.display().to_string(),
        input_rate: in_fs,
        input_bits: info.bits,
        duration: info.duration,
        level,
        f76,
        f80,
        mode,
        up_factor: chosen_up,
        output_rate: out_rate,
        output_bits: bits_str.clone(),
        same_rate_repair: chosen_up <= 1 || (f76 && !f80),
        allowed_ups,
        slot_cost,
        active: dsx_active,
        // HEQ-only when DSX off — matches Music Center for hi-res lossless same-rate.
        processable: true,
        error: None,
    };

    let key = PrepKey {
        path: path.to_path_buf(),
        output_rate: out_rate,
        output_bits: bits_str,
    };
    cache().as_mut().unwrap().insert(
        key,
        Prepared {
            info,
            session: sess,
            analysis: analysis.clone(),
        },
    );
    Ok(analysis)
}

pub fn drop_prepared(path: &Path) {
    let mut g = cache();
    let map = g.as_mut().unwrap();
    map.retain(|k, _| k.path != path);
}

pub fn clear_prepared() {
    if let Some(m) = cache().as_mut() {
        m.clear();
    }
}

/// Take cached Session (removes from cache). `open_s` is 0 when cache hit.
pub fn take_prepared(
    path: &Path,
    output_rate: i32,
    output_bits: &str,
) -> Option<(MediaInfo, process::Session, f64)> {
    let key = PrepKey {
        path: path.to_path_buf(),
        output_rate,
        output_bits: output_bits.to_string(),
    };
    let prepared = cache().as_mut().unwrap().remove(&key)?;
    Some((prepared.info, prepared.session, 0.0))
}

/// Process using cached Session when present (skips probe + open_source).
pub fn process_file_cached<F>(
    req: &ProcessRequest<'_>,
    on_hop: Option<F>,
) -> Result<ProcessReport>
where
    F: FnMut(usize, usize),
{
    if let Some((info, sess, open_s)) =
        take_prepared(req.input, req.output_rate, req.output_bits)
    {
        return process::process_with_session(req, info, sess, 0.0, open_s, on_hop);
    }
    process::process_file_with_progress(req, on_hop)
}

/// Re-open into cache after a previous take (e.g. user re-runs same queue).
pub fn ensure_prepared(path: &Path, output_rate: i32, output_bits: &str) -> Result<TrackAnalysis> {
    let key = PrepKey {
        path: path.to_path_buf(),
        output_rate,
        output_bits: output_bits.to_string(),
    };
    if let Some(p) = cache().as_ref().and_then(|m| m.get(&key)) {
        return Ok(p.analysis.clone());
    }
    let info = media::probe(path)?;
    let base = family_base(info.rate);
    let up = if output_rate <= same_rate_out(info.rate) {
        Some(1)
    } else if output_rate >= base * 4 {
        Some(4)
    } else {
        Some(2)
    };
    analyze_track(path, up, output_bits, None)
}

#[cfg(test)]
mod tests {
    use super::{family_base, output_rate_for_up};

    #[test]
    fn mc_work_rates_32k_is_48_family() {
        assert_eq!(family_base(32_000), 48_000);
        assert_eq!(output_rate_for_up(32_000, 2), 96_000);
        assert_eq!(output_rate_for_up(32_000, 4), 192_000);
    }

    #[test]
    fn mc_work_rates_22k05_is_441_family() {
        assert_eq!(family_base(22_050), 44_100);
        assert_eq!(output_rate_for_up(22_050, 4), 176_400);
    }
}
