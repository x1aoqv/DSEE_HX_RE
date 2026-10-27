use crate::error::{DseeError, Result};
use crate::ffi::DseeMediaFacts;
use std::collections::VecDeque;
use std::fs;
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;

#[cfg(windows)]
use std::os::windows::process::CommandExt;

/// Hide console flash when spawning ffmpeg / ffprobe on Windows.
#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

fn command(program: impl AsRef<std::ffi::OsStr>) -> Command {
    let mut c = Command::new(program);
    #[cfg(windows)]
    {
        c.creation_flags(CREATE_NO_WINDOW);
    }
    c
}

static FFMPEG_OVERRIDE: Mutex<Option<PathBuf>> = Mutex::new(None);
static FFPROBE_OVERRIDE: Mutex<Option<PathBuf>> = Mutex::new(None);

/// FFmpeg soxr `precision` max (aresample range 15–33).
pub const SOX_PRECISION: u32 = 33;

/// `aresample` filter string: soxr at max precision, output sample rate `osr`.
pub fn soxr_aresample_filter(osr: i32) -> String {
    format!("aresample=resampler=soxr:precision={SOX_PRECISION}:osr={osr}")
}

/// Explicit absolute path from the UI (empty / None → not configured).
pub fn set_ffmpeg_override(path: Option<PathBuf>) {
    if let Ok(mut g) = FFMPEG_OVERRIDE.lock() {
        *g = path.filter(|p| !p.as_os_str().is_empty());
    }
}

pub fn set_ffprobe_override(path: Option<PathBuf>) {
    if let Ok(mut g) = FFPROBE_OVERRIDE.lock() {
        *g = path.filter(|p| !p.as_os_str().is_empty());
    }
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MediaToolsInfo {
    pub ffmpeg: Option<String>,
    pub ffmpeg_ok: bool,
    pub ffprobe: Option<String>,
    pub ffprobe_ok: bool,
}

pub fn media_tools_info() -> MediaToolsInfo {
    let ffmpeg = ffmpeg_path().ok();
    let ffprobe = ffprobe_path().ok();
    MediaToolsInfo {
        ffmpeg_ok: ffmpeg.is_some(),
        ffprobe_ok: ffprobe.is_some(),
        ffmpeg: ffmpeg.map(|p| p.display().to_string()),
        ffprobe: ffprobe.map(|p| p.display().to_string()),
    }
}

#[derive(Debug, Clone)]
pub struct MediaInfo {
    pub rate: i32,
    pub channels: i32,
    pub bits: i32,
    pub bit_rate: i32,
    pub codec_name: String,
    pub path_has_cbr: bool,
    pub duration: f64,
}

/// Folder that contains the running exe (portable pack root, e.g. `DSEE_HX_RE_v1.0.0`).
fn app_install_dir() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    exe.parent().map(|p| p.to_path_buf())
}

/// BFS under `root` for any of `names` (case-insensitive). Prefers shallower paths.
fn find_named_under(root: &Path, names: &[&str]) -> Option<PathBuf> {
    let mut q = VecDeque::new();
    q.push_back(root.to_path_buf());
    while let Some(dir) = q.pop_front() {
        let Ok(rd) = fs::read_dir(&dir) else {
            continue;
        };
        let mut subdirs = Vec::new();
        for ent in rd.flatten() {
            let p = ent.path();
            let Ok(meta) = ent.metadata() else {
                continue;
            };
            if meta.is_file() {
                if let Some(name) = p.file_name().and_then(|n| n.to_str()) {
                    if names.iter().any(|n| n.eq_ignore_ascii_case(name)) {
                        return Some(p);
                    }
                }
            } else if meta.is_dir() {
                subdirs.push(p);
            }
        }
        q.extend(subdirs);
    }
    None
}

fn sibling_tool(of: &Path, names: &[&str]) -> Option<PathBuf> {
    for name in names {
        let sib = of.with_file_name(name);
        if sib.is_file() {
            return Some(sib);
        }
    }
    None
}

fn find_on_path(names: &[&str]) -> Option<PathBuf> {
    let path = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path) {
        for name in names {
            let p = dir.join(name);
            if p.is_file() {
                return Some(p);
            }
        }
    }
    None
}

fn find_from_env(keys: &[&str]) -> Option<PathBuf> {
    for k in keys {
        if let Ok(p) = std::env::var(k) {
            let pb = PathBuf::from(p.trim());
            if pb.is_file() {
                return Some(pb);
            }
        }
    }
    None
}

/// Resolve one tool: app folder → env vars → system PATH.
fn resolve_tool(env_keys: &[&str], names: &[&str]) -> Option<PathBuf> {
    if let Some(root) = app_install_dir() {
        if let Some(p) = find_named_under(&root, names) {
            return Some(p);
        }
    }
    find_from_env(env_keys).or_else(|| find_on_path(names))
}

