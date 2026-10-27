use crate::error::{DseeError, Result};
use crate::export::finalize_export;
use crate::ffi::{
    dsee_fill_source, dsee_heq, dsee_heq_delays, dsee_heq_gain, dsee_mem_size, dsee_open_source,
    dsee_process_i32, DseeCfg, DseeSource,
};
use crate::media::{self, MediaInfo, S32leDecodeStream};
use crate::wav::{self, WavWriter};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

const RING: usize = 1589;
const HIST: usize = 42;

pub struct ProcessRequest<'a> {
    pub input: &'a Path,
    pub output: &'a Path,
    pub output_rate: i32,
    pub output_bits: &'a str,
    pub enable: bool,
}

/// Wall seconds spent in each e2e stage (streaming pipeline).
#[derive(Clone, Debug, Default)]
pub struct StageTiming {
    pub probe_s: f64,
    pub open_s: f64,
    /// Time blocked in ffmpeg `read_samples` (+ finish).
    pub decode_s: f64,
    /// Time in `process_hops` (HEQ/F76/F80/quant).
    pub dsp_s: f64,
    /// Time in WAV `write_samples` + header finish.
    pub write_s: f64,
    pub total_s: f64,
}

#[derive(Clone)]
pub struct ProcessReport {
    pub input_rate: i32,
    pub output_rate: i32,
    pub wav_rate: i32,
    pub hops: usize,
    pub samples_out: usize,
    pub mode: i32,
    pub f76: bool,
    pub f80: bool,
    pub stages: StageTiming,
}

pub(crate) struct Session {
    pub(crate) mem: Vec<u8>,
    pub(crate) cfg: DseeCfg,
    pub(crate) source: DseeSource,
    delay: [i32; 7],
    gain: f64,
    ring0: Vec<f32>,
    ring1: Vec<f32>,
    hist0: Vec<f32>,
    hist1: Vec<f32>,
    lag: i32,
    hidx: i32,
    n_in: usize,
    n_out: usize,
    hop_chunk: usize,
}

fn emit_hops<F: FnMut(usize, usize)>(on_hop: &mut Option<F>, done: usize, total: usize) {
    if let Some(f) = on_hop.as_mut() {
        f(done, total);
    }
}

fn temp_pcm_wav_path(final_out: &Path) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = final_out
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));
    let stem = final_out
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("dsee");
    dir.join(format!(".{stem}.{nanos}.dsee_pcm.tmp.wav"))
}

pub(crate) fn open_session_pub(
    info: &MediaInfo,
    output_rate: i32,
    output_bits: &str,
    enable: bool,
) -> Result<Session> {
    open_session(info, output_rate, output_bits, enable)
}

fn open_session(info: &MediaInfo, output_rate: i32, output_bits: &str, enable: bool) -> Result<Session> {
    let out_bits_i = wav::normalize_bits(output_bits)? as i32;
    let facts = media::to_facts(info);
    let mut source = DseeSource {
        mode: 0,
        channels: 0,
        codec: 0,
        input_fs: 0,
        output_fs: 0,
        input_bits: 0,
        output_bits: 0,
        vbr: 0,
        abr: 0,
        sb: 0,
        head: 0,
        dcr: 0,
        gain: 0,
        offset: 0,
    };
    let rc = unsafe {
        dsee_fill_source(
            &facts,
            output_rate,
            out_bits_i,
            if enable { 1 } else { 0 },
            &mut source,
        )
    };
    if rc != 0 {
        return Err(DseeError::Msg(format!("fill_source rc={rc}")));
    }

    let mem_size = unsafe { dsee_mem_size() } as usize;
    let mut mem = vec![0u8; mem_size];
    let mut cfg = DseeCfg::default();
    let rc = unsafe { dsee_open_source(mem.as_mut_ptr(), mem_size as i32, &source, &mut cfg) };
    if rc != 0 {
        return Err(DseeError::OpenSource(rc));
    }
    // cfg.active==0 means F76/F80 off (e.g. mode-2 hi-res same-rate). Music Center
    // still runs HEQ in that case; frame.c already skips DSX when f76/f80 are 0.

    let mut delay = [0i32; 7];
    if unsafe { dsee_heq_delays(cfg.src, delay.as_mut_ptr()) } != 0 {
        return Err(DseeError::Msg(format!("no HEQ delay for {} Hz", cfg.src)));
    }
    let heq_bits = if matches!(info.bits, 8 | 16 | 20 | 24 | 32) {
        info.bits
    } else {
        16
    };
    let mut gain_f = 0f32;
    if unsafe { dsee_heq_gain(heq_bits, &mut gain_f) } != 0 {
        return Err(DseeError::Msg(format!("no HEQ gain for {heq_bits}-bit")));
    }

    let n_in = (cfg.rs_in * 2) as usize;
    let n_out = (cfg.rs_out * 2) as usize;
    let hop_chunk = ((cfg.src as usize / (cfg.rs_in as usize).max(1)) / 2).max(256);

    Ok(Session {
        mem,
        cfg,
        source,
        delay,
        gain: gain_f as f64,
        ring0: vec![0f32; RING * 2],
        ring1: vec![0f32; RING * 2],
        hist0: vec![0f32; HIST],
        hist1: vec![0f32; HIST],
        lag: 0,
        hidx: 0,
        n_in,
        n_out,
        hop_chunk,
    })
}

