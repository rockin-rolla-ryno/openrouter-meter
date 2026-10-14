//! Bluetooth LE link to the meter, using Windows' own Bluetooth APIs (WinRT).
//!
//! Finding the meter:
//!   1. the address remembered in settings.txt, else
//!   2. any meter already paired with Windows (works even while Windows holds
//!      the connection and the meter has stopped advertising), else
//!   3. a meter advertising nearby (first-time setup).
//! GATT lookups are uncached, so a firmware update never leaves Windows with
//! a stale view of the meter. The session is kept alive by Windows
//! (MaintainConnection), which also reconnects after the meter sleeps.
use ormeter::{ids, log};
use std::time::{Duration, Instant};
use tokio::sync::mpsc;
use windows::core::GUID;
use windows::Devices::Bluetooth::Advertisement::{
    BluetoothLEAdvertisementReceivedEventArgs, BluetoothLEAdvertisementWatcher, BluetoothLEScanningMode,
};
use windows::Devices::Bluetooth::GenericAttributeProfile::{
    GattCharacteristic, GattClientCharacteristicConfigurationDescriptorValue, GattCommunicationStatus,
    GattSession, GattValueChangedEventArgs, GattWriteOption,
};
use windows::Devices::Bluetooth::{BluetoothCacheMode, BluetoothLEDevice};
use windows::Devices::Enumeration::DeviceInformation;
use windows::Foundation::TypedEventHandler;
use windows::Storage::Streams::DataWriter;

fn guid(u: uuid::Uuid) -> GUID {
    GUID::from_u128(u.as_u128())
}

#[derive(Debug)]
pub enum LinkError {
    NoAdapter,
    NotFound,
    NeedsPairing,
    Other(String),
}

impl std::fmt::Display for LinkError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LinkError::NoAdapter => write!(f, "no Bluetooth adapter (is Bluetooth turned on?)"),
            LinkError::NotFound => write!(f, "meter not found nearby"),
            LinkError::NeedsPairing => write!(f, "meter needs pairing"),
            LinkError::Other(e) => write!(f, "{e}"),
        }
    }
}

fn other(e: impl std::fmt::Display) -> LinkError {
    LinkError::Other(e.to_string())
}

pub fn format_address(a: u64) -> String {
    let b = a.to_be_bytes();
    format!("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", b[2], b[3], b[4], b[5], b[6], b[7])
}

pub struct Meter {
    pub address: String,
    _device: BluetoothLEDevice, // held so Windows keeps the link
    _session: Option<GattSession>,
    data: GattCharacteristic,
    _request: Option<GattCharacteristic>,
}

// WinRT objects used here are agile (callable from any thread).
unsafe impl Send for Meter {}
unsafe impl Sync for Meter {}

fn bluetooth_present() -> bool {
    use windows::Devices::Bluetooth::BluetoothAdapter;
    BluetoothAdapter::GetDefaultAsync()
        .and_then(|op| op.join())
        .map(|a| a.IsLowEnergySupported().unwrap_or(false))
        .unwrap_or(false)
}

#[derive(Debug, PartialEq)]
enum Probe {
    Meter,       // speaks our protocol
    Stale,       // answers, but not with our service: old firmware / stale pairing
    Unreachable, // off or out of range — leave the pairing alone
}

/// Ask the device (uncached) whether it exposes the meter's service.
fn probe(dev: &BluetoothLEDevice) -> Probe {
    let addr = format_address(dev.BluetoothAddress().unwrap_or(0));
    match dev
        .GetGattServicesForUuidWithCacheModeAsync(guid(ids::SERVICE), BluetoothCacheMode::Uncached)
        .and_then(|op| op.join())
    {
        Ok(r) => {
            let st = r.Status().unwrap_or(GattCommunicationStatus::Unreachable);
            let n = r.Services().map(|s| s.Size().unwrap_or(0)).unwrap_or(0);
            log!("probe {addr}: status {}, {n} service(s)", st.0);
            if st == GattCommunicationStatus::Success {
                if n > 0 { Probe::Meter } else { Probe::Stale }
            } else if st == GattCommunicationStatus::Unreachable {
                Probe::Unreachable
            } else {
                Probe::Stale
            }
        }
        Err(e) => {
            // e.g. 0x80070016 when Windows still treats it as the old device.
            log!("probe {addr}: {e}");
            Probe::Stale
        }
    }
}

