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