fn process_hops(sess: &mut Session, pcm: &[i32], hops: usize, out: &mut [f32]) -> Result<()> {
    debug_assert_eq!(pcm.len(), hops * sess.n_in);
    debug_assert_eq!(out.len(), hops * sess.n_out);
    let rc = unsafe {
        dsee_process_i32(
            sess.mem.as_mut_ptr(),
            sess.ring0.as_mut_ptr(),
            sess.ring1.as_mut_ptr(),
            sess.hist0.as_mut_ptr(),
            sess.hist1.as_mut_ptr(),
            &mut sess.lag,
            &mut sess.hidx,
            sess.delay.as_ptr(),
            sess.gain,
            pcm.as_ptr(),
            hops as i32,
            sess.n_in as i32,
            sess.n_out as i32,
            sess.cfg.rs_in,
            sess.cfg.rs_out,
            out.as_mut_ptr(),
        )
    };
    if rc != 0 {
        return Err(DseeError::Msg(format!("process_i32 rc={rc}")));
    }
    Ok(())
}

/// Music Center HEQ-only (F76/F80 off): HEQ → Q31 wire pack, no float/DSX.
fn process_hops_heq_q31(
    sess: &mut Session,
    pcm: &[i32],
    hops: usize,
    out: &mut [i32],
) -> Result<()> {
    debug_assert_eq!(pcm.len(), hops * sess.n_in);
    debug_assert_eq!(out.len(), hops * sess.n_out);
    debug_assert_eq!(sess.n_in, sess.n_out);
    for h in 0..hops {
        let off = h * sess.n_in;
        let nframe = (sess.n_in / 2) as i32;
        unsafe {
            dsee_heq(
                sess.ring0.as_mut_ptr(),
                sess.ring1.as_mut_ptr(),
                sess.hist0.as_mut_ptr(),
                sess.hist1.as_mut_ptr(),
                &mut sess.lag,
                &mut sess.hidx,
                sess.delay.as_ptr(),
                sess.gain,
                pcm[off..].as_ptr(),
                out[off..].as_mut_ptr(),
                nframe,
            );
        }
    }
    Ok(())
}

pub fn process_file(req: &ProcessRequest<'_>) -> Result<ProcessReport> {
    process_file_with_progress(req, None::<fn(usize, usize)>)
}

/// Slot width for batch admission: F80 (alone or with F76) → 2, F76-only → 1.
pub fn pipeline_slot_cost(
    input: &Path,
    output_rate: i32,
    output_bits: &str,
    enable: bool,
) -> Result<(bool, bool, usize)> {
    let _ = wav::normalize_bits(output_bits)?;
    let info = media::probe(input)?;
    let sess = open_session(&info, output_rate, output_bits, enable)?;
    let f76 = sess.cfg.f76 != 0;
    let f80 = sess.cfg.f80 != 0;
    let cost = if f80 { 2 } else { 1 };
    Ok((f76, f80, cost))
}

