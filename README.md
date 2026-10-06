# Chess-Engine (working name)

A from-scratch UCI chess engine in C++17 that runs on a PC and on a ~$10 ESP32-S3 microcontroller from the same source.

**Status — milestone M2** (2026-10-06):
- Bitboard move generation, perft-verified on the 6 standard test positions.
- Alpha-beta search (PVS) with a transposition table, null-move pruning, late-move reductions, check extension and quiescence search.
- NNUE evaluation: a (768→256)×2→1 network trained on 236.8M Stockfish-evaluated Lichess positions, with incrementally updated accumulators. It beat the M1 PeSTO evaluation by **+557 ± 141 Elo** (SPRT H1, 154 games; `docs/sprt-log.md`).
- ~3.3 M nodes/s single-threaded on an Apple M3. Bench signature: `13350441` nodes (`./engine bench`, depth 11), identical on macOS arm64 and Linux x86-64.
- ESP32-S3 build embeds a 128-neuron net and compiles; on-device verification is pending.
- Strength: the M1 (PeSTO) build scored 87.6 % over 1,000 games vs BBC 1.1 (CCRL Blitz 2020) → ~2359 ± 30 estimate. That is a one-opponent sanity check, not a CCRL rating; the NNUE build gets a multi-engine rating in M3. Games are in `results/`, method and caveats in `docs/measurements.md`.

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
- `tools/` — data converter, SPRT/match/USB-bridge scripts, held-out loss tool
- `train/` — bullet trainer (Rust, Metal GPU); `nets/` — trained networks
- `docs/` — spec, plans, measurements, test setup
- `research/` — the research dossier behind the design

## Honesty notes
- The code was written with AI assistance (Claude). The project is directed, tested and reviewed by Kavin Jain.
- The M1 evaluation uses the published PeSTO tables (Ronald Friederich), credited in `src/pesto_tables.h`.
- From M2, the neural network trains on Stockfish evaluations from the Lichess CC0 database. Every result will state this.
- Every rating claim ships with its games (`results/`), method (`docs/testing.md`) and confidence interval.

## License
GPL-3.0 — see `LICENSE`.
