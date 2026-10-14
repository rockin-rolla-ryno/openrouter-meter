# Installing the firmware

## Download mode

With a battery fitted, this board doesn't enter download mode automatically.
Do it by hand:

1. Unplug USB.
2. Hold **BOOT**.
3. Plug USB back in while holding BOOT.
4. Release after ~3 s. The screen stays black.

After flashing, the board doesn't restart by itself. Hold **PWR** 8 s (off),
then press **PWR** briefly (on).

Flashing firmware keeps the meter's pairing, so you don't need to pair again.

## Option A: web installer (easiest)

Open the project's GitHub Pages site in Chrome or Edge, click **Install**,
choose the *USB JTAG/serial debug unit* port. The page writes
`openrouter-meter-firmware.bin` at offset 0.

## Option B: PlatformIO (from source)

```bash
cd firmware
pio run -t upload --upload-port COM4     # your port may differ
```

On Windows, `tools/flash-when-ready.ps1` waits for you to do the BOOT sequence
and flashes as soon as the board reappears in download mode.

## Option C: esptool with the release file

```bash
pip install esptool
esptool --chip esp32c6 --port COM4 write-flash 0x0 openrouter-meter-firmware.bin
```

## Recovering a board

If the board shows nothing after flashing, flash again from download mode.
Clearing the board's pairing (hold PWR 3 s, release) never needs a reflash.
