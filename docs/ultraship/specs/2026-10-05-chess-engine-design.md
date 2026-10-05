# Chess Engine — Design Spec (2026-10-05)

Research basis: `research/2026-10-05 Research Dossier.md`. Decisions were made with Kavin on 2026-10-05; he then delegated all remaining calls.

## 1. Goal
A from-scratch UCI chess engine (alpha-beta search + NNUE evaluation). One portable C++ core builds for two targets:

| Target | Claim we want | Checkable success condition (by 2027-01-20) |
|---|---|---|
| PC (macOS/Linux, x86-64 + ARM64) | Independent, CCRL-anchored strength | Rating with 95% CI from ≥1,000 games vs a pool of CCRL-rated engines. Target 2600–2900. Stretch 3000 |
| ESP32-S3 (Kavin's Edgehax S3-PRO, N16R8) | Strongest documented ESP32-S3 engine | ≥400 games at 1+0.6 over USB vs the same engine pool CST Retro used. **Claim "#1" only if our 95% lower bound > 2210** (CST Retro's upper bound). Otherwise state the measured rating ± CI |

Also due by 2027-01-20:
- public GitHub repo, v1.0 release
- technical write-up (method, data, all PGNs)
- demo video
- one "explain-it-back" note per milestone, written by Kavin

## 2. Non-goals (v1)
- Beating Stockfish or being #1 on PC lists (impossible here; see dossier).
- Human-like play, move explanation, GUI. The ESP32 build talks UCI only; no screen UI.
- MCTS / Lc0-style GPU engine, Chess960, Syzygy probing.
- A distributed testing server (OpenBench). Local fastchess runs + lynxS are enough.

## 3. Constraints
- **Deadline:** 2027-01-20. **Kavin's time:** ~4 h/week, ~2 h/week in exam weeks (Nov 7 SAT, Dec 5 Shibaura exam, Dec 11–12 ACT). Code is written with Claude; long runs go to lynxS.
- **Hardware:** M3 MacBook (8 cores, 16 GB, 68 GB disk free on 2026-10-05), lynxS (Debian, always on), one ESP32-S3 N16R8 board.
  - ESP32-S3 facts: 512 KB SRAM. 32/64 KB data cache shared by both cores. PIE SIMD instructions (8/16/32-bit vectors). 16 MB flash, 8 MB octal PSRAM.
- **Budget:** ₹0 new spend.
- **Paths:** no spaces anywhere in the project path (ESP-IDF requirement). The project lives at `~/Projects/Chess-Engine`.
- **Disk:** training data on the Mac ≤ 20 GB at any time. The raw Lichess file is streamed and never stored.
- **Originality:**
  - Code: written from scratch, using only publicly documented techniques (chessprogramming.org). Never copy engine source.
  - Training labels: Stockfish evaluations from the Lichess CC0 eval DB. This is disclosed in the README and the write-up.
  - Bootstrap eval: PeSTO piece-square tables, M1 only. Disclosed; replaced at M2.
  - AI assistance is disclosed in the README.
- **License:** GPL-3.0 (computer-chess norm: Stockfish, Ethereal, PlentyChess).

## 4. Architecture
```
Chess-Engine/
  src/            portable core (C++17, no exceptions/RTTI use; all allocation — TT, attack tables, threads — happens in init, none during search)
    types.h       Square, Piece, Move (16-bit), Bitboard (uint64_t), Score
    bitboard.*    attack tables, magic bitboards, bit helpers (__builtin_popcountll/ctzll)
    board.*       Board: set_fen, make/unmake, Zobrist key, in_check, is_draw (50-move, repetition, insufficient material)
    movegen.*     generate<ALL|CAPTURES>(board, MoveList&) — pseudo-legal, legality check in make
    eval.*        evaluate(board) -> Score (stm-relative). M1: PeSTO tapered PST; M2+: NNUE
    nnue.*        accumulator (incremental add/sub on make/unmake), forward pass, net loaded from embedded bytes
    tt.*          transposition table (fixed buckets, size set at init)
    search.*      iterative deepening, PVS, qsearch, pruning, move ordering; Limits{time, inc, movestogo, depth, nodes}
    uci.*         UCI loop over an abstract line I/O (works on stdin or USB serial)
    platform.h    tiny shim: now_ms(), read_line(), write_line(), start_thread() — the ONLY per-target code
  pc/main.cpp     platform.h via stdio + std::thread
  esp32/          PlatformIO project (framework = arduino, the config already proven on Kavin's board: USB-CDC on boot, qio_opi PSRAM; GCC 8.4 with -std=gnu++17). platform.h via USB-CDC + FreeRTOS tasks pinned per core
  tools/          lichess_to_text (C++, links core), uci_bridge.py (fastchess <-> USB serial), sprt.sh, gauntlet.sh
  train/          bullet trainer config (Rust crate, Metal backend)
  nets/           release nets only (committed). Work-in-progress nets go in nets/wip/ (gitignored)
  Makefile        `make EXE=... EVALFILE=...` (OpenBench/CCRL convention)
```

