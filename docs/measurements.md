# Measurements
Numbers that later plans depend on. Every entry: date, command, result.

## 2026-10-05 — M0
- ESP32 build variant: `src_dir = ..` works (PlatformIO espressif32 7.0.1, Arduino core 2.0.17, xtensa GCC 8.4). Our sources compile with `-std=gnu++17 -O2` (defaults `-std=gnu++11 -Os` removed via `build_unflags`).
- ESP32 echo firmware: RAM 18,904 / 327,680 B static, flash 266,953 / 6,553,600 B.
- USB echo: deferred (board not connected; `pio device list` shows no `/dev/cu.usbmodem*`).
- PC bench (depth 11, 32 positions): **17440859 nodes**, ~8.1–8.5 M nps single-thread, 2.3 s (MacBook Air M3, 2026-10-05). Deterministic across runs. Depth 9 was 0.7 s, too shallow for a useful signature.
- PC search speed: ~7.1–7.7 M nps from startpos; depth 15 reached in 0.9 s under `go wtime 10000 winc 100`.
- ESP32 engine build (2026-10-05): static RAM 86,644 / 327,680 B, flash 543 KB. `MAX_GAME_PLY=256` on device: the first build with 1,024 entries used 141,940 B static, which would have starved the 128 KB TT + 72 KB search stack.
- ESP32 device perft / nps / boot heap / stop latency: **deferred** (board not connected).
