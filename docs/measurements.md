# Measurements
Numbers that later plans depend on. Every entry: date, command, result.

## 2026-10-05 — M0
- ESP32 build variant: `src_dir = ..` works (PlatformIO espressif32 7.0.1, Arduino core 2.0.17, xtensa GCC 8.4). Our sources compile with `-std=gnu++17 -O2` (defaults `-std=gnu++11 -Os` removed via `build_unflags`).
- ESP32 echo firmware: RAM 18,904 / 327,680 B static, flash 266,953 / 6,553,600 B.
- USB echo: deferred (board not connected; `pio device list` shows no `/dev/cu.usbmodem*`).
