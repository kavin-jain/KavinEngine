# NNUE scaling study (Track B): results log

**Question:** how do playing strength and evaluation accuracy scale with network width and training data, and where is the
strength-optimal width for a given device and time control?

## Setup (fixed across all nets)
- **Architecture:** (768 × 10 mirrored king buckets → N) × 2 → 8 output buckets (MaterialCount), SCReLU, factoriser.
- **Training:** bullet rev 6b2d278, CUDA on a Kaggle T4. 40 superbatches × 6104 batches × 16384 positions (~4B positions seen), AdamW, cosine LR 1e-3 → 2.7e-5, eval scale 400, WDL 0.
- **Data:** Lichess evaluation DB (CC0), 237M positions in 32 shuffled shards (release `data-lichess-v1`). Data size is varied with `SHARDS` (first n shards, ~7.4M positions each).
- **Accuracy metric:** mean (σ(eval/400) − σ(label/400))² over 300k held-out positions (`val.bin`, never trained on), computed with the engine's own quantised integer inference (not the trainer's float model).

## Held-out loss (2026-10-08)
| Width N | Positions | Held-out loss | Change per doubling of N |
|---|---|---|---|
| 32 | 237M | 0.006759 | — |
| 64 | 237M | 0.006182 | −8.5 % |
| 128 | 237M | 0.005675 | −8.2 % |
| 256 | 237M | 0.005255 | −7.4 % |
| 512 | 237M | 0.004938 | −6.0 % |
| 128 | 59M | 0.005969 | |
| 128 | 15M | 0.007356 | (≈270 epochs: memorised; training loss 0.004834) |

## Strength (SPRT, 8+0.08, GitHub Actions)
- 512 vs 256 (both with output buckets): **−16.2 ± 7.8 Elo**, despite 6 % lower held-out loss. The 512 net runs at ~19 % lower nps.

## Elo vs the 128-wide reference (2026-10-08, 2400 games each, book UHO_Lichess_4852_v1, GitHub Actions x86)
| Width N | Positions | Fixed 20k nodes/move (eval quality) | 8+0.08 (quality + speed) | nps vs N=128 |
|---|---|---|---|---|
| 32 | 237M | −125.5 ± 11.2 | −129.4 ± 9.9 | 1.10 |
| 64 | 237M | −76.2 ± 10.7 | −55.0 ± 9.1 | 1.02 |
| 128 | 237M | 0 | 0 | 1.00 |
| 256 | 237M | +45.7 ± 10.3 | +14.8 ± 8.9 | 0.80 |
| 512 | 237M | +70.4 ± 10.5 | +13.9 ± 9.0 | 0.57 |
| 128 | 59M | −29.5 ± 10.3 | −30.8 ± 8.8 | — |
| 128 | 15M | −243.1 ± 13.3 | −210.1 ± 11.4 | — |

## Fits
- **Accuracy is a power law in width:** loss(N) = 0.00335 + 0.0089·N^−0.278. It matches all five widths within 0.3 % (log-space least squares over a grid of irreducible-loss values).
- **At fixed nodes, Elo is linear in held-out loss:** ≈ 11.4 Elo per 0.0001 lower loss (residuals ≤ 18 Elo, against ±11 errors). This holds within one architecture and one data source. It failed across architectures (output buckets: lower loss, −20.5 Elo) and across data sources (self-play fine-tune: +54.5 Elo).
- **Speed cost** (fixed-node Elo minus timed Elo): +31 Elo at N=256 and +57 at N=512. Below 128, the network is a small share of node time, so smaller nets gain almost no speed (32: 1.10× nps).
- **Desktop STC optimum:** timed Elo plateaus at N = 256–512 (+14.8 vs +13.9). The earlier direct SPRT (512 vs 256: −16.2 ± 7.8) agrees within the errors.

