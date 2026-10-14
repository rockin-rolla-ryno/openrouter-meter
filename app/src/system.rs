//! Windows integration: pairing, single instance, start with Windows,
//! self-install, message boxes, clock format.
use std::path::{Path, PathBuf};

const RUN_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
const RUN_VALUE: &str = "OpenRouter Meter";

/// Pair with the meter through Windows ("Just Works" — no PIN). Returns true
/// when paired (or already paired). Blocking: call from a blocking thread.
///
/// Tries the basic pairing call first, then Windows' custom pairing with an
/// automatic "confirm" (some adapters only allow Just Works pairing that way).
#[cfg(windows)]
pub fn pair(address: u64) -> Result<bool, String> {
    use windows::Devices::Bluetooth::BluetoothLEDevice;
    use windows::Devices::Enumeration::{
        DeviceInformation, DeviceInformationCustomPairing, DevicePairingKinds, DevicePairingProtectionLevel,
        DevicePairingRequestedEventArgs, DevicePairingResultStatus,
    };
    use windows::Foundation::TypedEventHandler;
    use ormeter::log;

    let dev = BluetoothLEDevice::FromBluetoothAddressAsync(address)
        .and_then(|op| op.join())
        .map_err(|e| format!("device lookup: {e}"))?;
    // Fresh DeviceInformation (the cached one can be stale right after an unpair).
    let info = dev
        .DeviceId()
        .and_then(|id| DeviceInformation::CreateFromIdAsync(&id))
        .and_then(|op| op.join())
        .or_else(|_| dev.DeviceInformation())
        .map_err(|e| format!("device info: {e}"))?;
    let pairing = info.Pairing().map_err(|e| format!("pairing info: {e}"))?;
    if pairing.IsPaired().unwrap_or(false) {
        return Ok(true);
    }
    let ok = |s: DevicePairingResultStatus| s == DevicePairingResultStatus::Paired || s == DevicePairingResultStatus::AlreadyPaired;

    match pairing
        .PairWithProtectionLevelAsync(DevicePairingProtectionLevel::Encryption)
        .and_then(|op| op.join())
        .and_then(|r| r.Status())
    {
        Ok(s) if ok(s) => return Ok(true),
        Ok(s) => log!("basic pairing status {}", s.0),
        Err(e) => log!("basic pairing: {e}"),
    }

    let custom: DeviceInformationCustomPairing = pairing.Custom().map_err(|e| format!("custom pairing: {e}"))?;
    let handler = TypedEventHandler::<DeviceInformationCustomPairing, DevicePairingRequestedEventArgs>::new(|_, args| {
        if let Some(a) = args.as_ref() {
            let _ = a.Accept(); // Just Works / confirm-only: nothing to compare
        }
        Ok(())
    });
    let token = custom.PairingRequested(&handler).map_err(|e| e.to_string())?;
    let result = custom
        .PairWithProtectionLevelAsync(DevicePairingKinds::ConfirmOnly, DevicePairingProtectionLevel::Encryption)
        .and_then(|op| op.join())
        .and_then(|r| r.Status());
    let _ = custom.RemovePairingRequested(token);
    match result {
        Ok(s) if ok(s) => Ok(true),
        Ok(s) => {
            log!("custom pairing status {}", s.0);
            Ok(false)
        }
        Err(e) => Err(format!("custom pairing: {e}")),
    }
}

#[cfg(windows)]
pub fn is_paired(address: u64) -> bool {
    use windows::Devices::Bluetooth::BluetoothLEDevice;
    BluetoothLEDevice::FromBluetoothAddressAsync(address)
        .and_then(|op| op.join())
        .and_then(|d| d.DeviceInformation())
        .and_then(|i| i.Pairing())
        .and_then(|p| p.IsPaired())
        .unwrap_or(false)
}

/// Forget a Windows pairing (used to recover from a stale pairing, e.g.
/// after a firmware upgrade changed the meter's Bluetooth layout).
#[cfg(windows)]
pub fn unpair(address: u64) -> Result<(), String> {
    use windows::Devices::Bluetooth::BluetoothLEDevice;
    let pairing = BluetoothLEDevice::FromBluetoothAddressAsync(address)
        .and_then(|op| op.join())
        .and_then(|d| d.DeviceInformation())
        .and_then(|i| i.Pairing())
        .map_err(|e| format!("pairing info: {e}"))?;
    if !pairing.IsPaired().unwrap_or(false) {
        return Ok(());
    }
    pairing.UnpairAsync().and_then(|op| op.join()).map(|_| ()).map_err(|e| format!("unpair: {e}"))
}