/// Scan app folder (recursive) + env (`DSEE_FFMPEG` / `FFMPEG` …) + system `PATH`.
/// Sets overrides to the resolved absolute paths (or clears if missing).
pub fn detect_media_tools_in_app_dir() -> MediaToolsInfo {
    let ffmpeg = resolve_tool(
        &["DSEE_FFMPEG", "FFMPEG"],
        &["ffmpeg.exe", "ffmpeg"],
    );
    let ffprobe = resolve_tool(
        &["DSEE_FFPROBE", "FFPROBE"],
        &["ffprobe.exe", "ffprobe"],
    )
    .or_else(|| ffmpeg.as_ref().and_then(|ff| sibling_tool(ff, &["ffprobe.exe", "ffprobe"])));
    set_ffmpeg_override(ffmpeg);
    set_ffprobe_override(ffprobe);
    media_tools_info()
}

pub fn ffmpeg_path() -> Result<PathBuf> {
    if let Ok(g) = FFMPEG_OVERRIDE.lock() {
        if let Some(ref p) = *g {
            if p.is_file() {
                return Ok(p.clone());
            }
            return Err(DseeError::Ffmpeg(format!(
                "ffmpeg not found: {}",
                p.display()
            )));
        }
    }
    Err(DseeError::Ffmpeg(
        "ffmpeg not configured (Auto-detect or set path in Settings)".into(),
    ))
}

pub fn ffprobe_path() -> Result<PathBuf> {
    if let Ok(g) = FFPROBE_OVERRIDE.lock() {
        if let Some(ref p) = *g {
            if p.is_file() {
                return Ok(p.clone());
            }
            return Err(DseeError::Ffmpeg(format!(
                "ffprobe not found: {}",
                p.display()
            )));
        }
    }
    if let Ok(ff) = ffmpeg_path() {
        if let Some(sib) = sibling_tool(&ff, &["ffprobe.exe", "ffprobe"]) {
            return Ok(sib);
        }
    }
    Err(DseeError::Ffmpeg(
        "ffprobe not configured (Auto-detect or set path in Settings)".into(),
    ))
}

fn bits_from_stream(a: &serde_json::Value) -> i32 {
    if let Some(b) = a
        .get("bits_per_raw_sample")
        .and_then(|v| v.as_str())
        .and_then(|s| s.parse().ok())
        .or_else(|| a.get("bits_per_raw_sample").and_then(|v| v.as_i64()).map(|x| x as i32))
        .or_else(|| a.get("bits_per_sample").and_then(|v| v.as_i64()).map(|x| x as i32))
    {
        if b > 0 {
            return b;
        }
    }
    let fmt = a
        .get("sample_fmt")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_lowercase();
    if fmt.contains("s16") {
        16
    } else if fmt.contains("s32") || fmt.contains("flt") {
        32
    } else if fmt.contains("u8") {
        8
    } else {
        16
    }
}

pub fn probe(path: &Path) -> Result<MediaInfo> {
    let ffprobe = ffprobe_path()?;
    let out = command(&ffprobe)
        .args([
            "-v",
            "error",
            "-show_streams",
            "-show_format",
            "-of",
            "json",
        ])
        .arg(path)
        .output()
        .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
    if !out.status.success() {
        return Err(DseeError::Ffmpeg(
            String::from_utf8_lossy(&out.stderr).into(),
        ));
    }
    let v: serde_json::Value = serde_json::from_slice(&out.stdout)
        .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
    let streams = v["streams"]
        .as_array()
        .ok_or_else(|| DseeError::Ffmpeg("no streams".into()))?;
    let a = streams
        .iter()
        .find(|s| s["codec_type"].as_str() == Some("audio"))
        .ok_or_else(|| DseeError::Ffmpeg("no audio stream".into()))?;
    let rate = a["sample_rate"]
        .as_str()
        .and_then(|s| s.parse().ok())
        .or_else(|| a["sample_rate"].as_i64().map(|x| x as i32))
        .unwrap_or(0);
    let channels = a["channels"].as_i64().unwrap_or(0) as i32;
    let bits = bits_from_stream(a);
    let bit_rate = a["bit_rate"]
        .as_str()
        .and_then(|s| s.parse().ok())
        .or_else(|| v["format"]["bit_rate"].as_str().and_then(|s| s.parse().ok()))
        .unwrap_or(0);
    let codec_name = a["codec_name"].as_str().unwrap_or("").to_lowercase();
    let duration = v["format"]["duration"]
        .as_str()
        .and_then(|s| s.parse().ok())
        .unwrap_or(0.0);
    let path_has_cbr = path
        .file_name()
        .and_then(|s| s.to_str())
        .map(|s| s.to_lowercase().contains("cbr"))
        .unwrap_or(false);
    Ok(MediaInfo {
        rate,
        channels,
        bits,
        bit_rate,
        codec_name,
        path_has_cbr,
        duration,
    })
}

