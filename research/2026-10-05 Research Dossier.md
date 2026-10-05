# Chess Engine — Research Dossier (2026-10-05)

## Verdict

**Building the strongest chess engine in the world is not achievable for this project.** Stockfish 19 leads CCRL 40/15 at 3650. The top 30 engines sit within **46 Elo** of each other, and the leaders train on hundreds of billions of positions with distributed testing compute. The fastest solo-led climb on record (Reckless) took ~27 months, with community compute, to reach #2.

**Being the measurable #1 in a narrower, well-defined category is achievable.** The best candidate is the strongest engine on an ESP32-S3 microcontroller. The best documented result there is **~2170 CCRL** (CST Retro, 2026). Kavin already owns the hardware, and the lane fits his mechatronics application. See §8.

All numbers below were fetched on 2026-10-05 from the source listed. "Unverified" marks claims that only came from a secondary source.

---

## 1. Current engines and models

### 1.1 Classical search engines (alpha-beta search + NNUE evaluation)
CCRL 40/15 list dated 2026-10-02 (time control equivalent to 40 moves in 15 min on an i7-4770k, 4 CPU):

| Rank | Engine | Elo | Games |
|---|---|---|---|
| 1 | Stockfish 19 | **3650** | 677 |
| 2 | Reckless 0.9.0 | 3644 | 1579 |
| 3 | PlentyChess 7.0.0 | 3643 | 1780 |
| 4 | pawnocchio 2.0.1 | 3641 | 1142 |
| 5 | Torch v4d (Chess.com) | 3639 | 1424 |
| 9 | Obsidian 16.0 | 3636 | 2209 |
| 17 | Berserk 14 | 3627 | 1176 |
| 18 | Dragon by Komodo 3.3 | 3626 | 5905 |
| 30 | Lizard 11.2 | 3604 | 1242 |

- **Stockfish 18** (2026-01-31): up to +46 Elo over SF17. Wins 4x as many game pairs as it loses. New SFNNv10 network with "threat inputs". Trained on >100B positions of Lc0 evaluation data.
- **Stockfish 19** (2026-09-05): up to +44 Elo over SF18. Wins >3x as many game pairs. SFNNv16 network adds pawn-pair features. Trained on "hundreds of billions" of positions rescored by a strong Leela net.
- **TCEC Season 28 Superfinal**: Stockfish beat Leela 57.5–42.5 (+36 −21 =43), Stockfish's 18th title. Source is secondary (Wikipedia/Chessdom).
- **Lesson: the ceiling is compressed.** Most games between top engines are draws, so Elo gaps at the top shrink. Even a perfect engine would not rate far above 3650 on this list.

### 1.2 Neural MCTS (AlphaZero family)
- **AlphaZero** (Silver et al., *Science* 362(6419), Dec 2018). Self-play only. Used 5,000 first-gen TPUs for self-play and 64 second-gen TPUs for training, 700k steps × batch 4096. In the 1,000-game match vs Stockfish 8 it scored **155 wins, 6 losses**, rest draws.
- **Leela Chess Zero (Lc0)**: the open-source successor. Its transformer network BT4 has 15 layers, hidden size 1024 and 32 heads. It is ~300 Elo stronger in raw policy than the best CNN (T78).
- **Chessformer** (Monroe, Eilender, Chalmers, Tang, Anderson — arXiv 2605.19091, May 2026). A square-as-token transformer with "Geometric Attention Bias". It adds >100 Elo to Lc0 and won TCEC Cup 11 and Swiss 6/7 events over Stockfish. It is also the current best human-move predictor at **57.1%**.

### 1.3 Search-free transformers
- **DeepMind "Amortized Planning with Large-Scale Transformers"** (Ruoss et al., arXiv 2402.04494, v2 Oct 2024). A 270M-parameter transformer trained on ChessBench: 10M Lichess games, 15B Stockfish-16 annotations. It plays with **no search** and reached **Lichess blitz 2895 vs humans**, lower vs bots.
  - Weakness found by the authors: it is indecisive in won positions. It does not commit to a single mating plan, so they had to fall back to Stockfish to finish games.

