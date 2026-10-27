//! Finalize DSP PCM WAV into the user-facing file: prefer source container/codec,
//! copy tags / cover / lyrics, and choose a non-colliding name.

use crate::error::{DseeError, Result};
use crate::media::ffmpeg_path;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[cfg(windows)]
use std::os::windows::process::CommandExt;

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

fn source_ext(input: &Path) -> String {
    input
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("wav")
        .to_ascii_lowercase()
}

/// Extension for the export file. Hi-res rates force FLAC when the source
/// codec family cannot carry >48 kHz (MP3/AAC/Ogg/Opus/WMA).
pub fn export_extension(input: &Path, output_rate: i32) -> String {
    let ext = source_ext(input);
    match ext.as_str() {
        "mp3" | "aac" | "ogg" | "opus" | "wma" if output_rate > 48_000 => "flac".into(),
        "wave" => "wav".into(),
        "aif" => "aiff".into(),
        other => other.into(),
    }
}

fn dirs_equal(a: &Path, b: &Path) -> bool {
    match (fs::canonicalize(a), fs::canonicalize(b)) {
        (Ok(x), Ok(y)) => x == y,
        _ => a == b,
    }
}

/// Same basename+ext as the source when exporting elsewhere; `_dsee` suffix
/// only when the destination directory is the source directory (or would
/// overwrite the source file).
pub fn default_output_path(input: &Path, out_dir: &Path, output_rate: i32) -> PathBuf {
    let stem = input
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("out");
    let ext = export_extension(input, output_rate);
    let same_dir = input
        .parent()
        .map(|p| dirs_equal(p, out_dir))
        .unwrap_or(true);
    let mut name = if same_dir {
        format!("{stem}_dsee.{ext}")
    } else {
        format!("{stem}.{ext}")
    };
    let mut dest = out_dir.join(&name);
    // Never overwrite the source itself.
    if let (Ok(a), Ok(b)) = (fs::canonicalize(input), fs::canonicalize(&dest)) {
        if a == b {
            name = format!("{stem}_dsee.{ext}");
            dest = out_dir.join(&name);
        }
    } else if dest == input {
        name = format!("{stem}_dsee.{ext}");
        dest = out_dir.join(&name);
    }
    let _ = name;
    dest
}

fn sample_fmt_for_bits(bits: &str) -> &'static str {
    match bits.trim() {
        "16" => "s16",
        "32" => "s32",
        _ => "s32", // 24-bit packed in container; flac/alac use s32 internally often
    }
}

fn flac_sample_fmt(bits: &str) -> &'static str {
    match bits.trim() {
        "16" => "s16",
        "24" => "s32", // ffmpeg flac encodes 24 in s32 container
        _ => "s32",
    }
}

/// Re-encode / remux `pcm_wav` to `dest`, copying metadata (and cover) from `source`.
pub fn finalize_export(
    source: &Path,
    pcm_wav: &Path,
    dest: &Path,
    output_rate: i32,
    output_bits: &str,
) -> Result<()> {
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent).map_err(DseeError::Io)?;
    }
    let ffmpeg = ffmpeg_path()?;
    let ext = dest
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("wav")
        .to_ascii_lowercase();

    // Always take audio from processed WAV; metadata / attached pics from source.
    let mut args: Vec<String> = vec![
        "-y".into(),
        "-v".into(),
        "error".into(),
        "-i".into(),
        pcm_wav.display().to_string(),
        "-i".into(),
        source.display().to_string(),
        "-map".into(),
        "0:a:0".into(),
        "-map".into(),
        "1:v?".into(),
        "-map_metadata".into(),
        "1".into(),
        "-map_chapters".into(),
        "1".into(),
    ];

    match ext.as_str() {
        "wav" | "wave" => {
            let codec = match output_bits.trim() {
                "16" => "pcm_s16le",
                "32" => "pcm_s32le",
                _ => "pcm_s24le",
            };
            args.extend(["-c:a".into(), codec.into(), "-c:v".into(), "copy".into()]);
        }
        "flac" => {
            args.extend([
                "-c:a".into(),
                "flac".into(),
                "-sample_fmt".into(),
                flac_sample_fmt(output_bits).into(),
                "-c:v".into(),
                "copy".into(),
                "-disposition:v".into(),
                "attached_pic".into(),
            ]);
            if output_bits.trim() == "24" {
                args.extend(["-bits_per_raw_sample".into(), "24".into()]);
            }
        }
        "aiff" | "aif" => {
            let codec = match output_bits.trim() {
                "16" => "pcm_s16be",
                "32" => "pcm_s32be",
                _ => "pcm_s24be",
            };
            args.extend(["-c:a".into(), codec.into(), "-c:v".into(), "copy".into()]);
        }
        "m4a" | "mp4" | "alac" => {
            // Hi-res → ALAC; ≤48k keep AAC if source was lossy m4a (still OK as ALAC).
            args.extend([
                "-c:a".into(),
                "alac".into(),
                "-sample_fmt".into(),
                sample_fmt_for_bits(output_bits).into(),
                "-c:v".into(),
                "copy".into(),
                "-disposition:v".into(),
                "attached_pic".into(),
            ]);
        }
        "mp3" if output_rate <= 48_000 => {
            args.extend([
                "-c:a".into(),
                "libmp3lame".into(),
                "-b:a".into(),
                "320k".into(),
                "-write_id3v2".into(),
                "1".into(),
                "-id3v2_version".into(),
                "3".into(),
                "-c:v".into(),
                "copy".into(),
                "-disposition:v".into(),
                "attached_pic".into(),
            ]);
        }
        "ogg" | "opus" if output_rate <= 48_000 => {
            let codec = if ext == "opus" { "libopus" } else { "libvorbis" };
            args.extend([
                "-c:a".into(),
                codec.into(),
                "-c:v".into(),
                "copy".into(),
            ]);
        }
        // Fallback: FLAC (should not happen if export_extension matched).
        _ => {
            args.extend([
                "-c:a".into(),
                "flac".into(),
                "-sample_fmt".into(),
                flac_sample_fmt(output_bits).into(),
                "-c:v".into(),
                "copy".into(),
                "-disposition:v".into(),
                "attached_pic".into(),
            ]);
        }
    }

    args.push(dest.display().to_string());

    let out = command(&ffmpeg)
        .args(&args)
        .output()
        .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
    if !out.status.success() {
        let err = String::from_utf8_lossy(&out.stderr).trim().to_string();
        // Retry without video map if cover stream mapping failed.
        if err.contains("Stream map") || err.contains("matches no streams") || err.contains("Video")
        {
            return finalize_export_audio_only(source, pcm_wav, dest, output_rate, output_bits);
        }
        return Err(DseeError::Ffmpeg(if err.is_empty() {
            format!("ffmpeg export exit {}", out.status)
        } else {
            err
        }));
    }
    Ok(())
}

