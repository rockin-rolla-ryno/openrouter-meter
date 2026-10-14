//! Tiny file logger: `%APPDATA%\OpenRouterMeter\meter.log`, trimmed when it
//! passes 1 MB. Keys are never logged.
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::sync::Mutex;

static LOCK: Mutex<()> = Mutex::new(());

pub fn log_path() -> std::path::PathBuf {
    crate::data_dir().join("meter.log")
}

pub fn log(msg: impl AsRef<str>) {
    let _g = LOCK.lock();
    let path = log_path();
    if let Some(d) = path.parent() {
        let _ = fs::create_dir_all(d);
    }
    if fs::metadata(&path).map(|m| m.len() > 1_000_000).unwrap_or(false) {
        let _ = fs::rename(&path, path.with_extension("old.log"));
    }
    let line = format!("{} {}\n", chrono::Local::now().format("%Y-%m-%d %H:%M:%S"), msg.as_ref());
    if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(&path) {
        let _ = f.write_all(line.as_bytes());
    }
    #[cfg(debug_assertions)]
    eprint!("{line}");
}

#[macro_export]
macro_rules! log {
    ($($t:tt)*) => { $crate::logging::log(format!($($t)*)) };
}