### 1.4 Human-like models
| Model | Params | Training data | Top-1 human move match |
|---|---|---|---|
| Maia-1 (KDD 2020) | 9 × 10.3M | Lichess, per-rating models | ~51.6% |
| Maia-2 (NeurIPS 2024) | 23.3M | 169M games / 9.1B positions | ≈ +2 pp over Maia-1 |
| Allie (ICLR 2025) | 355M | 91M blitz games / 6.6B tokens | 55.7% (55.9% with search) |
| Chessformer (2026) | — | — | **57.1%** (current SOTA) |

- Maia-2 trained for 13 days on 2× A100 80GB.
- Allie was tested in 7,483 games against 2,412 humans. Its strength stayed within **49 Elo** on average of opponents rated 1000–2600.

### 1.5 LLMs playing chess
- **Kaggle Game Arena** (arXiv 2609.31473). Elo is anchored so the weakest model = 0; it is not on a human scale.
  - Results: Gemini 3 Pro Preview 1325, Gemini 3 Flash 1297, o3 1009, GPT-5.2 933, Claude Opus 4.5 236.
  - Rules: 3 retries for an illegal move; a 4th illegal move loses the game.
  - Failure modes: middlegame collapse, and illegal moves when in check.
- Takeaway: general LLMs are far below any real engine. Not a lane for "best".

### 1.6 Constrained and embedded engines (the niche)
| Engine / device | Hardware | Strength | Measurement |
|---|---|---|---|
| CST Retro (2026, Apache-2.0) | ESP32-S3, 240 MHz, 8 MB quad PSRAM | **~2170 CCRL** (95% CI 2129–2210) | 231 games vs 8 CCRL-rated engines at 1+0.6 — rigorous |
| Dog (van Heusden) | ESP32, 240 MHz dual-core, 320 kB RAM, 48 kB TT | ~2100 on device (2866 CCRL on PC, v3.0) | Author's estimate |
| Millennium The King Performance | ARM Cortex-M7 300 MHz, 348 KB RAM | "~2400–2450" | Retail claim, unverified, scale unknown |
| Kaggle FIDE × Google Efficient Chess AI winner (Niboshi, 2025) | 64 KB program, 5 MB RAM, 1 core, 10 s/move | Competition winner | NNUE-only eval, CNN + dense net in place of the feature transformer |

**CST Retro internals** (from its README):
- Board and search: 0x88 mailbox board. Classic search: iterative deepening, aspiration windows, PVS, qsearch, TT, null move, RFP, LMR, killers/history.
- Network: tiny NNUE (768→64→1, about 96 KB) trained on only **7.09M positions** labelled at depth 6. Already uses PIE SIMD.
- Memory: TT sits in 128 KB internal SRAM, because a PSRAM probe costs ~10x more. Real games run at 85–110 knps.

**Takeaway: ~2170 is the documented bar to beat.** It uses a small net, little training data and a basic search, which leaves a lot of room to improve (§8).

---

## 2. How strength is measured (benchmarks)
| Benchmark | What it measures | Notes |
|---|---|---|
| CCRL / CEGT / SPCC | Engine-vs-engine Elo | CCRL is the de-facto public list. Submissions are free and done by volunteer testers |
| TCEC / Chess.com CCC | Top-engine tournaments on big hardware | Invitation and strength gated |
| Lichess BOT rating | Bot vs humans/bots online | Public and live. The bot pool is not comparable to the human pool (DeepMind saw a gap) |
| Human move-matching % | Top-1 accuracy on held-out human games | Used by Maia, Allie and Chessformer |
| Puzzle accuracy | Lichess puzzle set by puzzle rating | Used by DeepMind's searchless paper |
| SPRT self-test | Is change X a real gain? | Daily dev loop (OpenBench, fastchess) |

**Elo scales are not interchangeable.** CCRL ≠ FIDE ≠ Lichess ≠ retail "Elo" claims. Any "best" claim must compare on the same anchored pool and method.

---