pub fn parse_address(text: &str) -> Option<u64> {
    let hex: String = text.chars().filter(|c| c.is_ascii_hexdigit()).collect();
    if hex.len() != 12 { return None; }
    u64::from_str_radix(&hex, 16).ok()
}

/// Hold a named mutex for the life of the process. False = already running.
#[cfg(windows)]
pub fn claim_single_instance() -> bool {
    use windows::core::w;
    use windows::Win32::Foundation::{GetLastError, ERROR_ALREADY_EXISTS};
    use windows::Win32::System::Threading::CreateMutexW;
    unsafe {
        match CreateMutexW(None, true, w!("Local\\OpenRouterMeter-app")) {
            // The handle is intentionally never closed: it lives as long as the process.
            Ok(_handle) => GetLastError() != ERROR_ALREADY_EXISTS,
            Err(_) => true,
        }
    }
}

#[cfg(windows)]
pub fn message(title: &str, text: &str, yes_no: bool) -> bool {
    use windows::core::HSTRING;
    use windows::Win32::UI::WindowsAndMessaging::{MessageBoxW, IDYES, MB_ICONINFORMATION, MB_OK, MB_YESNO, MB_ICONQUESTION};
    let style = if yes_no { MB_YESNO | MB_ICONQUESTION } else { MB_OK | MB_ICONINFORMATION };
    unsafe { MessageBoxW(None, &HSTRING::from(text), &HSTRING::from(title), style) == IDYES }
}

/// 24-hour clock per Windows' regional settings.
#[cfg(windows)]
pub fn windows_uses_24h() -> bool {
    use winreg::{enums::HKEY_CURRENT_USER, RegKey};
    RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey(r"Control Panel\International")
        .and_then(|k| k.get_value::<String, _>("iTime"))
        .map(|v| v.trim() == "1")
        .unwrap_or(true)
}

// ---------------------------------------------------------------------------
// Start with Windows: a per-user Task Scheduler task (no admin needed).
//
// The old HKCU\...\Run entry only fires on a fresh interactive logon and is
// skipped by some Fast Startup / resume paths. The task instead:
//   - starts 30 s after sign-in (lets the Bluetooth stack and login settle),
//   - re-checks every 5 minutes and relaunches the app if it isn't running
//     (MultipleInstancesPolicy=IgnoreNew makes this free while it runs),
//   - restarts on failure (3 tries, 1 minute apart),
//   - runs on battery and has no execution time limit.
// ---------------------------------------------------------------------------
pub const TASK_NAME: &str = "OpenRouterMeter";
pub const AUTOSTART_ARG: &str = "--autostart";

fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

/// The task definition (Task Scheduler schema 1.4).
pub fn task_xml(exe: &Path, user: &str) -> String {
    let exe = xml_escape(&exe.display().to_string());
    let user = xml_escape(user);
    format!(
        r#"<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Description>Starts the OpenRouter Meter app when you sign in and keeps it running.</Description>
    <URI>\{TASK_NAME}</URI>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>{user}</UserId>
      <Delay>PT30S</Delay>
      <Repetition>
        <Interval>PT5M</Interval>
        <StopAtDurationEnd>false</StopAtDurationEnd>
      </Repetition>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>{user}</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>true</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>6</Priority>
    <RestartOnFailure>
      <Interval>PT1M</Interval>
      <Count>3</Count>
    </RestartOnFailure>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>"{exe}"</Command>
      <Arguments>{AUTOSTART_ARG}</Arguments>
    </Exec>
  </Actions>
</Task>
"#
    )
}

#[cfg(windows)]
fn schtasks(args: &[&str]) -> std::io::Result<std::process::Output> {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    std::process::Command::new("schtasks.exe").args(args).creation_flags(CREATE_NO_WINDOW).output()
}

#[cfg(windows)]
fn current_user() -> String {
    let user = std::env::var("USERNAME").unwrap_or_default();
    match std::env::var("USERDOMAIN") {
        Ok(d) if !d.is_empty() => format!("{d}\\{user}"),
        _ => user,
    }
}

