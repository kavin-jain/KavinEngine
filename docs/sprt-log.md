# SPRT log
Every search/eval change, accepted or not. Method: `docs/testing.md` (fastchess, pentanomial `normalized`, alpha = beta = 0.05, Hash 16, UHO_Lichess_4852_v1). Raw logs and PGNs (`.pgn.xz`) are in `results/sprt/`.

| Date | Change | Machine, TC | Result | Games | Elo ± 95 % | LLR | Bench | Commit / patch |
|---|---|---|---|---|---|---|---|---|
| 2026-10-06 | NNUE m2-256 vs PeSTO (M2 gate) | lynxS, 8+0.08, conc. 2 | **H1** [0, 10] | 154 (W145 D6 L3, 0 time losses) | +557 ± 141 | 2.95 | 13350441 | 3ba47e6 |
| 2026-10-07 | Reverse futility pruning | Mac, 8+0.08, conc. 3 | **H1** [0, 5] | 558 (W238 D254 L66, 0 time losses) | +110.7 ± 20.0 | 2.95 | 6589794 | see commit |
| 2026-10-07 | (RFP, two earlier runs) | Mac under load avg 20–32 from other jobs | **invalid** | 222 + 220 | — | — | 6589794 | `results/sprt/rfp-invalid-load*.{log,pgn.xz}` (base lost games on time) |
| 2026-10-07 | Aspiration windows | Mac, 8+0.08, conc. 3 | **H1** [0, 5] | 658 (W237 D317 L104, 0 time losses) | +71.2 ± 16.1 | 2.96 | 6750335 | see commit |
