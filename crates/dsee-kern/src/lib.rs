//! DSEE HX kernel crate.
//!
//! Default backend links the vendored C sources under `native/` (bit-exact with
//! `DSEE HX FINAL`). Pure Rust DSP modules will land behind `rust-dsp` with
//! golden tests against this reference.

mod batch;
mod error;
mod export;
mod ffi;
mod media;
mod prepare;
mod process;
mod wav;

pub use batch::{
    default_jobs, default_slots, is_batch_source, list_audio_files, plan_dir, plan_dir_repeat,
    plan_paths, plan_paths_rates, process_batch, BatchItem, BatchItemReport, BatchReport,
    TrackHopProgress,
};
pub use export::{default_output_path, export_extension, finalize_export, resample_wav_soxr};
pub use error::{DseeError, Result};
pub use media::{
    decode_s32le_stereo, detect_media_tools_in_app_dir, media_tools_info, probe,
    set_ffmpeg_override, set_ffprobe_override, MediaInfo, MediaToolsInfo,
};
pub use prepare::{
    analyze_track, clear_prepared, drop_prepared, ensure_prepared, file_resample_rate,
    process_file_cached, set_advanced_dsp, TrackAnalysis,
};
pub use process::{
    pipeline_slot_cost, process_file, process_file_with_progress, process_pcm,
    process_pcm_with_progress, ProcessReport, ProcessRequest, StageTiming,
};
pub use wav::{normalize_bits, write_wav};
