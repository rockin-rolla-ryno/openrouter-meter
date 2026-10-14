//! OpenRouter Meter — Windows tray app.
//!
//! First run from anywhere (e.g. Downloads) installs a copy to
//! `%LOCALAPPDATA%\Programs\OpenRouter Meter`, turns on "Start with Windows",
//! opens the settings file for the OpenRouter key, and starts in the tray.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod art;
mod link;
mod system;
mod worker;

use ormeter::{config, log, logging};
use tao::event::{Event, StartCause};
use tao::event_loop::{ControlFlow, EventLoopBuilder};
use tokio::sync::mpsc;
use tray_icon::menu::{CheckMenuItem, Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tray_icon::{TrayIcon, TrayIconBuilder};
use worker::{Command, State, Status};

const APP: &str = "OpenRouter Meter";
const KEY_URL: &str = "https://openrouter.ai/settings/management-keys";

enum UserEvent {
    Status(Status),
    Menu(MenuEvent),
}

fn main() {
    let exe = std::env::current_exe().unwrap_or_default();
    if std::env::args().any(|a| a == "--once") {
        let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().expect("runtime");
        if let Err(e) = rt.block_on(worker::poll_once()) {
            log!("--once failed: {e}");
            std::process::exit(1);
        }
        return;
    }
    // Debug builds and `--portable` run in place instead of installing.
    let portable = cfg!(debug_assertions) || std::env::args().any(|a| a == "--portable");

    #[cfg(windows)]
    if !portable && !system::same_file(&exe, &system::install_path()) {
        install_and_launch(&exe);
        return;
    }

    #[cfg(windows)]
    if !system::claim_single_instance() {
        system::message(APP, "OpenRouter Meter is already running.\n\nLook for its icon near the clock (you may need to click the ^ arrow).", false);
        return;
    }

    let settings_path = config::settings_path();
    let first_run = config::ensure_file(&settings_path).unwrap_or(false);
    log!("OpenRouter Meter {} starting", env!("CARGO_PKG_VERSION"));
    if first_run {
        #[cfg(windows)]
        let _ = system::set_autostart(true, &exe);
    }
    if config::load().management_key.is_none() {
        #[cfg(windows)]
        system::message(
            APP,
            "OpenRouter Meter is running — its icon is near the clock.\n\n\
             One last step: Notepad will open your settings. Paste your OpenRouter \
             MANAGEMENT key after \"management_key =\", then save (Ctrl+S).\n\n\
             Need a key? Right-click the tray icon and choose \"Get a management key\".",
            false,
        );
        system::open_in_notepad(&settings_path);
    }

    let event_loop = EventLoopBuilder::<UserEvent>::with_user_event().build();
    let proxy = event_loop.create_proxy();
    let menu_proxy = proxy.clone();
    MenuEvent::set_event_handler(Some(move |e| {
        let _ = menu_proxy.send_event(UserEvent::Menu(e));
    }));

    let (cmd_tx, cmd_rx) = mpsc::unbounded_channel::<Command>();
    std::thread::spawn(move || {
        let rt = tokio::runtime::Builder::new_multi_thread().enable_all().build().expect("runtime");
        rt.block_on(worker::run(
            move |s| {
                let _ = proxy.send_event(UserEvent::Status(s));
            },
            cmd_rx,
        ));
    });

    let status_item = MenuItem::new("Starting\u{2026}", false, None);
    let summary_item = MenuItem::new("Balance \u{2014}", false, None);
    let refresh_item = MenuItem::new("Refresh now", true, None);
    let settings_item = MenuItem::new("Settings\u{2026}", true, None);
    let key_item = MenuItem::new("Get a management key\u{2026}", true, None);
    #[cfg(windows)]
    let autostart_on = system::autostart_enabled();
    #[cfg(not(windows))]
    let autostart_on = false;
    let autostart_item = CheckMenuItem::new("Start with Windows", true, autostart_on, None);
    let log_item = MenuItem::new("Open log", true, None);
    let about_item = MenuItem::new("About", true, None);
    let uninstall_item = MenuItem::new("Uninstall\u{2026}", true, None);
    let quit_item = MenuItem::new("Quit", true, None);

    let menu = Menu::new();
    let _ = menu.append_items(&[
        &status_item,
        &summary_item,
        &PredefinedMenuItem::separator(),
        &refresh_item,
        &settings_item,
        &key_item,
        &autostart_item,
        &log_item,
        &PredefinedMenuItem::separator(),
        &about_item,
        &uninstall_item,
        &quit_item,
    ]);

    let mut tray: Option<TrayIcon> = None;
    let mut menu_slot = Some(menu);
    let mut last_state: Option<State> = None;

    event_loop.run(move |event, _, flow| {
        *flow = ControlFlow::Wait;
        match event {
            Event::NewEvents(StartCause::Init) => {
                let built = TrayIconBuilder::new()
                    .with_menu(Box::new(menu_slot.take().expect("menu")))
                    .with_tooltip(APP)
                    .with_icon(art::icon(art::GREY))
                    .build();
                match built {
                    Ok(t) => tray = Some(t),
                    Err(e) => log!("tray icon failed: {e}"),
                }
            }
            Event::UserEvent(UserEvent::Status(s)) => {
                let (text, color) = describe(&s.state);
                status_item.set_text(text);
                summary_item.set_text(s.summary.clone().unwrap_or_else(|| "Balance \u{2014}".into()));
                if let Some(t) = &tray {
                    if last_state.as_ref() != Some(&s.state) {
                        let _ = t.set_icon(Some(art::icon(color)));
                    }
                    let tip = match &s.summary {
                        Some(sum) => format!("{APP}\n{text}\n{sum}"),
                        None => format!("{APP}\n{text}"),
                    };
                    let _ = t.set_tooltip(Some(tip));
                }
                if last_state.as_ref() != Some(&s.state) {
                    log!("status: {text}");
                }
                last_state = Some(s.state);
            }
            Event::UserEvent(UserEvent::Menu(m)) => {
                let id = m.id;
                if id == *refresh_item.id() {
                    let _ = cmd_tx.send(Command::RefreshNow);
                } else if id == *settings_item.id() {
                    system::open_in_notepad(&config::settings_path());
                } else if id == *key_item.id() {
                    system::open_url(KEY_URL);
                } else if id == *autostart_item.id() {
                    #[cfg(windows)]
                    {
                        let on = autostart_item.is_checked();
                        if let Err(e) = system::set_autostart(on, &exe) {
                            log!("autostart: {e}");
                        }
                    }
                } else if id == *log_item.id() {
                    system::open_in_notepad(&logging::log_path());
                } else if id == *about_item.id() {
                    #[cfg(windows)]
                    system::message(
                        APP,
                        &format!(
                            "OpenRouter Meter {}\n\nShows your OpenRouter balance and usage on the \
                             OpenRouter Meter desk display.\n\nFree for personal, non-commercial use \
                             (PolyForm Noncommercial 1.0.0).\nNot affiliated with OpenRouter.",
                            env!("CARGO_PKG_VERSION")
                        ),
                        false,
                    );
                } else if id == *uninstall_item.id() {
                    #[cfg(windows)]
                    if system::message(APP, "Remove OpenRouter Meter from this PC?\n\nThis stops it starting with Windows and deletes the app. Your settings file is kept.", true) {
                        uninstall(&exe);
                        tray = None;
                        *flow = ControlFlow::Exit;
                    }
                } else if id == *quit_item.id() {
                    tray = None;
                    *flow = ControlFlow::Exit;
                }
            }
            _ => {}
        }
    });
}

fn describe(s: &State) -> (&'static str, [u8; 3]) {
    match s {
        State::Live => ("Live \u{2014} meter connected", art::MINT),
        State::Searching => ("Looking for your meter\u{2026}", art::AMBER),
        State::Pairing => ("Pairing with your meter\u{2026}", art::AMBER),
        State::NeedsPairing => ("Pairing needed: hold the meter's PWR button 3 s, then release", art::AMBER),
        State::NeedsKey => ("Add your OpenRouter key in Settings", art::AMBER),
        State::KeyRejected => ("OpenRouter rejected the key \u{2014} check Settings", art::CORAL),
        State::NoBluetooth => ("Bluetooth is off or missing", art::CORAL),
        State::ApiOffline(_) => ("Can't reach OpenRouter \u{2014} retrying", art::AMBER),
    }
}

/// Copy this exe into the install folder, keep autostart pointing at it and
/// launch the installed copy.
#[cfg(windows)]
fn install_and_launch(exe: &std::path::Path) {
    let dest = system::install_path();
    let first_install = !dest.exists();
    if let Some(dir) = dest.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    let mut copied = false;
    for _ in 0..10 {
        if std::fs::copy(exe, &dest).is_ok() {
            copied = true;
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(300));
    }
    if !copied {
        system::message(
            APP,
            "OpenRouter Meter is already running, so it can't be updated.\n\n\
             Right-click its tray icon (near the clock), choose Quit, then run this file again.",
            false,
        );
        return;
    }
    if first_install || system::autostart_enabled() {
        let _ = system::set_autostart(true, &dest);
    }
    let _ = std::process::Command::new(&dest).spawn();
}

#[cfg(windows)]
fn uninstall(exe: &std::path::Path) {
    let _ = system::set_autostart(false, exe);
    if let Some(dir) = system::install_path().parent() {
        // The running exe can't delete itself: remove the folder a moment after we exit.
        let cmd = format!("ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{}\"", dir.display());
        let _ = std::process::Command::new("cmd.exe").args(["/C", &cmd]).spawn();
    }
}