pub fn to_facts(m: &MediaInfo) -> DseeMediaFacts {
    let mut codec_name = [0u8; 64];
    let bytes = m.codec_name.as_bytes();
    let n = bytes.len().min(63);
    codec_name[..n].copy_from_slice(&bytes[..n]);
    DseeMediaFacts {
        rate: m.rate,
        channels: m.channels,
        bits: m.bits,
        bit_rate: m.bit_rate,
        codec_name,
        path_has_cbr: if m.path_has_cbr { 1 } else { 0 },
    }
}

/// ffmpeg stdout → interleaved stereo s32le. Overlaps with DSP while the pipe buffers.
pub struct S32leDecodeStream {
    child: Child,
    reader: BufReader<std::process::ChildStdout>,
    byte_carry: Vec<u8>,
}

impl S32leDecodeStream {
    /// Decode stereo s32le. When `ar` is set, resample to that rate first.
    /// Mirrors MC `OmgDseeHxFilter` InputFsconv (CFsconvWrap): content rate →
    /// `work_rates` src (e.g. 32 kHz → 48 kHz) before HEQ/DSX. Without this,
    /// feeding 32 kHz PCM into a 48→192 session plays ~1.5× fast/sharp.
    pub fn spawn_ar(path: &Path, ar: Option<i32>) -> Result<Self> {
        let ffmpeg = ffmpeg_path()?;
        let mut args: Vec<String> = vec![
            "-v".into(),
            "error".into(),
            "-i".into(),
            path.display().to_string(),
        ];
        if let Some(rate) = ar.filter(|&r| r > 0) {
            // soxr max precision ≈ high-quality InputFsconv; -ar alone uses swr.
            args.push("-af".into());
            args.push(soxr_aresample_filter(rate));
            args.push("-ar".into());
            args.push(rate.to_string());
        }
        args.extend([
            "-f".into(),
            "s32le".into(),
            "-acodec".into(),
            "pcm_s32le".into(),
            "-ac".into(),
            "2".into(),
            "-".into(),
        ]);
        let mut child = command(&ffmpeg)
            .args(&args)
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| DseeError::Ffmpeg("ffmpeg stdout missing".into()))?;
        Ok(Self {
            child,
            reader: BufReader::with_capacity(1024 * 1024, stdout),
            byte_carry: Vec::new(),
        })
    }

    /// Read up to `dst.len()` interleaved i32 samples. Returns samples read (0 = EOF).
    /// Always returns a multiple of 2 (stereo frames); drops a trailing orphan sample.
    pub fn read_samples(&mut self, dst: &mut [i32]) -> Result<usize> {
        if dst.is_empty() {
            return Ok(0);
        }
        let mut byte_buf = vec![0u8; dst.len() * 4];
        let mut filled = 0usize;

        // Drain carry first.
        while !self.byte_carry.is_empty() && filled < byte_buf.len() {
            byte_buf[filled] = self.byte_carry.remove(0);
            filled += 1;
        }

        while filled < byte_buf.len() {
            match self.reader.read(&mut byte_buf[filled..]) {
                Ok(0) => break,
                Ok(n) => filled += n,
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(e) => return Err(DseeError::Io(e)),
            }
        }

        let usable = filled - (filled % 4);
        let rem = filled - usable;
        if rem > 0 {
            self.byte_carry.clear();
            self.byte_carry.extend_from_slice(&byte_buf[usable..filled]);
        }

        let mut n_samp = usable / 4;
        if n_samp % 2 == 1 {
            // Keep last 4 bytes as carry so we don't drop a channel mid-frame forever.
            let start = (n_samp - 1) * 4;
            self.byte_carry
                .splice(0..0, byte_buf[start..start + 4].iter().copied());
            n_samp -= 1;
        }

        for i in 0..n_samp {
            let o = i * 4;
            dst[i] = i32::from_le_bytes([
                byte_buf[o],
                byte_buf[o + 1],
                byte_buf[o + 2],
                byte_buf[o + 3],
            ]);
        }
        Ok(n_samp)
    }

    pub fn finish(mut self) -> Result<()> {
        drop(self.reader);
        let mut err = String::new();
        if let Some(mut e) = self.child.stderr.take() {
            let _ = e.read_to_string(&mut err);
        }
        let st = self
            .child
            .wait()
            .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
        if !st.success() {
            return Err(DseeError::Ffmpeg(if err.is_empty() {
                format!("ffmpeg exit {st}")
            } else {
                err
            }));
        }
        Ok(())
    }
}

/// Decode entire file (tests / callers that need a Vec).
pub fn decode_s32le_stereo(path: &Path) -> Result<Vec<i32>> {
    decode_s32le_stereo_ar(path, None)
}

pub fn decode_s32le_stereo_ar(path: &Path, ar: Option<i32>) -> Result<Vec<i32>> {
    let mut stream = S32leDecodeStream::spawn_ar(path, ar.filter(|&r| r > 0))?;
    let mut pcm = Vec::new();
    let mut buf = vec![0i32; 64 * 1024];
    loop {
        let n = stream.read_samples(&mut buf)?;
        if n == 0 {
            break;
        }
        pcm.extend_from_slice(&buf[..n]);
    }
    stream.finish()?;
    Ok(pcm)
}
