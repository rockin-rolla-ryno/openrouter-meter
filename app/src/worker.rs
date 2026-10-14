//! Background worker: keeps the Bluetooth link up, polls OpenRouter and
//! sends the meter whatever changed. Reports status to the tray.
use crate::link::{self, LinkError, Meter};
use crate::system;
use chrono::{Local, Utc};
use ormeter::api::{ApiError, ActivityRow, Client};
use ormeter::{config, daystate, log, payload};
use serde_json::Value;
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tokio::sync::mpsc;

#[derive(Debug, Clone, PartialEq)]
pub enum State {
    NeedsKey,
    KeyRejected,
    NoBluetooth,
    Searching,
    Pairing,
    NeedsPairing,
    WrongDevice(String),
    ApiOffline(String),
    Live,
}

#[derive(Debug, Clone)]
pub struct Status {
    pub state: State,
    pub summary: Option<String>, // "Balance $7.46 · today $0.03"
}

pub enum Command {
    RefreshNow,
}

const ACTIVITY_EVERY: Duration = Duration::from_secs(300);

pub async fn run(report: impl Fn(Status) + Send + 'static, mut commands: mpsc::UnboundedReceiver<Command>) {
    let client = Client::new();
    let (refresh_tx, mut refresh_rx) = mpsc::unbounded_channel::<()>();
    let mut meter: Option<Meter> = None;
    let mut sent: HashMap<&'static str, String> = HashMap::new();
    let mut activity: Option<(Instant, Vec<ActivityRow>)> = None;
    let mut last_summary: Option<String> = None;
    let mut link_retry_at = Instant::now();
    let mut link_backoff = Duration::from_secs(2);

    let say = |state: State, summary: &Option<String>| report(Status { state, summary: summary.clone() });

    loop {
        let settings = config::load();

        // ---- 1. Bluetooth link ------------------------------------------------
        if meter.is_none() && Instant::now() >= link_retry_at {
            say(State::Searching, &last_summary);
            match connect(&settings, refresh_tx.clone(), &report, &last_summary).await {
                Ok(m) => {
                    log!("connected to meter {}", m.address);
                    if settings.meter_address.as_deref() != Some(m.address.as_str()) {
                        let _ = config::save_meter_address(&m.address);
                    }
                    sent.clear();
                    meter = Some(m);
                    link_backoff = Duration::from_secs(2);
                }
                Err(e) => {
                    let state = match &e {
                        LinkError::NoAdapter => State::NoBluetooth,
                        LinkError::NeedsPairing => State::NeedsPairing,
                        LinkError::NotOurs(a) => State::WrongDevice(a.clone()),
                        _ => State::Searching,
                    };
                    if !matches!(e, LinkError::NotFound) {
                        log!("link: {e}");
                    }
                    say(state, &last_summary);
                    link_retry_at = Instant::now() + link_backoff;
                    link_backoff = (link_backoff * 2).min(Duration::from_secs(30));
                }
            }
        }

        // ---- 2. OpenRouter ----------------------------------------------------
        let mut outbox: Vec<(&'static str, Value)> = vec![("cfg", payload::settings_msg(&settings))];
        let mut state = State::Live;
        match settings.management_key.as_deref() {
            None => {
                state = State::NeedsKey;
                outbox.push(("err", payload::key_error()));
            }
            Some(key) => match poll(&client, key, &settings, &mut activity).await {
                Ok((msgs, summary)) => {
                    outbox.extend(msgs);
                    last_summary = Some(summary);
                }
                Err(ApiError::Auth) => {
                    state = State::KeyRejected;
                    outbox.push(("err", payload::key_error()));
                }
                Err(ApiError::Transient(e)) => {
                    log!("openrouter: {e}");
                    state = State::ApiOffline(e);
                }
            },
        }

        write_snapshot(&outbox);

        // ---- 3. Send what changed (core every time: it carries the clock) ----
        if let Some(m) = &meter {
            for (kind, msg) in &outbox {
                let text = msg.to_string();
                if *kind != "core" && sent.get(kind) == Some(&text) {
                    continue;
                }
                match tokio::task::block_in_place(|| m.send(text.as_bytes())) {
                    Ok(()) => {
                        sent.insert(kind, text);
                    }
                    Err(e) => {
                        log!("send {kind}: {e}");
                        if matches!(e, LinkError::NeedsPairing) {
                            state = State::NeedsPairing;
                        }
                        meter = None;
                        break;
                    }
                }
            }
        }
        let shown = if meter.is_none() && matches!(state, State::Live) { State::Searching } else { state };
        say(shown, &last_summary);

        // ---- 4. Wait for the next poll, a refresh request, or a link retry ----
        let wait = if meter.is_none() {
            Duration::from_secs(3)
        } else {
            Duration::from_secs(settings.poll_seconds)
        };
        tokio::select! {
            _ = tokio::time::sleep(wait) => {}
            Some(_) = refresh_rx.recv() => { log!("meter asked for a refresh"); }
            Some(Command::RefreshNow) = commands.recv() => {
                activity = None;
                link_retry_at = Instant::now();
            }
        }
    }
}

async fn connect(
    settings: &config::Settings,
    refresh: mpsc::UnboundedSender<()>,
    report: &(impl Fn(Status) + Send),
    summary: &Option<String>,
) -> Result<Meter, LinkError> {
    let saved = pinned_address(settings);
    let addr = tokio::task::spawn_blocking(move || link::locate(saved))
        .await
        .map_err(|e| LinkError::Other(e.to_string()))??;

    // Pair through Windows first if needed ("Just Works": no PIN).
    if !tokio::task::spawn_blocking(move || system::is_paired(addr)).await.unwrap_or(false) {
        report(Status { state: State::Pairing, summary: summary.clone() });
        log!("pairing with meter {}", link::format_address(addr));
        match tokio::task::spawn_blocking(move || system::pair(addr)).await {
            Ok(Ok(true)) => log!("paired"),
            Ok(Ok(false)) => return Err(LinkError::NeedsPairing),
            Ok(Err(e)) => {
                log!("pairing failed: {e}");
                return Err(LinkError::NeedsPairing);
            }
            Err(e) => return Err(LinkError::Other(e.to_string())),
        }
    }

    let r = refresh.clone();
    match tokio::task::spawn_blocking(move || link::open(addr, r)).await {
        Ok(Ok(m)) => Ok(m),
        // A refused pairing is reported, never "fixed" by unpairing on our own:
        // the user can choose "Re-pair meter" in the tray menu.
        Ok(Err(e)) => Err(e),
        Err(e) => Err(LinkError::Other(e.to_string())),
    }
}

/// The meter this app is bound to: OPENROUTER_BLE_ADDRESS (env) wins over
/// meter_address in settings.txt. None = not paired with a meter yet.
pub fn pinned_address(settings: &config::Settings) -> Option<u64> {
    std::env::var("OPENROUTER_BLE_ADDRESS")
        .ok()
        .and_then(|v| system::parse_address(&v))
        .or_else(|| settings.meter_address.as_deref().and_then(system::parse_address))
}

/// One OpenRouter poll -> the meter messages + a one-line tray summary.
async fn poll(
    client: &Client,
    key: &str,
    settings: &config::Settings,
    activity: &mut Option<(Instant, Vec<ActivityRow>)>,
) -> Result<(Vec<(&'static str, Value)>, String), ApiError> {
    let credits = client.credits(key).await?;
    let keys = match client.keys(key).await {
        Ok(k) => Some(k),
        Err(ApiError::Auth) => return Err(ApiError::Auth),
        Err(e) => {
            log!("keys: {e}");
            None
        }
    };
    let today = Utc::now().date_naive();
    let fresh = activity.as_ref().map(|(t, _)| t.elapsed() < ACTIVITY_EVERY).unwrap_or(false);
    if !fresh {
        match client.activity(key).await {
            Ok(rows) => *activity = Some((Instant::now(), rows)),
            Err(ApiError::Auth) => return Err(ApiError::Auth),
            Err(e) => log!("activity: {e}"),
        }
    }
    let summary = activity.as_ref().map(|(_, rows)| payload::summarize(rows, today));

    let live = daystate::live_day_cents(
        &ormeter::data_dir().join("day_state.json"),
        credits.total_usage,
        &today.to_string(),
    );
    let clock_24h = match settings.clock {
        config::Clock::H24 => true,
        config::Clock::H12 => false,
        _ => {
            #[cfg(windows)]
            { system::windows_uses_24h() }
            #[cfg(not(windows))]
            { true }
        }
    };
    let now_local = payload::clock_enabled(settings).then(|| Local::now().naive_local().and_utc().timestamp());
    let p = payload::Poll {
        credits: &credits,
        activity: summary.as_ref(),
        keys: keys.as_deref(),
        live_day_cents: live,
        today,
        now_local,
        clock_24h,
    };
    let core = payload::core(&p);
    let day = payload::spend_today(&p);
    let mut out: Vec<(&'static str, Value)> = Vec::new();
    if let Some(s) = &summary {
        out.push(("hist", payload::hist(s, today, day)));
        out.push(("models", payload::models(s)));
        out.push(("prov", payload::providers(s)));
        out.push(("tok", payload::tokens(s)));
    }
    if let Some(k) = &keys {
        out.push(("keys", payload::keys(k)));
    }
    let line = format!(
        "Balance {} \u{00B7} today {}",
        money(core["bal"].as_i64().unwrap_or(0)),
        money(day)
    );
    out.push(("core", core));
    Ok((out, line))
}

/// Save the messages from the latest poll to `last_poll.json` (no secrets) —
/// handy for troubleshooting.
fn write_snapshot(outbox: &[(&'static str, Value)]) {
    let list: Vec<&Value> = outbox.iter().map(|(_, m)| m).collect();
    if let Ok(text) = serde_json::to_string_pretty(&list) {
        let _ = std::fs::write(ormeter::data_dir().join("last_poll.json"), text);
    }
}

/// `OpenRouterMeter.exe --once`: one OpenRouter poll, snapshot written, exit.
pub async fn poll_once() -> Result<(), String> {
    let settings = config::load();
    let key = settings.management_key.clone().ok_or("no management_key in settings.txt")?;
    let mut activity = None;
    let (mut msgs, summary) = poll(&Client::new(), &key, &settings, &mut activity)
        .await
        .map_err(|e| e.to_string())?;
    msgs.insert(0, ("cfg", payload::settings_msg(&settings)));
    write_snapshot(&msgs);
    log!("--once: {summary}");
    Ok(())
}

pub fn money(cents: i64) -> String {
    let sign = if cents < 0 { "-" } else { "" };
    let c = cents.abs();
    format!("{sign}${}.{:02}", c / 100, c % 100)
}