/// Meters already paired with Windows (address), most likely first.
fn paired_candidates() -> Vec<u64> {
    let mut out = Vec::new();
    let Ok(selector) = BluetoothLEDevice::GetDeviceSelectorFromPairingState(true) else { return out };
    let Ok(list) = DeviceInformation::FindAllAsyncAqsFilter(&selector).and_then(|op| op.join()) else { return out };
    let Ok(size) = list.Size() else { return out };
    for i in 0..size {
        let Ok(info) = list.GetAt(i) else { continue };
        let name = info.Name().map(|n| n.to_string()).unwrap_or_default();
        if !name.to_ascii_lowercase().replace(' ', "").contains("openroutermeter") {
            continue;
        }
        if let Ok(dev) = info.Id().and_then(|id| BluetoothLEDevice::FromIdAsync(&id)).and_then(|op| op.join()) {
            if let Ok(a) = dev.BluetoothAddress() {
                out.push(a);
            }
        }
    }
    out
}

/// Listen for an advertising meter (first-time setup / unpaired).
fn scan_for_meter(timeout: Duration) -> Option<u64> {
    let watcher = BluetoothLEAdvertisementWatcher::new().ok()?;
    let _ = watcher.SetScanningMode(BluetoothLEScanningMode::Active);
    if let Ok(filter) = watcher.AdvertisementFilter().and_then(|f| f.Advertisement()) {
        let _ = filter.ServiceUuids().and_then(|v| v.Append(guid(ids::SERVICE)));
    }
    let (tx, rx) = std::sync::mpsc::channel::<u64>();
    let handler = TypedEventHandler::<BluetoothLEAdvertisementWatcher, BluetoothLEAdvertisementReceivedEventArgs>::new(
        move |_, args| {
            if let Some(a) = args.as_ref() {
                if let Ok(addr) = a.BluetoothAddress() {
                    let _ = tx.send(addr);
                }
            }
            Ok(())
        },
    );
    let token = watcher.Received(&handler).ok()?;
    watcher.Start().ok()?;
    let found = rx.recv_timeout(timeout).ok();
    let _ = watcher.Stop();
    let _ = watcher.RemoveReceived(token);
    found
}

/// Locate a meter's address (blocking).
pub fn locate(saved: Option<u64>) -> Result<u64, LinkError> {
    if !bluetooth_present() {
        return Err(LinkError::NoAdapter);
    }
    if let Some(a) = saved {
        return Ok(a);
    }
    let mut dropped_stale = false;
    for a in paired_candidates() {
        let Ok(dev) = BluetoothLEDevice::FromBluetoothAddressAsync(a).and_then(|op| op.join()) else { continue };
        match probe(&dev) {
            Probe::Meter => return Ok(a),
            Probe::Unreachable => {}
            Probe::Stale => {
                // A pairing from older firmware: forget it so the meter starts
                // advertising again, then pair afresh below.
                log!("dropping stale pairing for {}", format_address(a));
                drop(dev);
                let _ = crate::system::unpair(a);
                dropped_stale = true;
            }
        }
    }
    let wait = if dropped_stale { 20 } else { 8 };
    scan_for_meter(Duration::from_secs(wait)).ok_or(LinkError::NotFound)
}