## 3. Weaknesses of existing bots
| # | Weakness | Evidence | Who it affects | Opening for us |
|---|---|---|---|---|
| 1 | Top-strength engines need big hardware | Lc0 needs a GPU. SF trains on 100B+ positions. AlphaZero used 5,000 TPUs | Everyone off-desktop | Strength per watt / per dollar on a microcontroller |
| 2 | Endgame and fortress blind spots | Müller group (ACG 2021, ACG 2023): Lc0 makes real errors vs tablebases; one case needed 12,000 simulations to switch to the winning move; "draws are much harder to play". Zahavy et al. 2023: AlphaZero misses Penrose fortress positions, while a diversity league (AZ_db) solves 2x as many | AlphaZero/Lc0, partly Stockfish | Tablebase-checked endgame test suite; fortress detection |
| 3 | Not human-like at reduced strength | Maia/Allie exist because engines weakened by random blunders play unnaturally | Every "difficulty level" in chess apps | Human-like levels on a handheld (Maia-2 is open) |
| 4 | Opaque: no explanation | McGrath et al. (PNAS 2022) and Schut et al. (arXiv 2310.16410) had to probe networks to recover concepts | All NNUE/NN engines | Explain moves using Silman-style imbalances (§4) |
| 5 | Search-free nets don't commit to plans | DeepMind 2024 needed a Stockfish fallback to convert won positions | Pure-network bots | Any real search fixes it — don't go search-free |
| 6 | Exploitable blind spots | Go: adversarial policies beat KataGo in >97% of games (Wang et al., ICLR 2023), and humans can copy the trick. Chess equivalent is under-studied | Neural engines | Research angle only; not v1 |
| 7 | LLMs can't play | Game Arena: illegal moves in check, middlegame collapse | LLM "chess bots" | Ignore |
| 8 | Draw death at the top | TCEC Superfinal 43% draws, even with forced unbalanced openings. CCRL top 30 within 46 Elo | Top engines | Explains why "#1 overall" is a dead end |

---

## 4. Strategy literature, mapped to engine parts
Bibliographic details are standard references and were not individually re-fetched.

### 4.1 Classic strategy
| Book | Core ideas | Where it lands in an engine |
|---|---|---|
| Capablanca — *Chess Fundamentals* (1921) | Piece values, simple endgames, plans | Material baseline, endgame tests |
| Lasker — *Manual of Chess* (1925) | Economy, the struggle, principles | — |
| Nimzowitsch — *My System* (1925) | Blockade, outposts, open files, the 7th rank, prophylaxis, overprotection | Classic hand-crafted eval terms: outposts, rook on open file, passed-pawn blockade |
| Kotov — *Think Like a Grandmaster* (1971) | Candidate moves, the "tree of analysis" | Move ordering and selective search (LMR = examine the best candidates deeper) |
| Watson — *Secrets of Modern Chess Strategy* (1998) | "Rule independence": concrete exceptions beat dogma | Why hand-written rules lost to learned NNUE eval |
| Silman — *How to Reassess Your Chess* (4th ed., 2010) | Imbalances: minor pieces, pawn structure, space, material, files, squares, development, initiative, king safety | A ready-made vocabulary for an explanation layer and for tiny-net input features |

### 4.2 Endgames
- Fine — *Basic Chess Endings* (1941)
- Shereshevsky — *Endgame Strategy*
- Dvoretsky — *Dvoretsky's Endgame Manual* (2003)

Key concepts: opposition, key squares, triangulation, rule of the square, fortresses. These feed weakness #2: a tablebase-checked test suite.

### 4.3 AI-era chess
- Sadler & Regan — *Game Changer* (New in Chess, 2019). Based on >2,000 unpublished AlphaZero games. Won ECF Book of the Year 2019 and FIDE's Averbakh–Boleslavsky Award.
- Sadler — *The Silicon Road to Chess Improvement* (2021)
- Kasparov — *Deep Thinking* (2017)

Key ideas: the h-pawn push, long-term sacrifices, restricting the opponent's mobility.

### 4.4 Computer-chess history and cognition
- Shannon, "Programming a Computer for Playing Chess", *Phil. Mag.* 41(314):256–275, 1950
- de Groot, *Thought and Choice in Chess* (1946/1965)
- Chase & Simon, "Perception in Chess" (1973) — chunking
- Botvinnik, *Computers, Chess and Long-Range Planning* (1970)
- Levy & Newborn, *How Computers Play Chess* (1991)
- Hsu, *Behind Deep Blue* (2002)
- Campbell, Hoane & Hsu, "Deep Blue", *Artificial Intelligence* 134:57–83, 2002 — averaged 126M positions/s vs Kasparov in 1997

**Learning to keep: modern engine strength does not come from reading books.** Stockfish replaced its hand-written evaluation with NNUE in v12 (2020-09-02), and SF12 then won "at least ten times more game pairs than it loses" vs SF11. Strength comes from data, search and testing. Books still pay off in three places:
- choosing input features for a *tiny* net
- explaining moves to humans
- building endgame and fortress test positions

---

