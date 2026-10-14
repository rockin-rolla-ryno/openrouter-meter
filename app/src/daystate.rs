//! Fallback "spend today" from the rise in lifetime usage since the first
//! reading of the current UTC day (covers usage that isn't tied to a key,
//! e.g. the web chatroom). Persisted so restarts keep the baseline.
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Serialize, Deserialize, Debug, PartialEq)]
struct State {
    date: String,
    baseline_usd: f64,
}

pub fn live_day_cents(path: &Path, total_usage_usd: f64, today_iso: &str) -> i64 {
    let state: Option<State> = fs::read_to_string(path).ok().and_then(|t| serde_json::from_str(&t).ok());
    let baseline = match state {
        Some(s) if s.date == today_iso => s.baseline_usd,
        _ => {
            let s = State { date: today_iso.to_string(), baseline_usd: total_usage_usd };
            if let Some(dir) = path.parent() {
                let _ = fs::create_dir_all(dir);
            }
            let _ = fs::write(path, serde_json::to_string(&s).unwrap_or_default());
            total_usage_usd
        }
    };
    (((total_usage_usd - baseline).max(0.0)) * 100.0).round() as i64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn baseline_then_delta_then_new_day() {
        let dir = std::env::temp_dir().join(format!("orm-test-{}", std::process::id()));
        let p = dir.join("day.json");
        let _ = fs::remove_file(&p);
        assert_eq!(live_day_cents(&p, 27.50, "2026-10-14"), 0);
        assert_eq!(live_day_cents(&p, 27.53, "2026-10-14"), 3);
        assert_eq!(live_day_cents(&p, 27.60, "2026-10-15"), 0);
        let _ = fs::remove_dir_all(dir);
    }
}
