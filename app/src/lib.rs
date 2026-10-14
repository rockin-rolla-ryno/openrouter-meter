//! OpenRouter Meter — PC companion app (library half).
//!
//! The platform-independent pieces live here so they can be unit-tested on
//! any OS: settings, the OpenRouter API client, and the builders that turn
//! API data into the small JSON messages the meter understands.
pub mod api;
pub mod config;
pub mod daystate;
pub mod logging;
pub mod payload;

/// Bluetooth identifiers shared with the firmware (firmware/src/link/link.h).
pub mod ids {
    use uuid::Uuid;
    pub const SERVICE: Uuid = Uuid::from_u128(0x589b0001_eb3f_4774_98f5_45672a450cf3);
    pub const DATA: Uuid = Uuid::from_u128(0x589b0002_eb3f_4774_98f5_45672a450cf3);
    pub const REQUEST: Uuid = Uuid::from_u128(0x589b0003_eb3f_4774_98f5_45672a450cf3);
    pub const DEVICE_NAME: &str = "OpenRouter Meter";
}

/// Where the app keeps its settings, log and state:
/// `%APPDATA%\OpenRouterMeter` on Windows.
pub fn data_dir() -> std::path::PathBuf {
    let base = std::env::var_os("APPDATA")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::env::temp_dir());
    base.join("OpenRouterMeter")
}