## 5. Data archives
| Source | Size | License |
|---|---|---|
| Lichess open database — standard rated games | **8.22 billion** games, 2013-01 → 2026-09 (sum of the monthly counts file) | CC0 |
| Lichess evaluations | 416,442,401 positions evaluated by Stockfish | CC0 |
| Lichess puzzles | 6,157,341 rated, tagged puzzles | CC0 |
| Syzygy tablebases | 6-piece 149.2 GiB. 7-piece 16.7 TiB (Bojun Guo, 2018, ~$90k hardware). 8-piece estimated at 64 TB RAM + 2,000 TB storage | Free |
| ChessBench (DeepMind) | 10M games, 15B action-values | Open (GitHub) |
| CCRL / TCEC game archives | Engine-vs-engine PGNs | Free |

---

## 6. The math (chess *is* a math object)
- **Game theory.** Chess is finite, deterministic, two-player, zero-sum, with perfect information. Zermelo (1913): one outcome — White wins, Black wins, or draw — is forced with perfect play. It is not solved because of the numbers below. For contrast, checkers was solved in 2007 (Schaeffer et al., *Science*).
- **Size.** Shannon's game-tree estimate is **10^120** (about 10^3 move pairs per turn, over 40 moves). The number of legal positions is **(4.82 ± 0.03) × 10^44** (Tromp, from 2M random samples checked for legality).
- **Minimax / alpha-beta.** With perfect move ordering, alpha-beta visits b^⌈d/2⌉ + b^⌊d/2⌋ − 1 leaves instead of b^d (Levin 1961; Knuth & Moore, *AI* 6(4):293–326, 1975). In effect it searches twice as deep for the same work, so **move ordering is the whole game**.
- **Elo.** Expected score E = 1 / (1 + 10^(−Δ/400)), so Δ = 400·log10(E / (1−E)).
  - Likelihood of superiority: LOS = Φ((W−L) / √(W+L)). Draws carry no information in it.
  - SPRT (Wald 1945) decides "is this patch better?" with the fewest games.
- **MCTS / PUCT** (AlphaZero): choose a = argmax[ Q(s,a) + c·P(s,a)·√N(s) / (1+N(s,a)) ]. This is the multi-armed-bandit idea from UCB1 (Auer et al. 2002), steered by the network's policy P.
- **NNUE** (Yu Nasu, 2018, originally for shogi). The first layer is a sum of weight columns for the active (piece, square) features. A quiet move changes 2 features, a capture 3, castling 4. The update is accumulator −= W_old, += W_new, which is cheap. Weights are quantized to int8/int16 so SIMD can process 16–32 values per instruction.
- **Texel tuning.** Fit evaluation parameters by minimizing Σ (result − σ(K·eval))², with σ(x) = 1/(1+10^(−x/400)). This is logistic regression on game results.
- **Zobrist hashing** (1970). Position key = XOR of random 64-bit numbers. By the birthday bound, collisions appear around 2^32 stored positions, so transposition-table entries keep extra verification bits.
- **Bitboards.** A 64-bit integer represents the board. Sliding-piece attacks use "magic" multiply-and-shift perfect hashing.
- **Endgame geometry.**
  - King distance = Chebyshev distance max(|dx|,|dy|), which gives the "rule of the square".
  - Opposition and triangulation are parity arguments.
  - A knight always changes square colour, so it can never lose a tempo.
  - Bishops are colour-bound, which is why opposite-coloured bishop endings are drawish.
- **Tablebases.** Retrograde analysis: compute backward from mate. WDL files are used inside the search; DTZ files are for the 50-move rule at the root.

---

## 7. How fast can a new engine climb? (the real calibration)
Reckless (Rust, solo-led; uses the OpenBench testing framework and the bullet NNUE trainer):