/// Decode ∥ DSP ∥ stream WAV write: ffmpeg pipe → hop chunks → WavWriter.
/// `on_hop(done, total)` — `total` is duration-based estimate (0 if unknown).
pub fn process_file_with_progress<F>(
    req: &ProcessRequest<'_>,
    on_hop: Option<F>,
) -> Result<ProcessReport>
where
    F: FnMut(usize, usize),
{
    let t_all = Instant::now();
    let _ = wav::normalize_bits(req.output_bits)?;
    let t0 = Instant::now();
    let info = media::probe(req.input)?;
    let probe_s = t0.elapsed().as_secs_f64();

    let t0 = Instant::now();
    let sess = open_session(&info, req.output_rate, req.output_bits, req.enable)?;
    let open_s = t0.elapsed().as_secs_f64();
    process_with_session(req, info, sess, probe_s, open_s, on_hop).map(|mut r| {
        r.stages.total_s = t_all.elapsed().as_secs_f64();
        r
    })
}

/// Run pipeline with an already-opened session (import cache hit → open_s≈0).
pub(crate) fn process_with_session<F>(
    req: &ProcessRequest<'_>,
    info: MediaInfo,
    mut sess: Session,
    probe_s: f64,
    open_s: f64,
    mut on_hop: Option<F>,
) -> Result<ProcessReport>
where
    F: FnMut(usize, usize),
{
    let t_all = Instant::now();
    // Always write PCM to a temp WAV, then remux/encode to `req.output` with
    // source tags/cover (and matching container when possible).
    let tmp_wav = temp_pcm_wav_path(req.output);
    let mut wav = WavWriter::create(&tmp_wav, sess.cfg.dst as u32, req.output_bits)?;
    // MC InputFsconv: content → work_rates src before HEQ/DSX (e.g. 32k→48k).
    // Decode must match cfg.src or playback is fast/sharp (32k into 48→192 ≈ 1.5×).
    let decode_ar = if info.rate != sess.cfg.src && sess.cfg.src > 0 {
        Some(sess.cfg.src)
    } else {
        None
    };
    let mut decode = S32leDecodeStream::spawn_ar(req.input, decode_ar)?;

    // HEQ-only (MC hi-res same-rate): pack Q31 like OmgDseeHxFilter 25AB — no float.
    let heq_only = sess.cfg.f76 == 0 && sess.cfg.f80 == 0;

    let mut pcm_buf = vec![0i32; sess.hop_chunk * sess.n_in];
    let mut out_f = vec![0f32; sess.hop_chunk * sess.n_out];
    let mut out_q = vec![0i32; sess.hop_chunk * sess.n_out];
    let mut pending: Vec<i32> = Vec::with_capacity(sess.n_in * 2);
    let mut hops_done = 0usize;
    let mut decode_s = 0.0;
    let mut dsp_s = 0.0;
    let mut write_s = 0.0;
    let work_rate = if sess.cfg.src > 0 {
        sess.cfg.src
    } else {
        info.rate
    };
    let est_hops = if info.duration > 0.0 && work_rate > 0 && sess.cfg.rs_in > 0 {
        ((info.duration * work_rate as f64) / sess.cfg.rs_in as f64).ceil() as usize
    } else {
        0
    };
    emit_hops(&mut on_hop, 0, est_hops);
    let mut last_emit = Instant::now();

    loop {
        while pending.len() < pcm_buf.len() {
            let space = pcm_buf.len() - pending.len();
            let t0 = Instant::now();
            let n = decode.read_samples(&mut pcm_buf[..space])?;
            decode_s += t0.elapsed().as_secs_f64();
            if n == 0 {
                break;
            }
            pending.extend_from_slice(&pcm_buf[..n]);
        }

        let hops = pending.len() / sess.n_in;
        if hops == 0 {
            break;
        }
        let take = hops.min(sess.hop_chunk);
        let pcm_n = take * sess.n_in;
        let out_n = take * sess.n_out;
        let t0 = Instant::now();
        if heq_only {
            process_hops_heq_q31(&mut sess, &pending[..pcm_n], take, &mut out_q[..out_n])?;
        } else {
            process_hops(&mut sess, &pending[..pcm_n], take, &mut out_f[..out_n])?;
        }
        dsp_s += t0.elapsed().as_secs_f64();
        let t0 = Instant::now();
        if heq_only {
            wav.write_q31_samples(&out_q[..out_n])?;
        } else {
            wav.write_samples(&out_f[..out_n])?;
        }
        write_s += t0.elapsed().as_secs_f64();
        pending.drain(..pcm_n);
        hops_done += take;

        if last_emit.elapsed().as_millis() >= 40 || (est_hops > 0 && hops_done >= est_hops) {
            emit_hops(&mut on_hop, hops_done, est_hops);
            last_emit = Instant::now();
        }
    }

    let t0 = Instant::now();
    decode.finish()?;
    decode_s += t0.elapsed().as_secs_f64();
    let t0 = Instant::now();
    wav.finish()?;
    if hops_done == 0 {
        let _ = fs::remove_file(&tmp_wav);
        return Err(DseeError::Msg("input too short for one hop".into()));
    }
    if let Err(e) = finalize_export(
        req.input,
        &tmp_wav,
        req.output,
        sess.cfg.dst,
        req.output_bits,
    ) {
        let _ = fs::remove_file(&tmp_wav);
        return Err(e);
    }
    let _ = fs::remove_file(&tmp_wav);
    write_s += t0.elapsed().as_secs_f64();
    emit_hops(&mut on_hop, hops_done, hops_done.max(1));

    Ok(ProcessReport {
        input_rate: info.rate,
        output_rate: req.output_rate,
        wav_rate: sess.cfg.dst,
        hops: hops_done,
        samples_out: hops_done * sess.n_out,
        mode: sess.source.mode,
        f76: sess.cfg.f76 != 0,
        f80: sess.cfg.f80 != 0,
        stages: StageTiming {
            probe_s,
            open_s,
            decode_s,
            dsp_s,
            write_s,
            total_s: t_all.elapsed().as_secs_f64() + probe_s + open_s,
        },
    })
}

