//! Plain-text settings file (`settings.txt`), written with comments so it can
//! be edited in Notepad. Re-read on every poll, so edits apply within seconds.
use std::fs;
use std::path::{Path, PathBuf};

pub const SCREEN_NAMES: [&str; 6] = ["credits", "routes", "spend", "tokens", "providers", "keys"];

#[derive(Debug, Clone, PartialEq)]
pub enum Clock {
    Off,
    Auto,
    H12,
    H24,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    pub management_key: Option<String>,
    pub low_balance_alert_cents: i64,
    pub daily_budget_alert_cents: i64,
    pub clock: Clock,
    pub screens_mask: u8, // 0 = all
    pub dim_after_minutes: u8,
    pub poll_seconds: u64,
    pub meter_address: Option<String>,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            management_key: None,
            low_balance_alert_cents: 500,
            daily_budget_alert_cents: 0,
            clock: Clock::Auto,
            screens_mask: 0,
            dim_after_minutes: 0,
            poll_seconds: 20,
            meter_address: None,
        }
    }
}

pub const TEMPLATE: &str = r#"# ==========================================================================
#  OpenRouter Meter — settings
#  Edit, then save this file (Ctrl+S). The app picks up changes by itself.
# ==========================================================================

# 1) Your OpenRouter MANAGEMENT key (required).
#    Create one at https://openrouter.ai/settings/management-keys
#    It can only READ your usage — it cannot spend credits.
#    Paste it after the = sign, e.g.  management_key = sk-or-v1-abc123...
management_key =

# 2) Alerts — the meter lights up and shows a banner. Dollars; 0 turns it off.
low_balance_alert = 5.00
daily_budget_alert = 0

# 3) Clock in the top corner: auto (follows Windows), 24h, 12h or off.
clock = auto

# 4) Which screens to show (tap the meter to move between them).
#    Any of: credits, routes, spend, tokens, providers, keys   — or: all
screens = all

# 5) Dim the meter after this many minutes without a touch (0 = never).
dim_after_minutes = 0

# 6) How often to refresh, in seconds (10 or more).
poll_seconds = 20

# Filled in automatically after the first connection. Delete it to pair a
# different meter.
meter_address =
"#;

pub fn settings_path() -> PathBuf {
    crate::data_dir().join("settings.txt")
}

/// Create the settings file from the template if it doesn't exist yet.
pub fn ensure_file(path: &Path) -> std::io::Result<bool> {
    if path.exists() {
        return Ok(false);
    }
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    fs::write(path, TEMPLATE.replace('\n', "\r\n"))?;
    Ok(true)
}

fn dollars_to_cents(v: &str) -> Option<i64> {
    let v = v.trim().trim_start_matches('$');
    if v.is_empty() {
        return Some(0);
    }
    v.parse::<f64>().ok().filter(|x| x.is_finite() && *x >= 0.0).map(|x| (x * 100.0).round() as i64)
}

pub fn parse(text: &str) -> Settings {
    let mut s = Settings::default();
    for raw in text.lines() {
        let line = raw.trim();
        if line.starts_with('#') || !line.contains('=') {
            continue;
        }
        let (k, v) = line.split_once('=').unwrap();
        let key = k.trim().to_ascii_lowercase();
        // Inline comments only after whitespace, so keys containing '#' survive.
        let v = match v.find(" #") {
            Some(i) => &v[..i],
            None => v,
        }
        .trim();
        match key.as_str() {
            "management_key" => {
                let k = v.trim_matches('"').trim();
                s.management_key = if k.is_empty() { None } else { Some(k.to_string()) };
            }
            "low_balance_alert" => {
                if let Some(c) = dollars_to_cents(v) {
                    s.low_balance_alert_cents = c
                }
            }
            "daily_budget_alert" => {
                if let Some(c) = dollars_to_cents(v) {
                    s.daily_budget_alert_cents = c
                }
            }
            "clock" => {
                s.clock = match v.to_ascii_lowercase().as_str() {
                    "off" | "no" | "none" => Clock::Off,
                    "12" | "12h" => Clock::H12,
                    "24" | "24h" => Clock::H24,
                    _ => Clock::Auto,
                }
            }
            "screens" => s.screens_mask = parse_screens(v),
            "dim_after_minutes" => s.dim_after_minutes = v.parse::<u32>().map(|m| m.min(240) as u8).unwrap_or(0),
            "poll_seconds" => s.poll_seconds = v.parse::<u64>().map(|p| p.clamp(10, 3600)).unwrap_or(20),
            "meter_address" => {
                s.meter_address = if v.is_empty() { None } else { Some(v.to_ascii_uppercase()) }
            }
            _ => {}
        }
    }
    if let Ok(k) = std::env::var("OPENROUTER_MANAGEMENT_KEY") {
        if !k.trim().is_empty() {
            s.management_key = Some(k.trim().to_string());
        }
    }
    s
}

fn parse_screens(v: &str) -> u8 {
    let v = v.to_ascii_lowercase();
    if v.trim().is_empty() || v.trim() == "all" {
        return 0;
    }
    let mut mask = 0u8;
    for part in v.split(|c: char| c == ',' || c.is_whitespace()).filter(|p| !p.is_empty()) {
        if let Some(i) = SCREEN_NAMES.iter().position(|n| *n == part) {
            mask |= 1 << i;
        }
    }
    mask
}

pub fn load() -> Settings {
    let path = settings_path();
    let _ = ensure_file(&path);
    fs::read_to_string(&path).map(|t| parse(&t)).unwrap_or_default()
}

/// Remember the meter we paired with, editing only the `meter_address` line.
pub fn save_meter_address(addr: &str) -> std::io::Result<()> {
    let path = settings_path();
    let text = fs::read_to_string(&path).unwrap_or_else(|_| TEMPLATE.to_string());
    let mut found = false;
    let mut out: Vec<String> = text
        .lines()
        .map(|l| {
            if l.trim_start().to_ascii_lowercase().starts_with("meter_address") && l.contains('=') {
                found = true;
                format!("meter_address = {addr}")
            } else {
                l.to_string()
            }
        })
        .collect();
    if !found {
        out.push(format!("meter_address = {addr}"));
    }
    fs::write(&path, out.join("\r\n") + "\r\n")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn template_parses_to_defaults() {
        let s = parse(TEMPLATE);
        assert_eq!(s.management_key, None);
        assert_eq!(s.low_balance_alert_cents, 500);
        assert_eq!(s.daily_budget_alert_cents, 0);
        assert_eq!(s.clock, Clock::Auto);
        assert_eq!(s.screens_mask, 0);
        assert_eq!(s.poll_seconds, 20);
    }

    #[test]
    fn values_parse() {
        let s = parse(
            "management_key = sk-or-v1-abc#def\nlow_balance_alert = $2.50 # note\nscreens = credits, keys\nclock=12h\npoll_seconds=3\nmeter_address = d4:05:92:b6:a4:9e",
        );
        assert_eq!(s.management_key.as_deref(), Some("sk-or-v1-abc#def"));
        assert_eq!(s.low_balance_alert_cents, 250);
        assert_eq!(s.screens_mask, 0b100001);
        assert_eq!(s.clock, Clock::H12);
        assert_eq!(s.poll_seconds, 10);
        assert_eq!(s.meter_address.as_deref(), Some("D4:05:92:B6:A4:9E"));
    }
}