| Version | Date | CCRL Blitz | Months since v0.1 |
|---|---|---|---|
| 0.1.0 | 2023-05-16 | ~2005 (SPCC) | 0 |
| 0.3.0 | 2023-11-06 | 2617 | 6 |
| 0.4.0 | 2023-12-13 | 2926 | 7 |
| 0.6.0 | 2024-03-22 | 3317 | 10 |
| 0.8.0 | 2025-08-29 | 3765 (#2) | 27 |

PlentyChess (#3) trained on 15B+ self-generated positions using bullet. Both relied on donated CPU time for testing.

**Takeaway: with modern tools, 2900+ CCRL on PC within ~7 months is a proven trajectory.** The top spot isn't.

---

## 8. The map: where "best" is winnable
| Lane | Current best (verified) | Can we be #1 by Jan 2027? | Compute needed | Fit with Kavin |
|---|---|---|---|---|
| A. Strongest engine on an ESP32-S3 microcontroller | ~2170 CCRL (CST Retro) | **Plausible.** Hypothesis, not yet proven | M3 Mac + the ESP32-S3 he owns | Mechatronics, ESP32 handheld, GitHub ESP32 lane, physical demo |
| B. Strong PC engine listed on CCRL | 3650 (Stockfish 19) | No. ~2800–3200 is realistic | M3 Mac + lynxS for testing | Same codebase as A; an independent external rating |
| C. Most human-like bot | 57.1% (Chessformer) | No. Maia-2 alone took 13 days on 2× A100 | Rented GPUs (money) | Research-flavoured, weak hardware tie |
| D. Best explainable coach | No benchmark exists | "Best" can't be measured | Low | Could be a layer on A later |

**Why A is winnable:** the documented bar uses a 7.09M-position dataset, a 768→64→1 net, a mailbox board and a basic search.

- **Levers to test** (each one gets a measured SPRT gain or is dropped):
  - a net trained on 100M–1B+ positions (the Lichess eval DB is CC0)
  - the modern search heuristics strong engines use
  - dual-core Lazy SMP (CST's README doesn't mention multithreading)
  - octal PSRAM (Kavin's N16R8) instead of quad
  - tiering the TT between SRAM and PSRAM
- **Main risks:**
  - The King Performance's unverified "~2450" claim may be real.
  - A "#1" claim stays credible only if the PGNs, the engine pool and the method are published so others can reproduce them.

---

## Sources
- CCRL 40/15 list: https://computerchess.org.uk/ccrl/4040/
- Stockfish 18: https://stockfishchess.org/blog/2026/stockfish-18/ · Stockfish 19: https://stockfishchess.org/blog/2026/stockfish-19/ · Stockfish 12: https://stockfishchess.org/blog/2020/stockfish-12/
- TCEC (secondary): https://en.wikipedia.org/wiki/Top_Chess_Engine_Championship
- DeepMind searchless chess: https://arxiv.org/abs/2402.04494
- Maia-2: https://arxiv.org/abs/2409.20553 · Allie: https://arxiv.org/abs/2410.03893 · Chessformer: https://arxiv.org/abs/2605.19091
- Game Arena: https://arxiv.org/abs/2609.31473
- Diversifying AI (fortresses / Penrose): https://arxiv.org/abs/2308.09175
- Acquisition of chess knowledge in AlphaZero: https://arxiv.org/abs/2111.09259 · Concept discovery & transfer: https://arxiv.org/abs/2310.16410
- Müller, "Evaluating Strong Engines Against Perfect Endgame Play" (2024): https://webdocs.cs.ualberta.ca/~mmueller/talks/2024-Evaluating-Engines-Endgame.pdf
- Adversarial policies (Go): https://arxiv.org/abs/2211.00241
- Reckless: https://github.com/codedeliveryservice/Reckless · PlentyChess: https://github.com/Yoshie2000/PlentyChess
- OpenBench: https://github.com/AndyGrant/OpenBench · bullet: https://github.com/jw1912/bullet
- CST Retro (ESP32-S3): https://github.com/Chris-Whittington-Chess/CST-Retro-esp32 · Dog: https://www.vanheusden.com/chess/Dog/
- King Performance specs (retail): https://chessmonger.com/articles/best-chess-computer/
- Kaggle Efficient Chess AI: https://www.kaggle.com/competitions/fide-google-efficiency-chess-ai-challenge
- ESP32-S3 PIE SIMD: https://developer.espressif.com/blog/2024/12/pie-introduction/
- Lichess database: https://database.lichess.org/ · Syzygy: https://www.chessprogramming.org/Syzygy_Bases
- Legal positions: https://github.com/tromp/ChessPositionRanking
- Alpha-beta, NNUE, match statistics, AlphaZero: https://www.chessprogramming.org/
- Shannon 1950 (Phil. Mag. 41(314)): https://historyofinformation.com/detail.php?entryid=94 · Deep Blue (AI 134): https://chessprogramming.org/Deep_Blue
- Game Changer: https://newinchess.com/game-changer
