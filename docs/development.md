# Development

## Layout

```
app/                    Windows tray app (Rust)
  src/lib.rs            platform-independent core (tested on any OS)
    api.rs              OpenRouter client (/credits, /activity, /keys)
    payload.rs          API data -> meter messages (the numbers logic)
    config.rs           settings.txt
    daystate.rs         spend-today fallback baseline
  src/main.rs           tray icon + menu, self-install (Windows)
  src/worker.rs         poll loop, sends changed messages
  src/link.rs           Bluetooth LE via WinRT (find, pair, GATT)
  src/system.rs         pairing, autostart, single instance
firmware/
  src/main.cpp          setup/loop, LVGL glue, brightness, PWR gesture
  src/protocol.cpp      JSON messages -> Model
  src/board/            Waveshare ESP32-C6-Touch-AMOLED-2.16 support
  src/link/             NimBLE GATT server
  src/ui/               shell (ui.cpp) + one file per screen (scr_*.cpp)
  src/fonts/            Inter / JetBrains Mono bitmap fonts
  sim/                  desktop simulator (SDL2)
docs/                   protocol, flashing, hardware, web installer
```

## Firmware

Install [PlatformIO](https://platformio.org/install/cli).

```bash
cd firmware
pio run                      # build
pio run -t upload            # flash (board in download mode, see flashing.md)
pio device monitor           # serial log (115200)
```

RAM budget: the C6 has no PSRAM. LVGL gets a 96 KB pool, the draw buffers are
2 × 480 × 24 px, and after boot about 185 KB of heap stays free. Animation moves
small objects over static artwork, so each frame only redraws a few small
rectangles. Fades use per-part opacity rather than `style_opa`, which would make
LVGL render the object through an off-screen layer.

### Simulator

The whole firmware (UI, protocol, alerts, navigation) runs on the desktop
against SDL2. That's the fastest way to work on screens.

```bash
# Linux: sudo apt install libsdl2-dev   macOS: brew install sdl2
cmake -S firmware/sim -B build-sim && cmake --build build-sim -j
cd firmware && ../build-sim/meter-sim          # plays sim/scenario.jsonl
```

Mouse = touch, arrows = BOOT/KEY, `p` = PWR, `Shift+P` (hold) = pairing gesture,
`d` = toggle Bluetooth link, `s` = screenshot.

Headless renders (used for the README images):

```bash
SDL_VIDEODRIVER=dummy SIM_TAPS=3000,6000 SIM_SHOT_MS=7000 SIM_SHOT=shot.bmp ../build-sim/meter-sim
SDL_VIDEODRIVER=dummy SIM_FRAMES=frames SIM_FRAME_MS=50 SIM_EXIT_MS=20000 ../build-sim/meter-sim
```

`sim/scenario.jsonl` is a list of protocol messages (plus an optional
`after_ms` delay), so you can script any situation, including alerts.

## App

Install Rust (<https://rustup.rs>).

```bash
cd app
cargo test --lib             # numbers logic, settings (any OS)
cargo build --release        # Windows: target/release/OpenRouterMeter.exe
```

Debug builds (and `--portable`) run in place instead of installing themselves.
Useful flags:

- `OpenRouterMeter.exe --once`: polls OpenRouter once and writes the messages it
  would send to `%APPDATA%\OpenRouterMeter\last_poll.json`, without Bluetooth.
- `OpenRouterMeter.exe --enable-autostart` / `--disable-autostart`: register
  or remove the start-at-sign-in task (same as the tray's *Start with Windows*).
- `OPENROUTER_MANAGEMENT_KEY` environment variable: overrides the key in
  settings.txt.
- `OPENROUTER_BLE_ADDRESS` environment variable: pins the meter's Bluetooth
  address and takes priority over `meter_address` in settings.txt.

### Autostart and recovery

Autostart is a per-user Task Scheduler task named **OpenRouterMeter**. It needs
no admin rights. The task runs `OpenRouterMeter.exe --autostart` 30 s after
sign-in, with no time limit and on battery. If the app fails it restarts it up
to 3 times, 1 minute apart, and the trigger repeats every 5 minutes; a second
copy exits silently because only one instance can hold the lock. Older
installs used an `HKCU\…\Run` value, which is removed and migrated on start.
*Uninstall* deletes the task.

The app doesn't give up on its own. API and Bluetooth errors are logged and
retried with backoff (2 s up to 30 s, forever), including while the Bluetooth
adapter isn't ready yet. Pinning is strict: once `meter_address` (or
`OPENROUTER_BLE_ADDRESS`) is set, no other device is ever tried. A device is
only written to after its OpenRouter Meter service and DATA characteristic have
been found. The app never removes a pairing by itself; that only happens from
*Re-pair meter…*.

Files the app writes to `%APPDATA%\OpenRouterMeter\`: `settings.txt`,
`meter.log` (rotated at 1 MB, never contains the key), `day_state.json`,
`last_poll.json`.

## Releasing

Without GitHub Actions, build both files locally and publish by hand:

```bash
gh release create v1.0.1 app/target/release/OpenRouterMeter.exe openrouter-meter-firmware.bin --title v1.0.1 --notes "..."
```

With Actions enabled, push a tag like `v1.0.0` instead. The **Release** workflow builds the firmware and the
app, attaches `OpenRouterMeter.exe` and `openrouter-meter-firmware.bin` to a
GitHub Release, and publishes the web installer (with that firmware) to
GitHub Pages. Enable Pages once under *Settings → Pages → Source: GitHub
Actions*.
