# Chess-Engine (working name)

A from-scratch UCI chess engine in C++17 that runs on a PC and on a ~$10 ESP32-S3 microcontroller from the same source.

**Status — milestone M1** (2026-10-05):
- Bitboard move generation, perft-verified on the 6 standard test positions.
- Alpha-beta search (PVS) with a transposition table, null-move pruning, late-move reductions, check extension and quiescence search.
- PeSTO evaluation as a bootstrap; a trained NNUE network replaces it in M2.
- ~8 M nodes/s single-threaded on an Apple M3. Bench signature: `17440859` nodes (`./engine bench`, depth 11).
- ESP32-S3 build compiles; on-device verification is pending.
- Strength so far: see `docs/measurements.md`. Ratings appear only with the games behind them.

## Build and run
| Target | Command |
|---|---|
| PC engine (UCI) | `make` → `./engine` |
| Tests | `make test` |
| Bench / signature | `./engine bench` |
| ESP32-S3 | `cd esp32 && pio run -e s3 -t upload`, then speak UCI over USB: `.venv/bin/python tools/uci_bridge.py /dev/cu.usbmodemXXXX` |

## Layout
- `src/` — portable engine core (board, move generation, search, evaluation, UCI)
- `pc/` and `esp32/` — the two targets. Only `src/platform.h` differs between them.
- `tests/` — assert-based test suite
- `tools/` — SPRT, match and USB-bridge scripts
- `docs/` — spec, plans, measurements, test setup
- `research/` — the research dossier behind the design

## Honesty notes
- The code was written with AI assistance (Claude). The project is directed, tested and reviewed by Kavin Jain.
- The M1 evaluation uses the published PeSTO tables (Ronald Friederich), credited in `src/pesto_tables.h`.
- From M2, the neural network trains on Stockfish evaluations from the Lichess CC0 database. Every result will state this.
- Every rating claim ships with its games (`results/`), method (`docs/testing.md`) and confidence interval.

## License
GPL-3.0 — see `LICENSE`.