/// Open the GATT session and characteristics (blocking). Refresh requests
/// from the meter are forwarded on `refresh`.
pub fn open(address: u64, refresh: mpsc::UnboundedSender<()>) -> Result<Meter, LinkError> {
    let device = BluetoothLEDevice::FromBluetoothAddressAsync(address)
        .and_then(|op| op.join())
        .map_err(|_| LinkError::NotFound)?;
    // Keep the link up; Windows reconnects by itself after drop-outs.
    let session = device
        .BluetoothDeviceId()
        .and_then(|id| GattSession::FromDeviceIdAsync(&id))
        .and_then(|op| op.join())
        .ok();
    if let Some(s) = &session {
        let _ = s.SetMaintainConnection(true);
    }

    let started = Instant::now();
    let services = loop {
        let r = device
            .GetGattServicesForUuidWithCacheModeAsync(guid(ids::SERVICE), BluetoothCacheMode::Uncached)
            .and_then(|op| op.join())
            .map_err(other)?;
        let st = r.Status().map_err(other)?;
        let list = r.Services().map_err(other)?;
        if st == GattCommunicationStatus::Success && list.Size().unwrap_or(0) > 0 {
            break list;
        }
        if st == GattCommunicationStatus::AccessDenied || st == GattCommunicationStatus::ProtocolError {
            return Err(LinkError::NeedsPairing);
        }
        if started.elapsed() > Duration::from_secs(12) {
            return Err(LinkError::NotFound);
        }
        std::thread::sleep(Duration::from_millis(700));
    };
    let service = services.GetAt(0).map_err(other)?;

    let char_of = |u: uuid::Uuid| -> Result<Option<GattCharacteristic>, LinkError> {
        let r = service
            .GetCharacteristicsForUuidWithCacheModeAsync(guid(u), BluetoothCacheMode::Uncached)
            .and_then(|op| op.join())
            .map_err(other)?;
        if r.Status().map_err(other)? == GattCommunicationStatus::AccessDenied {
            return Err(LinkError::NeedsPairing);
        }
        let list = r.Characteristics().map_err(other)?;
        Ok(if list.Size().unwrap_or(0) > 0 { list.GetAt(0).ok() } else { None })
    };
    let data = char_of(ids::DATA)?.ok_or_else(|| LinkError::Other("meter has no data characteristic (old firmware?)".into()))?;
    let request = char_of(ids::REQUEST)?;
    if let Some(req) = &request {
        let handler = TypedEventHandler::<GattCharacteristic, GattValueChangedEventArgs>::new(move |_, _| {
            let _ = refresh.send(());
            Ok(())
        });
        let _ = req.ValueChanged(&handler);
        match req
            .WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify)
            .and_then(|op| op.join())
        {
            Ok(GattCommunicationStatus::Success) => {}
            Ok(GattCommunicationStatus::AccessDenied) | Ok(GattCommunicationStatus::ProtocolError) => {
                return Err(LinkError::NeedsPairing)
            }
            Ok(s) => log!("subscribe status {:?}", s.0),
            Err(e) => log!("subscribe: {e}"),
        }
    }
    Ok(Meter {
        address: format_address(address),
        _device: device,
        _session: session,
        data,
        _request: request,
    })
}

impl Meter {
    /// Write one message with response (blocking). Long writes are handled
    /// by Windows, and the response gives back-pressure.
    pub fn send(&self, bytes: &[u8]) -> Result<(), LinkError> {
        let w = DataWriter::new().map_err(other)?;
        w.WriteBytes(bytes).map_err(other)?;
        let buf = w.DetachBuffer().map_err(other)?;
        let r = self
            .data
            .WriteValueWithResultAndOptionAsync(&buf, GattWriteOption::WriteWithResponse)
            .and_then(|op| op.join())
            .map_err(other)?;
        match r.Status().map_err(other)? {
            GattCommunicationStatus::Success => Ok(()),
            GattCommunicationStatus::AccessDenied => Err(LinkError::NeedsPairing),
            GattCommunicationStatus::ProtocolError => {
                let code = r.ProtocolError().ok().and_then(|p| p.Value().ok()).unwrap_or(0);
                // 0x05 / 0x0F: insufficient authentication / encryption.
                if code == 0x05 || code == 0x0F { Err(LinkError::NeedsPairing) } else { Err(LinkError::Other(format!("protocol error 0x{code:02X}"))) }
            }
            s => Err(LinkError::Other(format!("write status {}", s.0))),
        }
    }
}