fn finalize_export_audio_only(
    source: &Path,
    pcm_wav: &Path,
    dest: &Path,
    output_rate: i32,
    output_bits: &str,
) -> Result<()> {
    let ffmpeg = ffmpeg_path()?;
    let ext = dest
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("wav")
        .to_ascii_lowercase();
    let mut args: Vec<String> = vec![
        "-y".into(),
        "-v".into(),
        "error".into(),
        "-i".into(),
        pcm_wav.display().to_string(),
        "-i".into(),
        source.display().to_string(),
        "-map".into(),
        "0:a:0".into(),
        "-map_metadata".into(),
        "1".into(),
        "-map_chapters".into(),
        "1".into(),
        "-vn".into(),
    ];
    match ext.as_str() {
        "wav" | "wave" => {
            let codec = match output_bits.trim() {
                "16" => "pcm_s16le",
                "32" => "pcm_s32le",
                _ => "pcm_s24le",
            };
            args.extend(["-c:a".into(), codec.into()]);
        }
        "flac" => {
            args.extend([
                "-c:a".into(),
                "flac".into(),
                "-sample_fmt".into(),
                flac_sample_fmt(output_bits).into(),
            ]);
            if output_bits.trim() == "24" {
                args.extend(["-bits_per_raw_sample".into(), "24".into()]);
            }
        }
        "aiff" | "aif" => {
            let codec = match output_bits.trim() {
                "16" => "pcm_s16be",
                "32" => "pcm_s32be",
                _ => "pcm_s24be",
            };
            args.extend(["-c:a".into(), codec.into()]);
        }
        "m4a" | "mp4" | "alac" => {
            args.extend([
                "-c:a".into(),
                "alac".into(),
                "-sample_fmt".into(),
                sample_fmt_for_bits(output_bits).into(),
            ]);
        }
        "mp3" if output_rate <= 48_000 => {
            args.extend([
                "-c:a".into(),
                "libmp3lame".into(),
                "-b:a".into(),
                "320k".into(),
                "-write_id3v2".into(),
                "1".into(),
                "-id3v2_version".into(),
                "3".into(),
            ]);
        }
        _ => {
            args.extend([
                "-c:a".into(),
                "flac".into(),
                "-sample_fmt".into(),
                flac_sample_fmt(output_bits).into(),
            ]);
        }
    }
    args.push(dest.display().to_string());
    let out = command(&ffmpeg)
        .args(&args)
        .output()
        .map_err(|e| DseeError::Ffmpeg(e.to_string()))?;
    if !out.status.success() {
        let err = String::from_utf8_lossy(&out.stderr).trim().to_string();
        return Err(DseeError::Ffmpeg(if err.is_empty() {
            format!("ffmpeg export exit {}", out.status)
        } else {
            err
        }));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn hi_res_mp3_becomes_flac() {
        assert_eq!(
            export_extension(Path::new(r"C:\a\song.mp3"), 176_400),
            "flac"
        );
        assert_eq!(export_extension(Path::new(r"C:\a\song.mp3"), 48_000), "mp3");
        assert_eq!(
            export_extension(Path::new(r"C:\a\song.flac"), 176_400),
            "flac"
        );
    }

    #[test]
    fn naming_same_dir_gets_dsee_suffix() {
        let input = PathBuf::from(r"C:\music\track.flac");
        let out = default_output_path(&input, Path::new(r"C:\music"), 88200);
        assert_eq!(out.file_name().unwrap(), "track_dsee.flac");
    }
}
