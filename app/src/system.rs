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

#[cfg(windows)]
pub fn autostart_enabled() -> bool {
    use winreg::{enums::HKEY_CURRENT_USER, RegKey};
    RegKey::predef(HKEY_CURRENT_USER)
        .open_subkey(RUN_KEY)
        .and_then(|k| k.get_value::<String, _>(RUN_VALUE))
        .is_ok()
}

#[cfg(windows)]
pub fn set_autostart(on: bool, exe: &Path) -> std::io::Result<()> {
    use winreg::{enums::*, RegKey};
    let (key, _) = RegKey::predef(HKEY_CURRENT_USER).create_subkey(RUN_KEY)?;
    if on {
        key.set_value(RUN_VALUE, &format!("\"{}\"", exe.display()))
    } else {
        match key.delete_value(RUN_VALUE) {
            Err(e) if e.kind() != std::io::ErrorKind::NotFound => Err(e),
            _ => Ok(()),
        }
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