**Unit contracts:**
- `Board` knows nothing about search.
- `search` reads `Board`, `TT` and `eval` only.
- `uci` is the only module that parses text.
- `platform.h` is the only file that differs between targets.

Every unit is testable from the `pc` build.

**Platform differences** (each one decided by measurement, not assumed):

| Concern | PC | ESP32-S3 | Decision rule |
|---|---|---|---|
| Slider attacks | Fancy magics, tables built at init | Same code; tables (~860 KB: rook 102,400 + bishop 5,248 entries × 8 B) built into PSRAM at boot | Implement a compact ray method in SRAM **only if** device perft < 418 knps (CST's mailbox perft speed) |
| NNUE hidden size N (`(768→N)×2→1`) | 256 at M2, grown via SPRT | Starts at 128 | Largest N that wins on device in node-scaled SPRT |
| NNUE inner loop | Compiler auto-vectorisation (NEON/AVX2) | Scalar first, then PIE SIMD | SIMD is kept only if device nps improves ≥20% |
| TT | Default 16 MB (UCI `Hash`) | 128 KB in internal SRAM, optional PSRAM tier | PSRAM tier kept only if it wins SPRT at device speed |
| Threads | Lazy SMP via `Threads` option (M3+) | 2 FreeRTOS tasks, one per core (M4) | SPRT 2 threads vs 1 at device speed |

## 5. Evaluation and training data
- **Source:** `https://database.lichess.org/lichess_db_eval.jsonl.zst`. 22.4 GB compressed, 416,442,401 positions, CC0.
  - Each line: `fen` (no move counters) plus `evals[]`. Each eval has `pvs[{cp|mate, line}]`, `knodes` and `depth`.
  - **Scores are White-relative.** Verified on samples: in a Black-to-move position, the PVs are ordered with the lowest cp first. The M2 converter unit test asserts this.
- **Pipeline** (streamed): `curl | zstd -dc | lichess_to_text` → 10M-line text shards in the form `<FEN 0 1> | <cp white-relative> | 0.5` → `bullet-utils convert` → binary (32 B/position) → delete the text shard → shuffle + interleave.
- **Filter** (in `lichess_to_text`, using our own movegen):
  - Use the eval with the greatest depth, and keep only depth ≥ 20.
  - Drop mate scores and |cp| > 3000.
  - Drop positions where the side to move is in check.
  - Drop positions where the first PV move is a capture or promotion (keep quiet positions only).
  - Hold out 1% for validation loss.
- **Training:** bullet, Metal backend, on the M3.
  - Eval-only target: no game results exist, so the WDL weight = 0.0 and the result field is a 0.5 placeholder.
  - Network: `(768→N)×2→1` with SCReLU. Quantised i16 with QA=255, QB=64. Weights saved little-endian, in the column order our inference expects (pinned by a round-trip test).
  - The PC net and the device net are both trained from the same data.
- **Stretch (only if time allows after M4):** rescore our own self-play positions with our engine and mix them in.

## 6. Search (staged; every addition after M1's base is SPRT-gated)
- **M1 base:**
  - Search core: iterative deepening, negamax PVS, quiescence (captures), TT cutoffs.
  - Move ordering: TT move → MVV-LVA captures → killers → history.
  - Pruning/reductions: null-move pruning, check extension, simple LMR.
  - Draw detection.
  - Time management: soft limit = time/20 + inc/2, hard limit = 3× soft, capped at remaining time − 50 ms.
- **M3 candidates** (each one SPRT-tested, then kept or dropped):
  - Aspiration windows, reverse futility pruning, futility pruning, late-move pruning.
  - SEE pruning and ordering, continuation history, singular extensions, improving flag.
  - Tuned LMR table, Lazy SMP.

## 7. Verification
1. **Correctness:**
   - perft against the six standard chessprogramming.org positions (start, Kiwipete, positions 3–6): exact counts to depth 5 on PC and depth 4 on device.
   - Unit asserts for make/unmake round-trip (key and board restored) and FEN round-trip.
2. **Determinism:** `bench` searches a fixed set of 50 positions to a fixed depth and prints the total node count. Every functional commit message carries `Bench: <n>`.
3. **Strength of a change:** fastchess SPRT, pentanomial model `normalized`.
   - Gainers: `elo0=0 elo1=5`, alpha = beta = 0.05.
   - Simplifications: `elo0=-5 elo1=0`.
   - Time control: STC 8+0.08, 1 thread, Hash 16. LTC 40+0.4 for search-shape changes.
   - Openings: one UHO unbalanced EPD book (Pohl), pinned by SHA-256 in `docs/testing.md`.
   - A change merges only on H1.
4. **PC rating:** gauntlet vs ≥8 engines with published CCRL Blitz ratings within ±250 of our estimate. ≥1,000 games. The TC is scaled from CCRL's reference hardware by the measured bench-speed ratio. Ratings are computed with Ordo, pool engines fixed to their CCRL ratings, and reported with 95% CI.
5. **Device rating:**
   - `uci_bridge.py` relays UCI between fastchess and USB-CDC serial.
   - Same pool and TC as CST Retro (its `docs/ENGINE-POOL.md`, 1+0.6). ≥400 games, concurrency 1, overnight runs on the Mac.
   - **Infrastructure failures are counted as losses** (conservative) and are reported.
   - **Fast loop:** before device runs, emulate the device on PC with a node limit = measured device nps × time per move.
6. **Publication:** all PGNs, pool lists, commands and raw fastchess output are committed under `results/`.

## 8. Milestones (each gets its own implementation plan when reached)
| # | Dates | Done when |
|---|---|---|
| M0 | Oct 6–19 | Repo builds for PC and ESP32. Perft suite exact on both. Device perft nps measured. `bench` works |
| M1 | Oct 20–Nov 2 | UCI search + PeSTO eval. Expected strength ~2200–2400 CCRL. Sanity gauntlet: 1,000 games vs one engine rated ~2000 CCRL; pass = estimate ≥2200 (≈76% score), and anything lower triggers a bug hunt before M2. SPRT harness works end to end. The M1 base features (null move, LMR, check extension) are validated by this gauntlet only, not SPRT |
| M2 | Nov 3–30 | Converter + filtered dataset (≥200M positions). First NNUE beats PeSTO by SPRT on PC. Device build runs the small net |
| M3 | Dec 1–21 (light) | ≥5 SPRT-tested search additions. PC rating estimate ≥2600 from a mini-gauntlet |
| M4 | Dec 1–Jan 4 | Device: SIMD, TT tiering, dual-core decided by measurement. Node-emulated device gauntlet estimate ≥2250 |
| M5 | Jan 5–14 | Final PC gauntlet (≥1,000 games) and device gauntlet (≥400 games), published |
| M6 | Jan 12–20 | README, write-up, demo video, v1.0 tag, CCRL test request posted on TalkChess, launch per the GitHub Growth playbook |

Plan 1 covers M0 + M1. Later plans are written when we get there, because they depend on M0/M2 measurements (device speed, data yield).

## 9. Failure modes and risks
| Risk | Mitigation |
|---|---|
| Device stays below the 2210 bar | Publish the measured rating ± CI with honest wording. The PC result stands on its own |
| Millennium King Performance is really stronger (retail "~2450", no stated method) | Compare only against documented measurements and say so explicitly |
| CCRL doesn't test us by Jan 20 | Our own CCRL-anchored gauntlet is the deliverable; an official listing is a bonus |
| USB-CDC/serial driver not working on the Mac | Plan 1, task 1 is a USB-CDC echo check on the real board. Fallback: run gauntlets from lynxS |
| ESP32 watchdog or crash mid-game | Search task feeds or disables the task WDT; `stop` is honoured within 10 ms; crashes count as losses |
| Lichess data filter yields less than expected | M2 measures the yield first. ≥100M positions is still enough for N ≤ 512 |
| Exams squeeze December | M3/M4 already sized light; M6 holds 8 days of buffer |
| Kavin can't explain the engine at interview | One explain-it-back note per milestone, written by Kavin, checked by Claude |

## 10. Decision log
| Decision | Chosen | Trade-off |
|---|---|---|
| Language | C++17 | Rust is safer, but ESP32-S3 Rust needs the forked espup toolchain and inline asm for PIE is awkward |
| Board | Bitboards + mailbox | Lose ~2x on 64-bit operations on a 32-bit MCU; gain proven speed on PC and shared code |
| Training labels | Lichess Stockfish evals (Kavin's call) | Strength sooner and ₹0 compute; the net is distilled from Stockfish — disclosed |
| Testing | fastchess locally + lynxS | No OpenBench server to run; fewer cores |
| Build | Makefile (PC) + PlatformIO, Arduino framework (ESP32) | No CMake; matches the OpenBench/CCRL `make EXE=` convention. Arduino instead of raw ESP-IDF because its USB-CDC setup already works on this board; FreeRTOS tasks are still available |