#[cfg(windows)]
pub fn autostart_enabled() -> bool {
    schtasks(&["/Query", "/TN", TASK_NAME]).map(|o| o.status.success()).unwrap_or(false)
}

/// Legacy HKCU\...\Run entry written by earlier versions.
#[cfg(windows)]
fn legacy_run_entry() -> bool {
    use winreg::{enums::HKEY_CURRENT_USER, RegKey};
    RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey(RUN_KEY)
        .and_then(|k| k.get_value::<String, _>(RUN_VALUE))
        .is_ok()
}

#[cfg(windows)]
fn remove_legacy_run_entry() {
    use winreg::{enums::*, RegKey};
    if let Ok(key) = RegKey::predef(HKEY_CURRENT_USER).open_subkey_with_flags(RUN_KEY, KEY_SET_VALUE) {
        let _ = key.delete_value(RUN_VALUE);
    }
}

#[cfg(windows)]
pub fn set_autostart(on: bool, exe: &Path) -> std::io::Result<()> {
    remove_legacy_run_entry();
    if !on {
        if autostart_enabled() {
            let out = schtasks(&["/Delete", "/TN", TASK_NAME, "/F"])?;
            if !out.status.success() {
                return Err(std::io::Error::other(String::from_utf8_lossy(&out.stderr).trim().to_string()));
            }
        }
        return Ok(());
    }
    // schtasks wants the XML as UTF-16 (with BOM).
    let xml = task_xml(exe, &current_user());
    let mut bytes = vec![0xFF, 0xFE];
    for u in xml.encode_utf16() {
        bytes.extend_from_slice(&u.to_le_bytes());
    }
    let file = std::env::temp_dir().join("OpenRouterMeter-task.xml");
    std::fs::write(&file, bytes)?;
    let path = file.display().to_string();
    let out = schtasks(&["/Create", "/TN", TASK_NAME, "/XML", &path, "/F"]);
    let _ = std::fs::remove_file(&file);
    let out = out?;
    if out.status.success() {
        Ok(())
    } else {
        Err(std::io::Error::other(String::from_utf8_lossy(&out.stderr).trim().to_string()))
    }
}

/// Bring autostart up to date at launch: an old Run-key entry becomes the
/// scheduled task, and an existing task is refreshed to point at this exe.
/// Never turns autostart on if the user (or an admin) turned it off.
#[cfg(windows)]
pub fn migrate_autostart(exe: &Path) -> std::io::Result<()> {
    if legacy_run_entry() || autostart_enabled() {
        set_autostart(true, exe)
    } else {
        Ok(())
    }
}

/// `%LOCALAPPDATA%\Programs\OpenRouter Meter\OpenRouterMeter.exe`
pub fn install_path() -> PathBuf {
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
    base.join("Programs").join("OpenRouter Meter").join("OpenRouterMeter.exe")
}

pub fn same_file(a: &Path, b: &Path) -> bool {
    match (a.canonicalize(), b.canonicalize()) {
        (Ok(x), Ok(y)) => x == y,
        _ => false,
    }
}

pub fn open_in_notepad(path: &Path) {
    let _ = std::process::Command::new("notepad.exe").arg(path).spawn();
}

pub fn open_url(url: &str) {
    let _ = std::process::Command::new("explorer.exe").arg(url).spawn();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn task_definition_has_the_reliability_settings() {
        let x = task_xml(Path::new(r"C:\Users\a&b\OpenRouterMeter.exe"), r"PC\ryan");
        for needle in [
            "<LogonTrigger>", "<UserId>PC\\ryan</UserId>", "<Delay>PT30S</Delay>",
            "<Interval>PT5M</Interval>", "<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>",
            "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>",
            "<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>",
            "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>",
            "<Interval>PT1M</Interval>", "<Count>3</Count>",
            "<LogonType>InteractiveToken</LogonType>", "<RunLevel>LeastPrivilege</RunLevel>",
            "a&amp;b", "<Arguments>--autostart</Arguments>",
        ] {
            assert!(x.contains(needle), "missing {needle}");
        }
    }

    #[test]
    fn addresses_parse() {
        assert_eq!(parse_address("D4:05:92:B6:A4:9E"), Some(0xD40592B6A49E));
        assert_eq!(parse_address("d40592b6a49e"), Some(0xD40592B6A49E));
        assert_eq!(parse_address("nope"), None);
    }
}
