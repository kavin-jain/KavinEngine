# SPRT log
Every search/eval change, accepted or not. Method: `docs/testing.md` (fastchess, pentanomial `normalized`, alpha = beta = 0.05, Hash 16, UHO_Lichess_4852_v1). Raw logs and PGNs (`.pgn.xz`) are in `results/sprt/`.

| Date | Change | Machine, TC | Result | Games | Elo ± 95 % | LLR | Bench | Commit / patch |
|---|---|---|---|---|---|---|---|---|
| 2026-10-06 | NNUE m2-256 vs PeSTO (M2 gate) | lynxS, 8+0.08, conc. 2 | **H1** [0, 10] | 154 (W145 D6 L3, 0 time losses) | +557 ± 141 | 2.95 | 13350441 | 3ba47e6 |
| 2026-10-07 | Reverse futility pruning | Mac, 8+0.08, conc. 3 | **H1** [0, 5] | 558 (W238 D254 L66, 0 time losses) | +110.7 ± 20.0 | 2.95 | 6589794 | see commit |
| 2026-10-07 | (RFP, two earlier runs) | Mac under load avg 20–32 from other jobs | **invalid** | 222 + 220 | — | — | 6589794 | `results/sprt/rfp-invalid-load*.{log,pgn.xz}` (base lost games on time) |
| 2026-10-07 | Aspiration windows | Mac, 8+0.08, conc. 3 | **H1** [0, 5] | 658 (W237 D317 L104, 0 time losses) | +71.2 ± 16.1 | 2.96 | 6750335 | see commit |
| 2026-10-07 | Late move pruning (3+d², checks kept) | Mac, 8+0.08, conc. 3 | **stopped early, not merged** | 1360 (W312 D715 L333, 0 time losses) | −5.3 ± 11.6 | −0.65 | 3697952 | `results/sprt/lmp.patch` (retry after continuation history) |
| 2026-10-07 | Futility pruning (checks kept) | GitHub Actions, 8+0.08, 6 jobs × conc. 3 | **H1** [0, 5] | 1200 (W351 D626 L223, 0 time losses) | +37.2 ± 11.3 | 3.05 | 5205601 | see commit |
| 2026-10-07 | (Futility, Mac partial run, superseded by the cloud run) | Mac, 8+0.08 | moved to cloud | 240 | — | 0.34 | 5205601 | `results/sprt/fp-mac-partial.*` |
| 2026-10-07 | SEE: qsearch pruning + capture ordering | GitHub Actions, 8+0.08, 16 jobs × conc. 3 | **H1** [0, 5] | 3200 (W922 D1677 L601, 0 time losses) | +35.0 ± 7.1 | 7.29 | 4822270 | see commit |