## Early findings
1. **Accuracy improves smoothly with width.** The gain per doubling shrinks slowly (8.5 % → 6.0 %), consistent with a power law that approaches an irreducible loss.
2. **Lower loss did not mean more Elo at short time controls** (also seen for output buckets: −20.5 Elo at lower loss). Strength depends on eval accuracy *and* speed, so the optimum must be measured in games, per device and per time control. This is the study's core question.
3. **Data limits small sets hard:** at 15M positions the 128-wide net overfits badly. More data is worth more than more width below ~60M positions.

## Time-limited strength model (2026-10-08)
- **Model:** Elo_T(N) = Q(N) + κ·log2(nps(N) / nps(128)).
  - Q(N) is the fixed-node Elo: −114000·(L(N) − L(128)), with L from the power law above.
  - κ is the Elo per doubling of nodes.
- **Fit to our table:** κ = 72 ± 8 per doubling (weighted fit over four widths). The fixed-node Elo carries over to 8+0.08 unchanged (scale factor 0.99 ± 0.04).
- **Predicted vs measured** at 8+0.08:

  | Width | Predicted | Measured |
  |---|---|---|
  | 256 | +22.5 | +14.8 ± 8.9 |
  | 512 | +12.0 | +13.9 ± 9.0 |
  | 32 | −115.6 | −129.4 ± 9.9 |
  | 64 | −74.1 | −55.0 ± 9.1 (≈ 2σ) |

- **Literature range:** κ = 76–146 (Stockfish wiki "Useful data": time-odds and speed-up tests). Ours sits near the long-time-control end.
- **Cost model:** time per node ∝ c0 + N with c0 ≈ 381, so the eval share is p = N/(c0 + N) = 0.25, 0.40, 0.57 at N = 128, 256, 512.
- **Optimum:** dQ/dlog2 N = κ·N/(c0 + N), which gives N\* ≈ 400. Anything from 245 to 663 is within 5 Elo, consistent with 256 ≈ 512 measured.
- **What speed is worth:** 1 % nps ≈ 1 Elo. At N = 256, 1 % lower held-out loss ≈ 6 Elo ≈ 6 % nps. Widening trades accuracy for speed at break-even or worse; free accuracy (data, labels) and free speed (compiler, SIMD, caches) are what pay.
- **First free speed-up:** clang++ instead of g++ on the cloud runners: **+8.0 % nps**, bench-identical (`nps` workflow). Model value ≈ +8 Elo. Every earlier cloud rating used g++ builds.

## Hypothesis to test next (falsifiable)
On the ESP32-S3 (no SIMD for these kernels, ~100× slower per node), evaluation is a much larger share of each node. The speed cost per doubling should therefore be larger, and the timed optimum should shift **below** N = 128. It is falsified if the device-emulated optimum stays at ≥ 256.

## Next
- Elo of each net at (a) fixed nodes, which measures eval quality alone, (b) fixed time on desktop hardware, and (c) node budgets emulating the ESP32-S3's measured speed per width, checked later on the real board.
- Fit loss(N, D) and Elo(N, device, time control); predict the optimal width per device and verify the prediction with a held-out experiment.

## Data quality vs quantity (2026-10-08)
**Setup:** the same Lichess-trained base, fine-tuned for 5 superbatches (WDL 0.3) on subsets of 296.9M self-play positions (sp2).

| Comparison | Elo (fixed games) |
|---|---|
| Fine-tune on self-play vs on Lichess, same steps (confound control) | +57.2 ± 7.3 |
| +5 superbatches on Lichess vs none | +3.5 ± 8.0 |
| Hardest 25 % by the main net's per-position loss vs random 25 % | **−14.5 ± 6.8** |
| Hardest 25 % vs all 100 % | −27.7 ± 7.0 (so 4× data ≈ +13) |
| Two training runs of the same arm (noise floor) | +0.3 ± 8.4 |

**Reading:**
- The self-play gain comes from the data, not from extra training.
- Ranking by surprise selects label noise: 5k-node search labels are often wrong exactly where the net disagrees. That is Sorscher et al.'s caveat: data pruning needs a metric that separates informative examples from mislabelled ones.
- Next tests: label quality at equal compute (deeper search, fewer positions), and pruning the noisiest ~10 % instead of keeping it.

