# Hardware

**Waveshare ESP32-C6-Touch-AMOLED-2.16**: ESP32-C6 (RISC-V, 160 MHz, BLE 5,
512 KB SRAM, no PSRAM), 16 MB flash, 2.16″ 480×480 AMOLED, capacitive touch,
AXP2101 power management with Li-ion charging, QMI8658 IMU.

| Function | Part | Connection |
|---|---|---|
| Display | CO5300 AMOLED | QSPI: CS 15, CLK 0, D0–D3 = GPIO 1–4. No reset GPIO; the panel reset is the AXP2101 ALDO3 rail |
| Touch | CST9220-family | I2C 0x5A, INT 5, RST 11 |
| Power | AXP2101 | I2C 0x34: rails, battery %, charging, PWR key |
| Motion | QMI8658 | I2C 0x6B: automatic screen rotation |
| I2C bus | | SDA 8, SCL 7 |
| Buttons | BOOT, KEY | GPIO 9, GPIO 10 (active low); PWR via the AXP2101 |
| Audio | ES8311 codec + amplifier | I2S MCLK 19, BCLK 20, WS 22, DOUT 23 (not used yet) |

Sources: Waveshare's documentation and example code for this board.

## Battery

A 3.7 V Li-ion cell on the MX1.25 connector powers the meter away from USB.
The header shows the charge level, and Windows shows it too through the
Bluetooth Battery Service. A "+" next to the battery means it's charging.

## Screen care

AMOLED panels can burn in. The meter shifts its whole image by a couple of
pixels every 4 minutes, and `dim_after_minutes` can dim it when no one is
looking.
