# Testing setup (pinned)

| Item | Value |
|---|---|
| Match runner | fastchess `alpha 1.8.2 20261004-a281caf`, built from source in `.deps/fastchess` |
| Opening book | `UHO_Lichess_4852_v1.epd` (official-stockfish/books), 2,632,036 positions, SHA-256 `7a7f6470615a69c6cf23d565417701d38732876f480af90d67b42abade35644a` |
| SPRT | `tools/sprt.sh`: STC 8+0.08, Hash 16, pentanomial `normalized`, alpha = beta = 0.05; gainers [0, 5], simplifications [-5, 0] |
| Concurrency | 4 (MacBook Air M3: 4 P-cores, fanless; macOS cannot pin threads to cores) |
| Sparring engine (M1 gate) | BBC 1.1 (maksimKorzh/bbc, tag `1.1`, `src/bbc_1.1.c`), CCRL Blitz **2020 ±17** (list of 2026-10-03, open-source class) |
| Device link | `tools/uci_bridge.py <port>`; a link failure ends the game as a loss |

BBC on macOS: the source declares a global `int time`, which clashes with libc `time()` under Apple's SDK. For a local build we rename the identifier `time` → `bbc_time` on exactly 8 code lines (347, 4431, 4467, 4472, 4493, 4512, 4515, 4518). Output strings and search logic are untouched.
```
perl -pe 's/\btime\b/bbc_time/g if $. == 347 || $. == 4431 || $. == 4467 || $. == 4472 || $. == 4493 || $. == 4512 || $. == 4515 || $. == 4518' bbc_1.1.c > bbc_1.1_macos.c
cc -O3 -w -o bbc bbc_1.1_macos.c
```

Rules:
- A change merges only on SPRT H1.
- Never compare Elo across separate runs (thermal state differs).

## Harness self-check (2026-10-05)
`tools/sprt.sh ./engine ./engine-material 0 10`: PeSTO eval vs a material-only build of the same engine.
- Games 152: W 148 / D 3 / L 1 (98.36%). Pentanomial [0, 0, 1, 3, 72]. LLR 2.98 (-2.94, 2.94) → **H1 accepted** in 14 min 53 s.
- No time losses or crashes in the log. The harness detects a real gain end to end.

## SPRT on lynxS (from M3)
lynxS: Intel i3-7020U (2 cores / 4 threads, 2.3 GHz, AVX2), Debian 13, fastchess built from the same commit (`alpha 1.8.2 20261004-a281caf`), book SHA-256 verified identical. Concurrency 2 (one game per physical core). Matches run in `tmux`.
- Bench node counts are identical to the Mac (NNUE 13350441, PeSTO 17440859).
- nps (bench, single thread): lynxS NNUE 2.12 M, PeSTO 2.95 M. Mac M3 cool PeSTO 8.3 M (M0 figure; the Mac was loaded by other work on 2026-10-06, so no clean NNUE figure yet).
- **STC on lynxS = 20+0.2** (R ≈ 2.5 from the PeSTO ratio 2.8, discounted for the dirty-piece bookkeeping added since M0). This is an estimate: re-measure R on an idle Mac. If it differs by > 20 %, later SPRTs switch TC, and each log row records its TC.
