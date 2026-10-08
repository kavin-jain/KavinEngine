# Chess-Engine (working name)

A from-scratch UCI chess engine in C++17 that runs on a PC and on a ~$10 ESP32-S3 microcontroller from the same source.

**Status — milestone M2** (2026-10-06):
- Bitboard move generation, perft-verified on the 6 standard test positions.
- Alpha-beta search (PVS) with a transposition table, null-move pruning, late-move reductions, check extension and quiescence search.
- NNUE evaluation: a (768→256)×2→1 network trained on 236.8M Stockfish-evaluated Lichess positions, with incrementally updated accumulators. It beat the M1 PeSTO evaluation by **+557 ± 141 Elo** (SPRT H1, 154 games; `docs/sprt-log.md`).
- ~3.3 M nodes/s single-threaded on an Apple M3. Bench signature: `13350441` nodes (`./engine bench`, depth 11), identical on macOS arm64 and Linux x86-64.
- ESP32-S3 build embeds a 128-neuron net and compiles; on-device verification is pending.
- Strength: **3449 ± 11 on the CCRL Blitz scale** (2026-10-08, Leela-data net): 1,536 games at 60+0.6 against 8 anchors from 7 engine families with known CCRL Blitz ratings (Stash 35/37, Ethereal 12.75, Halogen 11, Koivisto 7.0, Berserk 8.5, Altair 7.0.0, Alexandria 6.0.0), each built from its release tag and checked by its UCI name; rating fitted by maximum likelihood with the anchors held fixed (`tools/gauntlet.py`, games in `results/gauntlet/honest-2026-10-08/`). An estimate on CCRL's scale from our own hardware and time control, not an official CCRL rating. (History: the M1 PeSTO build was ~2359 vs BBC 1.1, `docs/measurements.md`.)

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

## Training data and licences
- **Default net `nets/leela-256.bin`.** Trained from scratch on 547M positions from Leela Chess Zero training data, as converted to bullet format in [linrock/bullet-training-data](https://huggingface.co/datasets/linrock/bullet-training-data) (file S2 iter-1).
  - Licence: Lc0 training data is under the **Open Database License (ODbL)**, and this net is a Produced Work from it.
  - Method: the full pipeline is in this repository (`tools/kaggle/train.py`, `train/`). Scores were rescaled to the engine's centipawns with `EVAL_SCALE` = 400 / (main-net eval / label slope) = 1106.
- **Earlier nets.** Trained on the Lichess evaluation database (CC0; Stockfish evaluations) and on this engine's own self-play games (`tools/datagen.cpp`, releases `data-sp*`).
- **What is ours.** The search, the network architecture and the training pipeline. The evaluation knowledge is distilled from Leela/Stockfish-derived data plus our own self-play. No Stockfish or Leela code or network weights are used.

## License
GPL-3.0 — see `LICENSE`.