pub fn process_pcm(
    info: &MediaInfo,
    pcm: &[i32],
    output_rate: i32,
    output_bits: &str,
    enable: bool,
) -> Result<(Vec<f32>, ProcessReport)> {
    process_pcm_with_progress(
        info,
        pcm,
        output_rate,
        output_bits,
        enable,
        None::<&mut dyn FnMut(&str, u32, &str)>,
    )
}

pub fn process_pcm_with_progress<F>(
    info: &MediaInfo,
    pcm: &[i32],
    output_rate: i32,
    output_bits: &str,
    enable: bool,
    mut progress: Option<&mut F>,
) -> Result<(Vec<f32>, ProcessReport)>
where
    F: FnMut(&str, u32, &str) + ?Sized,
{
    let mut sess = open_session(info, output_rate, output_bits, enable)?;
    let hops = pcm.len() / sess.n_in;
    if hops == 0 {
        return Err(DseeError::Msg("input too short for one hop".into()));
    }
    let pcm_use = &pcm[..hops * sess.n_in];
    let mut out = vec![0f32; hops * sess.n_out];

    let n_in = sess.n_in;
    let n_out = sess.n_out;
    let mut done = 0usize;
    while done < hops {
        let count = sess.hop_chunk.min(hops - done);
        let pcm_off = done * n_in;
        let out_off = done * n_out;
        process_hops(
            &mut sess,
            &pcm_use[pcm_off..pcm_off + count * n_in],
            count,
            &mut out[out_off..out_off + count * n_out],
        )?;
        done += count;
        let dsp_pct = 15 + ((done as u64 * 79) / hops.max(1) as u64) as u32;
        if let Some(f) = progress.as_mut() {
            f("dsp", dsp_pct.min(94), &format!("DSP {done}/{hops}"));
        }
    }

    Ok((
        out,
        ProcessReport {
            input_rate: info.rate,
            output_rate,
            wav_rate: sess.cfg.dst,
            hops,
            samples_out: hops * sess.n_out,
            mode: sess.source.mode,
            f76: sess.cfg.f76 != 0,
            f80: sess.cfg.f80 != 0,
            stages: StageTiming::default(),
        },
    ))
}
