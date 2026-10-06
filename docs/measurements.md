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

## 2026-10-06 — M1 gate: 1,000 games vs BBC 1.1
`fastchess` 10+0.1, Hash 16 MB (ours) / BBC default 64 MB, fastchess time margin 0 ms, no adjudication, concurrency 4, UHO_Lichess_4852_v1 book, PeSTO build (bench 17440859). Games: `results/m1-vs-bbc.pgn`, full log: `results/m1-vs-bbc.log`.
- W 793 / D 165 / L 42, **87.55 %**. Pentanomial [0, 6, 40, 151, 303]. Elo **+338.8 ± 24.4** (nElo 531.5 ± 21.5).
- Estimate: BBC's CCRL Blitz 2020 ± 17 + 339 ≈ **2359 ± 30** (95 % intervals combined in quadrature). One-opponent figure at a non-CCRL time control, so it is a sanity check, not a rating. The real rating comes from the M5 multi-engine Ordo gauntlet.
- **Gate (≥ 2200, ≈ 76 % score): PASS.**
- Time forfeits: ours 7, BBC 3, overruns 2–76 ms. All 7 of ours came in the first session (games 1–374), which overlapped heavy foreground jobs on the same 4 P-cores. One forfeit move reached only depth 5 in 226 ms, versus under 1 ms when unloaded, so the process was starved of CPU. In the second session (626 games, other jobs at background QoS) ours had 0. Rule since: no CPU-heavy job runs alongside a match.
- The match was paused once (fastchess `config.json` resume) and resumed. Total match time 1 h 15 min for the second session.

## 2026-10-06 — M2
- Download speed from database.lichess.org: 11.8 MB/s (200 MB range request) → ~32 min for the 22.4 GB eval DB.
- Converter on the first 60 MB (678,229 lines): keep 352,503 (52.0%); mate 125,006 (18.4%); noisy 103,207 (15.2%); too_shallow (<20) 62,611 (9.2%); in_check 29,294 (4.3%); too_big (>3000 cp) 5,607 (0.8%); bad_fen 1 (the truncated last line).
- Converter speed: 324K lines/s on one P-core after replacing `std::getline(std::cin)` with POSIX `getline()` (libc++ cin locked per character; profiled at >50% of runtime; was ~20K lines/s). Output byte-identical before and after.
- **Converter byte-identical to `bullet-utils convert --from text` on 100,000 positions** (bullet rev 6b2d278). The check found one real Lichess analysis position with 33 pieces, which would have overflowed the record's 32-piece array; `classify()` now rejects >32 pieces.
- **Full conversion** (2026-10-06, 04:39–05:08 streamed): 416,442,401 lines → **238,708,347 kept (57.3 %)**. Rejects: noisy 75,986,731; mate 51,570,942; in check 23,370,891; depth < 20 21,504,427; > 3000 cp 5,286,139; bad FEN (incl. > 32 pieces) 14,924. Gate ≥ 200M: **PASS**. Output 7.6 GB: 32 shards (236,845,383 positions, shuffled in place) + `val.bin` (1,862,964, held out by hash).
- **Training** (bullet rev 6b2d278, Metal, MacBook Air M3): 1.6–2.2 M positions/s (thermal throttling on the fanless Air lowers the top figure). 40 superbatches × 100M positions; LR 0.001 cosine → 2.7e-5; eval-only target. m2-256: 38.7 min, final train loss 0.005856. m2-128: 29.6 min, 0.006347.
- This bullet revision prints "Validation data not currently implemented", so `tools/nnue_loss.cpp` measures held-out loss on the quantised net. **No overfitting:** m2-256 val 0.005879 / train 0.005876; m2-128 val 0.006370 / train 0.006364 (same-size train slice).
- Engine eval matches the trainer's float eval on 6 fixed positions within max(10 cp, 3 %). Incremental accumulator equals a full refresh over 200 random playouts.
- **NNUE speed:** profiling showed accumulator update 27 % and output layer 20 %. A fused copy+update and an exact int32 SCReLU dot (weights range-checked at load, so no overflow is possible) gave +30 % nps with an identical node count.
- **Bench (NNUE m2-256): 13350441.** Identical on macOS arm64 (M3) and Linux x86-64 (lynxS, i3-7020U), as is the PeSTO bench 17440859: the engine is deterministic across platforms.
- nps (bench, single thread): M3 (warm after training) NNUE 3.3 M / PeSTO 4.3 M; lynxS NNUE 2.12 M / PeSTO 2.95 M.
- ESP32 with the 128 net: static RAM 111,884 / 327,680 B, flash 742,325 B. **Risk:** ~216 KB nominal heap must hold the 128 KB TT plus the 72 KB search-task stack, before IDF overhead. Failures are reported at runtime (TT alloc message, `pdPASS` check, boot heap print). Measure on the board in M4; fallback is a 64 KB TT or a PSRAM TT.
