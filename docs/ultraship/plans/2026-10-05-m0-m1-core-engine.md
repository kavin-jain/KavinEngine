# M0 + M1 Core Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use ultraship:subagent-driven-development (recommended) or ultraship:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A correct, perft-verified UCI chess engine. It searches with alpha-beta and uses PeSTO evaluation, builds for macOS and for the ESP32-S3, and comes with a working SPRT and gauntlet harness. Spec milestones M0 and M1.

**Architecture:** One portable C++17 core in `src/`. Only `platform.h` (time, I/O, memory, worker thread) is implemented per target: `pc/platform_pc.cpp` and `esp32/main.cpp`. Pseudo-legal move generation; legality is checked inside `Board::make`. Search is single-threaded negamax PVS with a transposition table. The big search arrays live in static storage so the ESP32 task stack stays small.

**Tech Stack:** C++17 (Apple clang 21 on the Mac, xtensa GCC 8.4 on ESP32), GNU Make, PlatformIO (espressif32 7.0.1, Arduino core 2.0.17), fastchess (SPRT/matches), Python 3 + pyserial (USB bridge).

**Spec:** `docs/ultraship/specs/2026-10-05-chess-engine-design.md`

**Machine reality (measured 2026-10-05):**
- MacBook Air M3: fanless, 4 performance + 4 efficiency cores, 16 GB RAM, 68 GB free disk.
- lynxS: unreachable from this network today.
- Consequences:
  - All matches run with `-concurrency 4` (one per performance core; macOS cannot pin threads to cores, so no `-use-affinity`).
  - Long runs happen plugged in, overnight.
  - Expect thermal throttling: compare engines only within the same run, never across runs.

---

## Risk register
| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| PlatformIO refuses `src_dir = ..` (shared core outside the project dir) | Medium | Medium | Task 10 step 2 falls back to a symlink `esp32/src -> ../src` plus a 1-file `esp32/main/` source dir |
| ESP32 internal RAM too small for 128 KB TT + 72 KB search stack | Medium | Medium | Task 10 prints free internal heap. Fallback: `-DTT_DEFAULT_BYTES=65536` |
| Magic-number search too slow at ESP32 boot (32-bit index path) | Low | Low | Task 10 measures boot time. If > 5 s, precompute magics on the PC and embed them (separate follow-up) |
| Board not plugged in / Kavin busy | High | Low | Device steps are marked **[DEVICE]**. If `pio device list` shows no board, record "deferred" in `docs/measurements.md` and continue the PC path |
| Fanless throttling skews timed tests | High | Low | Never compare numbers across runs; SPRT/gauntlets are self-relative within a run |

## Task dependencies
- Task 1 (ESP32 toolchain + USB echo) is independent; do it first.
- Tasks 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 are strictly sequential (each builds on the previous files).
- Task 10 (ESP32 engine port) needs Tasks 1 and 9.
- Task 11 (test tooling) is independent of 2–10 and can run any time after Task 1.
- Task 12 (SPRT self-check) needs 9 + 11. Task 13 (sanity gauntlet) needs 9 + 11. Task 14 (docs/wrap-up) is last.

## File map
| File | Responsibility |
|---|---|
| `src/types.h` | Basic types, move encoding, score constants |
| `src/bitboard.h/.cpp` | Bit helpers, leaper attack tables, magic slider attacks |
| `src/platform.h` | The per-target interface (declarations only) |
| `src/board.h/.cpp` | Position state: FEN I/O, Zobrist, make/unmake, attacks, draws, null move |
| `src/movegen.h/.cpp` | Pseudo-legal move generation, perft |
| `src/pesto_tables.h` | Generated PeSTO tables (data only) |
| `src/eval.h/.cpp` | Tapered PeSTO evaluation |
| `src/tt.h/.cpp` | Transposition table |
| `src/search.h/.cpp` | Time budget, quiescence, negamax PVS, iterative deepening |
| `src/uci.h/.cpp` | Engine init, UCI command handling, bench, perft divide |
| `pc/platform_pc.cpp`, `pc/main.cpp` | macOS/Linux target |
| `esp32/platformio.ini`, `esp32/main.cpp` | ESP32-S3 target |
| `tests/test.h`, `tests/test_main.cpp`, `tests/test_*.cpp` | Self-registering assert tests (`make test`) |
| `tools/uci_bridge.py` | fastchess ↔ USB serial relay |
| `tools/sprt.sh`, `tools/match.sh` | fastchess wrappers |
| `docs/testing.md`, `docs/measurements.md` | Pinned test setup and measured numbers |

---

### Task 1: ESP32 toolchain + USB-CDC echo check **[DEVICE for steps 4–5]**

**Files:**
- Create: `esp32/platformio.ini`
- Create: `esp32/main.cpp` (temporary echo firmware; replaced in Task 10)
- Create: `docs/measurements.md`

- [ ] **Step 1: Write `esp32/platformio.ini`.** The settings are the ones already proven on Kavin's board in `~/Projects/esp32-handheld`. The `src_dir` points at the repo root so Task 10 can share `src/`.

```ini
; ESP32-S3 (Edgehax S3-PRO, ESP32-S3-WROOM-1-N16R8) build of the engine.
[platformio]
src_dir = ..

[env:s3]
platform = espressif32 @ 7.0.1
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_size = 16MB
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB
build_src_filter = +<esp32/main.cpp>
build_unflags = -std=gnu++11 -Os
build_flags =
    -std=gnu++17
    -O2
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
monitor_speed = 115200
```

- [ ] **Step 2: Write the echo firmware `esp32/main.cpp`.**

```cpp
// Temporary USB-CDC echo firmware (Task 1). Replaced by the engine in Task 10.
#include <Arduino.h>

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
}

void loop() {
    static String line;
    while (Serial.available()) {
        char c = char(Serial.read());
        if (c == '\n') { Serial.println("echo " + line); line = ""; }
        else if (c != '\r') line += c;
    }
    delay(1);
}
```

- [ ] **Step 3: Build.**

Run: `cd ~/Projects/Chess-Engine/esp32 && pio run -e s3`
Expected: `[SUCCESS]`.
If PlatformIO rejects `src_dir = ..`, apply the fallback and rebuild:
- set `src_dir = main`
- `mkdir main && git mv main.cpp main/main.cpp`
- for Task 10, add `lib_extra_dirs`-free symlink `esp32/main/core -> ../../src`
Record which variant worked in `docs/measurements.md`.

- [ ] **Step 4 [DEVICE]: Detect the board.**

Run: `pio device list`
Expected: an entry like `/dev/cu.usbmodem*` (native USB-CDC; macOS has the driver built in).
If none appears, write `USB echo: deferred (board not connected)` in `docs/measurements.md`, skip Step 5, and continue with Task 2.

- [ ] **Step 5 [DEVICE]: Upload and check the echo.**

Run: `pio run -e s3 -t upload && sleep 3 && python3 -c "import serial,time; s=serial.Serial('$(ls /dev/cu.usbmodem* | head -1)',115200,timeout=2); time.sleep(1); s.write(b'hello\n'); print(s.readline())"`
Expected: `b'echo hello\r\n'`. If `import serial` fails, run Task 11 Step 1 (venv) first and use `.venv/bin/python`.

- [ ] **Step 6: Create `docs/measurements.md` and commit.**

```markdown
# Measurements
Numbers that later plans depend on. Every entry: date, command, result.

## 2026-10 — M0
- ESP32 build variant (src_dir): <".." or "symlink fallback">
- USB echo: <"ok on /dev/cu.usbmodemXXXX" or "deferred (board not connected)">
```

```bash
cd ~/Projects/Chess-Engine && git add esp32 docs/measurements.md && git commit -m "M0: ESP32 build skeleton and USB-CDC echo check"
```

---

### Task 2: Types, bitboards, magic attacks, PC platform, test harness

**Files:**
- Create: `src/types.h`, `src/bitboard.h`, `src/bitboard.cpp`, `src/platform.h`
- Create: `pc/platform_pc.cpp`, `pc/main.cpp` (stub until Task 9)
- Create: `tests/test.h`, `tests/test_main.cpp`, `tests/test_bitboard.cpp`
- Create: `Makefile`
- Modify: `.gitignore` (add `/engine-material`, `/build/`, `.venv/`, `.deps/`, `books/`, `results/sprt/`)

- [ ] **Step 1: Write `src/types.h`.**

```cpp
#pragma once
#include <cstdint>

using Bitboard = uint64_t;
using Key = uint64_t;
using Move = uint16_t;

#ifndef MAX_PLY
#define MAX_PLY 128
#endif
constexpr int MAX_MOVES = 256;
constexpr int MAX_GAME_PLY = 1024;

enum Color : int { WHITE = 0, BLACK = 1 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
// Piece index = color * 6 + type (0..11); NO_PIECE = 12.
constexpr int NO_PIECE = 12;
inline int make_piece(Color c, PieceType t) { return int(c) * 6 + int(t); }
inline PieceType type_of(int pc) { return PieceType(pc % 6); }
inline Color color_of(int pc) { return Color(pc / 6); }

// Squares: 0 = a1, 7 = h1, 56 = a8, 63 = h8.
constexpr int NO_SQ = 64;
inline int file_of(int sq) { return sq & 7; }
inline int rank_of(int sq) { return sq >> 3; }
inline Bitboard bb(int sq) { return Bitboard(1) << sq; }

// Move: bits 0-5 from, 6-11 to, 12-15 flags (chessprogramming.org "Encoding Moves").
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3,
    CAPTURE = 4, EP_CAPTURE = 5, PROMO = 8, PROMO_CAPTURE = 12  // promo | 0..3 = N, B, R, Q
};
constexpr Move NO_MOVE = 0;
inline Move make_move(int from, int to, int flags) { return Move(from | (to << 6) | (flags << 12)); }
inline int from_sq(Move m) { return m & 63; }
inline int to_sq(Move m) { return (m >> 6) & 63; }
inline int flags_of(Move m) { return m >> 12; }
inline bool is_capture(Move m) { return (flags_of(m) & 4) != 0; }
inline bool is_promo(Move m) { return (flags_of(m) & 8) != 0; }
inline PieceType promo_type(Move m) { return PieceType(KNIGHT + (flags_of(m) & 3)); }

constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MATE_BOUND = MATE - MAX_PLY;  // |score| >= this means a forced mate
```

- [ ] **Step 2: Write `src/platform.h`.**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Everything that differs between PC and ESP32 lives behind these functions.
int64_t now_ms();
bool read_line(std::string& line);                // blocking; false on EOF
void write_line(const std::string& line);         // appends '\n'; whole line is written atomically
void* alloc_mem(size_t bytes, bool fast);         // fast = internal SRAM on ESP32; free with std::free
void start_worker(void (*fn)(void*), void* arg);  // run fn(arg) on the background search thread
void join_worker();                               // wait for that thread (no-op if none running)
```

- [ ] **Step 3: Write `pc/platform_pc.cpp` and a stub `pc/main.cpp`.**

```cpp
// pc/platform_pc.cpp — platform.h for macOS/Linux.
#include "../src/platform.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool read_line(std::string& line) { return bool(std::getline(std::cin, line)); }

void write_line(const std::string& line) {
    std::string out = line + '\n';
    std::fwrite(out.data(), 1, out.size(), stdout);  // one call: stdio locks it, so lines never interleave
    std::fflush(stdout);
}

void* alloc_mem(size_t bytes, bool) { return std::malloc(bytes); }

static std::thread worker;
void start_worker(void (*fn)(void*), void* arg) { join_worker(); worker = std::thread(fn, arg); }
void join_worker() { if (worker.joinable()) worker.join(); }
```

```cpp
// pc/main.cpp — stub until Task 9 adds the UCI loop.
int main() { return 0; }
```

- [ ] **Step 4: Write `src/bitboard.h`.**

```cpp
#pragma once
#include "types.h"

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline int lsb(Bitboard b) { return __builtin_ctzll(b); }
inline int pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }

extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];

// "Fancy" magic bitboards. On 32-bit CPUs (ESP32) the index uses two 32-bit
// multiplies instead of one 64-bit multiply, which Xtensa would emulate.
struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;
    unsigned index(Bitboard occ) const {
#if UINTPTR_MAX == 0xFFFFFFFFu
        unsigned lo = unsigned(occ) & unsigned(mask);
        unsigned hi = unsigned(occ >> 32) & unsigned(mask >> 32);
        return (lo * unsigned(magic) ^ hi * unsigned(magic >> 32)) >> shift;
#else
        return unsigned(((occ & mask) * magic) >> shift);
#endif
    }
};
extern Magic RookMagics[64];
extern Magic BishopMagics[64];

inline Bitboard rook_attacks(int sq, Bitboard occ) { return RookMagics[sq].attacks[RookMagics[sq].index(occ)]; }
inline Bitboard bishop_attacks(int sq, Bitboard occ) { return BishopMagics[sq].attacks[BishopMagics[sq].index(occ)]; }
inline Bitboard queen_attacks(int sq, Bitboard occ) { return rook_attacks(sq, occ) | bishop_attacks(sq, occ); }

// Slow reference ray walk; used to build the tables and by tests.
Bitboard slider_attacks_slow(int sq, Bitboard occ, bool rook);

void init_bitboards();  // idempotent; call before anything else
```

- [ ] **Step 5: Write the test harness `tests/test.h` and `tests/test_main.cpp`.**

```cpp
// tests/test.h — tiny self-registering test framework (no dependencies).
#pragma once
#include <cstdio>
#include <utility>
#include <vector>

using TestFn = void (*)();
std::vector<std::pair<const char*, TestFn>>& registry();
extern int g_failures;

#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { std::printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); ++g_failures; } } while (0)
#define TEST(name) static void name(); \
    static const bool name##_registered = (registry().emplace_back(#name, name), true); \
    static void name()
```

```cpp
// tests/test_main.cpp
#include "test.h"
#include "../src/bitboard.h"

std::vector<std::pair<const char*, TestFn>>& registry() {
    static std::vector<std::pair<const char*, TestFn>> r;
    return r;
}
int g_failures = 0;

int main() {
    init_bitboards();  // Task 9 replaces this with engine_init()
    for (auto& t : registry()) {
        int before = g_failures;
        t.second();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.first);
    }
    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
```

- [ ] **Step 6: Write the failing test `tests/test_bitboard.cpp`.**

```cpp
#include "test.h"
#include "../src/bitboard.h"

TEST(leaper_attacks) {
    CHECK_EQ(popcount(KnightAttacks[0]), 2);    // a1: b3, c2
    CHECK_EQ(popcount(KnightAttacks[27]), 8);   // d4
    CHECK_EQ(popcount(KingAttacks[0]), 3);
    CHECK_EQ(popcount(KingAttacks[27]), 8);
    CHECK(PawnAttacks[0][12] == (bb(19) | bb(21)));  // white e2 attacks d3, f3
    CHECK(PawnAttacks[1][52] == (bb(43) | bb(45)));  // black e7 attacks d6, f6
    CHECK(PawnAttacks[0][8] == bb(17));              // a2 attacks only b3
}

TEST(magics_match_slow_rays) {
    uint64_t s = 12345;
    for (int i = 0; i < 20000; ++i) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        Bitboard occ = s & (s >> 3);  // sparse-ish random occupancy
        int sq = i & 63;
        CHECK(rook_attacks(sq, occ) == slider_attacks_slow(sq, occ, true));
        CHECK(bishop_attacks(sq, occ) == slider_attacks_slow(sq, occ, false));
    }
    CHECK_EQ(popcount(rook_attacks(0, 0)), 14);
    CHECK_EQ(popcount(bishop_attacks(27, 0)), 13);
}
```

- [ ] **Step 7: Write the `Makefile`.**

```make
# PC build. OpenBench/CCRL convention: `make EXE=<path>`; EVALFILE is accepted (used from M2).
EXE      ?= engine
EVALFILE ?=
ARCH     := $(shell uname -m)
ifeq ($(ARCH),arm64)
  NATIVE := -mcpu=native
else
  NATIVE := -march=native
endif
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -DNDEBUG $(NATIVE)
SRC      := $(wildcard src/*.cpp) pc/platform_pc.cpp
HDR      := $(wildcard src/*.h)

$(EXE): $(SRC) $(HDR) pc/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) pc/main.cpp -pthread

# Deliberately weak build (material-only eval) for the SPRT harness self-check.
material: $(SRC) $(HDR) pc/main.cpp
	$(CXX) $(CXXFLAGS) -DMATERIAL_ONLY -o engine-material $(SRC) pc/main.cpp -pthread

test: $(SRC) $(HDR) $(wildcard tests/*.cpp tests/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -o build/tests $(SRC) $(wildcard tests/*.cpp) -pthread
	./build/tests

.PHONY: material test
```

- [ ] **Step 8: Run the tests and verify they fail to link.**

Run: `cd ~/Projects/Chess-Engine && make test`
Expected: FAIL. Linker errors for `init_bitboards`, `slider_attacks_slow`, `KnightAttacks`, …

- [ ] **Step 9: Write `src/bitboard.cpp`.**

```cpp
#include "bitboard.h"
#include <cstdlib>
#include "platform.h"

Bitboard PawnAttacks[2][64], KnightAttacks[64], KingAttacks[64];
Magic RookMagics[64], BishopMagics[64];

namespace {

const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

uint64_t rng = 0x9E3779B97F4A7C15ull;
uint64_t rand64() {  // xorshift64*: fixed seed, so magics are identical on every run
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
    return rng * 2685821657736338717ull;
}

void init_magics(Magic* magics, Bitboard* table, bool rook) {
    Bitboard* occupancy = new Bitboard[4096];
    Bitboard* reference = new Bitboard[4096];
    int* epoch = new int[4096]();
    int attempt = 0;
    Bitboard* next = table;
    for (int sq = 0; sq < 64; ++sq) {
        Magic& m = magics[sq];
        Bitboard edges = ((0xFFull | 0xFF00000000000000ull) & ~(0xFFull << (8 * rank_of(sq))))
                       | ((0x0101010101010101ull | 0x8080808080808080ull) & ~(0x0101010101010101ull << file_of(sq)));
        m.mask = slider_attacks_slow(sq, 0, rook) & ~edges;
#if UINTPTR_MAX == 0xFFFFFFFFu
        m.shift = 32 - popcount(m.mask);
#else
        m.shift = 64 - popcount(m.mask);
#endif
        m.attacks = next;
        int size = 0;
        Bitboard b = 0;
        do {  // Carry-Rippler: enumerate every subset of the mask
            occupancy[size] = b;
            reference[size] = slider_attacks_slow(sq, b, rook);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);
        next += size;
        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.mask * m.magic) >> 56) < 6;)
                m.magic = rand64() & rand64() & rand64();
            ++attempt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < attempt) { epoch[idx] = attempt; m.attacks[idx] = reference[i]; }
                else if (m.attacks[idx] != reference[i]) break;  // collision: try another magic
            }
        }
    }
    delete[] occupancy;
    delete[] reference;
    delete[] epoch;
}

}  // namespace

Bitboard slider_attacks_slow(int sq, Bitboard occ, bool rook) {
    const int (*dirs)[2] = rook ? RookDirs : BishopDirs;
    Bitboard att = 0;
    for (int d = 0; d < 4; ++d) {
        int f = file_of(sq) + dirs[d][0], r = rank_of(sq) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            att |= bb(r * 8 + f);
            if (occ & bb(r * 8 + f)) break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return att;
}

void init_bitboards() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int sq = 0; sq < 64; ++sq) {
        int f = file_of(sq), r = rank_of(sq);
        auto add = [&](Bitboard& t, int df, int dr) {
            int nf = f + df, nr = r + dr;
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) t |= bb(nr * 8 + nf);
        };
        add(PawnAttacks[WHITE][sq], -1, 1); add(PawnAttacks[WHITE][sq], 1, 1);
        add(PawnAttacks[BLACK][sq], -1, -1); add(PawnAttacks[BLACK][sq], 1, -1);
        const int jumps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (const auto& j : jumps) add(KnightAttacks[sq], j[0], j[1]);
        for (int df = -1; df <= 1; ++df)
            for (int dr = -1; dr <= 1; ++dr)
                if (df || dr) add(KingAttacks[sq], df, dr);
    }
    // 102,400 rook + 5,248 bishop entries (~860 KB): PSRAM on ESP32, normal heap on PC.
    auto* rook_table = static_cast<Bitboard*>(alloc_mem(102400 * sizeof(Bitboard), false));
    auto* bishop_table = static_cast<Bitboard*>(alloc_mem(5248 * sizeof(Bitboard), false));
    if (!rook_table || !bishop_table) std::abort();
    init_magics(RookMagics, rook_table, true);
    init_magics(BishopMagics, bishop_table, false);
}
```

- [ ] **Step 10: Run the tests and verify they pass.**

Run: `make test`
Expected: `ok   leaper_attacks`, `ok   magics_match_slow_rays`, `all tests passed`.
If `-mcpu=native` is rejected by Apple clang, it only affects the `engine` target. Set `NATIVE := -mcpu=apple-m1` for arm64.

- [ ] **Step 11: Update `.gitignore` and commit.**

```bash
printf '/engine-material\n/build/\n.venv/\n.deps/\nbooks/\nresults/sprt/\n' >> .gitignore
git add -A && git commit -m "M0: types, magic bitboards, PC platform, test harness"
```

---

### Task 3: Board — FEN I/O and Zobrist keys

**Files:**
- Create: `src/board.h` (full interface; later tasks implement the rest)
- Create: `src/board.cpp` (FEN, keys, piece placement)
- Create: `tests/test_board.cpp`
- Modify: `tests/test_main.cpp` (call `Board::init()` after `init_bitboards()`)

- [ ] **Step 1: Write `src/board.h`.**

```cpp
#pragma once
#include <string>
#include "bitboard.h"

struct StateInfo { Key key; int castling, ep, halfmove, captured; };
enum CastlingRight : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

class Board {
public:
    Bitboard pieces[12];
    Bitboard colors[2];
    Bitboard occ;
    int mailbox[64];
    Color stm;
    int castling, ep, halfmove, fullmove;
    Key key;
    int game_ply;                       // number of entries used in history
    StateInfo history[MAX_GAME_PLY];    // ~24 KB: keep Boards in static storage on ESP32

    static void init();                 // Zobrist keys; call once after init_bitboards()
    // Parses a FEN (move counters optional). On false the board is unspecified: reset it.
    bool set_fen(const std::string& fen);
    std::string fen() const;
    Key compute_key() const;            // from scratch; tests compare it with the incremental key

    bool make(Move m);                  // false (board unchanged) if the move leaves our king in check
    void unmake(Move m);
    void make_null();
    void unmake_null();
    void trim_history();                // drop history older than the 50-move window

    bool attacked(int sq, Color by) const;
    int king_sq(Color c) const { return lsb(pieces[make_piece(c, KING)]); }
    bool in_check() const { return attacked(king_sq(stm), ~stm); }
    bool is_repetition() const;
    bool is_draw() const;               // 50-move rule, repetition, insufficient material
    bool has_non_pawn_material(Color c) const;

private:
    void put(int pc, int sq);
    void remove(int sq);
    void move_piece(int from, int to);
};

extern const char* const START_FEN;
std::string move_to_uci(Move m);
```

- [ ] **Step 2: Write the failing tests `tests/test_board.cpp`.**

```cpp
#include "test.h"
#include <string>
#include "../src/board.h"

static Board B;  // Board is ~25 KB: keep it off the stack

TEST(fen_roundtrip) {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    };
    for (const char* f : fens) {
        CHECK(B.set_fen(f));
        CHECK(B.fen() == f);
        CHECK(B.key == B.compute_key());
    }
}

TEST(fen_defaults_and_rejects) {
    CHECK(B.set_fen("8/8/8/4k3/8/8/8/4K3 w - -"));  // move counters optional
    CHECK(B.fen() == "8/8/8/4k3/8/8/8/4K3 w - - 0 1");
    CHECK(!B.set_fen("garbage"));
    CHECK(!B.set_fen("8/8/8/8/8/8/8/8 w - - 0 1"));               // no kings
    CHECK(!B.set_fen("8/8/8/4k3/8/8/8/4K3 x - - 0 1"));           // bad side
    CHECK(!B.set_fen("P7/8/8/4k3/8/8/8/4K3 w - - 0 1"));          // pawn on rank 8
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/4K2r w - - 0 1"));           // side to move in check: legal
    CHECK(!B.set_fen("4k3/8/8/8/8/8/8/4K2r b - - 0 1"));          // side not to move is in check: illegal
}

TEST(castling_rights_need_pieces_on_home_squares) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1"));  // no rooks: rights dropped
    CHECK(B.fen() == "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
}
```

- [ ] **Step 3: Call `Board::init()` from the test main.** In `tests/test_main.cpp`, add `#include "../src/board.h"`. Then change `init_bitboards();` to `init_bitboards(); Board::init();`.

- [ ] **Step 4: Run and verify the tests fail.**
Run: `make test`
Expected: link errors (`Board::set_fen` undefined).

- [ ] **Step 5: Write `src/board.cpp` (FEN, keys, placement).**

```cpp
#include "board.h"
#include <algorithm>
#include <cstring>
#include <sstream>

const char* const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace {
Key PieceKeys[12][64], CastleKeys[16], EpKeys[8], SideKey;
int CastlingMask[64];  // rights that survive a move touching this square
const char PIECE_CHARS[] = "PNBRQKpnbrqk";
}  // namespace

void Board::init() {
    uint64_t s = 0x2545F4914F6CDD1Dull;
    auto rnd = [&s]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; };
    for (auto& row : PieceKeys) for (auto& k : row) k = rnd();
    for (auto& k : CastleKeys) k = rnd();
    for (auto& k : EpKeys) k = rnd();
    SideKey = rnd();
    for (int& m : CastlingMask) m = WK | WQ | BK | BQ;
    CastlingMask[0] &= ~WQ;  CastlingMask[7] &= ~WK;  CastlingMask[4] &= ~(WK | WQ);
    CastlingMask[56] &= ~BQ; CastlingMask[63] &= ~BK; CastlingMask[60] &= ~(BK | BQ);
}

void Board::put(int pc, int sq) {
    pieces[pc] |= bb(sq); colors[color_of(pc)] |= bb(sq); occ |= bb(sq);
    mailbox[sq] = pc; key ^= PieceKeys[pc][sq];
}

void Board::remove(int sq) {
    int pc = mailbox[sq];
    pieces[pc] &= ~bb(sq); colors[color_of(pc)] &= ~bb(sq); occ &= ~bb(sq);
    mailbox[sq] = NO_PIECE; key ^= PieceKeys[pc][sq];
}

void Board::move_piece(int from, int to) { int pc = mailbox[from]; remove(from); put(pc, to); }

Key Board::compute_key() const {
    Key k = CastleKeys[castling];
    for (int sq = 0; sq < 64; ++sq) if (mailbox[sq] != NO_PIECE) k ^= PieceKeys[mailbox[sq]][sq];
    if (ep != NO_SQ) k ^= EpKeys[file_of(ep)];
    if (stm == BLACK) k ^= SideKey;
    return k;
}

bool Board::set_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string placement, side, cast, eps;
    if (!(ss >> placement >> side >> cast >> eps)) return false;
    int hm = 0, fm = 1;
    if (!(ss >> hm)) hm = 0;
    if (!(ss >> fm)) fm = 1;
    int mb[64];
    std::fill(mb, mb + 64, NO_PIECE);
    int rank = 7, file = 0, kings[2] = {0, 0};
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) return false;
            --rank; file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return false;
        } else {
            const char* p = std::strchr(PIECE_CHARS, c);
            if (!p || file > 7) return false;
            int pc = int(p - PIECE_CHARS);
            if (type_of(pc) == PAWN && (rank == 0 || rank == 7)) return false;
            if (type_of(pc) == KING) ++kings[color_of(pc)];
            mb[rank * 8 + file++] = pc;
        }
    }
    if (rank != 0 || file != 8 || kings[WHITE] != 1 || kings[BLACK] != 1) return false;
    if (side != "w" && side != "b") return false;
    Color us = side == "w" ? WHITE : BLACK;
    int ep_sq = NO_SQ;
    if (eps != "-") {
        if (eps.size() != 2 || eps[0] < 'a' || eps[0] > 'h') return false;
        if (eps[1] == (us == WHITE ? '6' : '3')) ep_sq = (eps[1] - '1') * 8 + (eps[0] - 'a');
    }

    std::memset(pieces, 0, sizeof pieces);
    colors[WHITE] = colors[BLACK] = occ = 0;
    key = 0;
    for (int sq = 0; sq < 64; ++sq) { mailbox[sq] = NO_PIECE; if (mb[sq] != NO_PIECE) put(mb[sq], sq); }
    stm = us;
    castling = 0;
    const int wk = make_piece(WHITE, KING), wr = make_piece(WHITE, ROOK);
    const int bk = make_piece(BLACK, KING), br = make_piece(BLACK, ROOK);
    for (char c : cast) {
        if (c == 'K' && mailbox[4] == wk && mailbox[7] == wr) castling |= WK;
        if (c == 'Q' && mailbox[4] == wk && mailbox[0] == wr) castling |= WQ;
        if (c == 'k' && mailbox[60] == bk && mailbox[63] == br) castling |= BK;
        if (c == 'q' && mailbox[60] == bk && mailbox[56] == br) castling |= BQ;
    }
    ep = ep_sq;
    halfmove = hm;
    fullmove = fm;
    game_ply = 0;
    key ^= CastleKeys[castling];
    if (ep != NO_SQ) key ^= EpKeys[file_of(ep)];
    if (stm == BLACK) key ^= SideKey;
    return !attacked(king_sq(~stm), stm);  // the side that just moved cannot be in check
}

std::string Board::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            int pc = mailbox[r * 8 + f];
            if (pc == NO_PIECE) { ++empty; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            s += PIECE_CHARS[pc];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    std::string c;
    if (castling & WK) c += 'K';
    if (castling & WQ) c += 'Q';
    if (castling & BK) c += 'k';
    if (castling & BQ) c += 'q';
    s += c.empty() ? "-" : c;
    s += ' ';
    if (ep == NO_SQ) s += '-';
    else { s += char('a' + file_of(ep)); s += char('1' + rank_of(ep)); }
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

bool Board::attacked(int sq, Color by) const {
    const Bitboard* p = pieces + int(by) * 6;
    if (PawnAttacks[~by][sq] & p[PAWN]) return true;
    if (KnightAttacks[sq] & p[KNIGHT]) return true;
    if (KingAttacks[sq] & p[KING]) return true;
    if (bishop_attacks(sq, occ) & (p[BISHOP] | p[QUEEN])) return true;
    return (rook_attacks(sq, occ) & (p[ROOK] | p[QUEEN])) != 0;
}

std::string move_to_uci(Move m) {
    if (m == NO_MOVE) return "0000";
    std::string s;
    s += char('a' + file_of(from_sq(m))); s += char('1' + rank_of(from_sq(m)));
    s += char('a' + file_of(to_sq(m)));   s += char('1' + rank_of(to_sq(m)));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}
```

- [ ] **Step 6: Run the tests and verify they pass.**
Run: `make test`
Expected: `ok   fen_roundtrip`, `ok   fen_defaults_and_rejects`, `ok   castling_rights_need_pieces_on_home_squares`, `all tests passed`.

- [ ] **Step 7: Commit.**
```bash
git add -A && git commit -m "M0: board FEN parsing/printing and Zobrist keys"
```

---

### Task 4: Make/unmake, move generation, perft

**Files:**
- Modify: `src/board.cpp` (append make/unmake/trim_history)
- Create: `src/movegen.h`, `src/movegen.cpp`
- Create: `tests/test_movegen.cpp`

- [ ] **Step 1: Write the failing tests `tests/test_movegen.cpp`.** The perft numbers come from chessprogramming.org "Perft Results" (verified 2026-10-05).

```cpp
#include "test.h"
#include <string>
#include "../src/movegen.h"

static Board B;

struct PerftCase { const char* fen; int depth; uint64_t nodes; };
static const PerftCase CASES[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
};

TEST(perft_suite) {
    for (const auto& c : CASES) {
        CHECK(B.set_fen(c.fen));
        CHECK_EQ(perft(B, c.depth), c.nodes);
        CHECK(B.fen() == c.fen);  // perft must leave the board untouched
    }
}

TEST(make_unmake_restores_everything) {
    CHECK(B.set_fen(CASES[1].fen));
    const std::string start = B.fen();
    const Key key = B.key;
    MoveList l1;
    generate(B, l1, false);
    for (int i = 0; i < l1.size; ++i) {
        if (!B.make(l1.moves[i])) continue;
        CHECK(B.key == B.compute_key());
        MoveList l2;
        generate(B, l2, false);
        for (int j = 0; j < l2.size; ++j) {
            if (!B.make(l2.moves[j])) continue;
            CHECK(B.key == B.compute_key());
            B.unmake(l2.moves[j]);
        }
        B.unmake(l1.moves[i]);
        CHECK(B.fen() == start);
        CHECK(B.key == key);
    }
}

TEST(captures_only_generation) {
    CHECK(B.set_fen(CASES[1].fen));
    MoveList l;
    generate(B, l, true);
    int legal = 0;
    for (int i = 0; i < l.size; ++i) {
        CHECK(is_capture(l.moves[i]) || is_promo(l.moves[i]));
        if (B.make(l.moves[i])) { ++legal; B.unmake(l.moves[i]); }
    }
    CHECK_EQ(legal, 8);  // Kiwipete depth-1 captures per chessprogramming.org Perft Results
}
```

- [ ] **Step 2: Run and verify the tests fail.**
Run: `make test`
Expected: compile error (`movegen.h` not found).

- [ ] **Step 3: Write `src/movegen.h`.**

```cpp
#pragma once
#include "board.h"

struct MoveList {
    Move moves[MAX_MOVES];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

// Pseudo-legal moves (Board::make rejects illegal ones).
// captures_only: captures plus queen promotions, for quiescence search.
void generate(const Board& b, MoveList& list, bool captures_only);
uint64_t perft(Board& b, int depth);
```

- [ ] **Step 4: Write `src/movegen.cpp`.**

```cpp
#include "movegen.h"

namespace {

void add_promos(MoveList& list, int from, int to, bool capture, bool queen_only) {
    int base = capture ? PROMO_CAPTURE : PROMO;
    list.add(make_move(from, to, base | 3));  // queen first
    if (queen_only) return;
    for (int p = 0; p < 3; ++p) list.add(make_move(from, to, base | p));
}

void add_targets(MoveList& list, int from, Bitboard targets, Bitboard enemies) {
    while (targets) {
        int to = pop_lsb(targets);
        list.add(make_move(from, to, (enemies & bb(to)) ? CAPTURE : QUIET));
    }
}

}  // namespace

void generate(const Board& b, MoveList& list, bool captures_only) {
    const Color us = b.stm, them = ~us;
    const Bitboard enemies = b.colors[them];
    const Bitboard targets = captures_only ? enemies : ~b.colors[us];
    const Bitboard* p = b.pieces + int(us) * 6;
    const int up = us == WHITE ? 8 : -8;
    const int promo_rank = us == WHITE ? 7 : 0, start_rank = us == WHITE ? 1 : 6;

    for (Bitboard pawns = p[PAWN]; pawns;) {
        int from = pop_lsb(pawns), to = from + up;
        bool promo = rank_of(to) == promo_rank;
        if (!(b.occ & bb(to))) {
            if (promo) add_promos(list, from, to, false, captures_only);
            else if (!captures_only) {
                list.add(make_move(from, to, QUIET));
                if (rank_of(from) == start_rank && !(b.occ & bb(to + up)))
                    list.add(make_move(from, to + up, DOUBLE_PUSH));
            }
        }
        for (Bitboard caps = PawnAttacks[us][from] & enemies; caps;) {
            int t = pop_lsb(caps);
            if (promo) add_promos(list, from, t, true, captures_only);
            else list.add(make_move(from, t, CAPTURE));
        }
        if (b.ep != NO_SQ && (PawnAttacks[us][from] & bb(b.ep)))
            list.add(make_move(from, b.ep, EP_CAPTURE));
    }
    for (Bitboard s = p[KNIGHT]; s;) { int f = pop_lsb(s); add_targets(list, f, KnightAttacks[f] & targets, enemies); }
    for (Bitboard s = p[BISHOP]; s;) { int f = pop_lsb(s); add_targets(list, f, bishop_attacks(f, b.occ) & targets, enemies); }
    for (Bitboard s = p[ROOK]; s;)   { int f = pop_lsb(s); add_targets(list, f, rook_attacks(f, b.occ) & targets, enemies); }
    for (Bitboard s = p[QUEEN]; s;)  { int f = pop_lsb(s); add_targets(list, f, queen_attacks(f, b.occ) & targets, enemies); }
    const int k = b.king_sq(us);
    add_targets(list, k, KingAttacks[k] & targets, enemies);

    if (captures_only || b.attacked(k, them)) return;
    // Castling: path empty, king not in check, transit square not attacked. The destination is checked by make().
    if (us == WHITE) {
        if ((b.castling & WK) && !(b.occ & 0x60ull) && !b.attacked(5, them)) list.add(make_move(4, 6, KING_CASTLE));
        if ((b.castling & WQ) && !(b.occ & 0x0Eull) && !b.attacked(3, them)) list.add(make_move(4, 2, QUEEN_CASTLE));
    } else {
        if ((b.castling & BK) && !(b.occ & (0x60ull << 56)) && !b.attacked(61, them)) list.add(make_move(60, 62, KING_CASTLE));
        if ((b.castling & BQ) && !(b.occ & (0x0Eull << 56)) && !b.attacked(59, them)) list.add(make_move(60, 58, QUEEN_CASTLE));
    }
}

uint64_t perft(Board& b, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate(b, list, false);
    uint64_t n = 0;
    for (int i = 0; i < list.size; ++i) {
        if (!b.make(list.moves[i])) continue;
        n += perft(b, depth - 1);
        b.unmake(list.moves[i]);
    }
    return n;
}
```

- [ ] **Step 5: Append make/unmake/trim_history to `src/board.cpp`.**

```cpp
bool Board::make(Move m) {
    StateInfo& st = history[game_ply];
    st.key = key; st.castling = castling; st.ep = ep; st.halfmove = halfmove; st.captured = NO_PIECE;
    const int from = from_sq(m), to = to_sq(m), flags = flags_of(m);
    const Color us = stm;
    const int pc = mailbox[from];

    key ^= SideKey;
    if (ep != NO_SQ) { key ^= EpKeys[file_of(ep)]; ep = NO_SQ; }
    ++halfmove;
    if (flags == EP_CAPTURE) {
        int cap = to + (us == WHITE ? -8 : 8);
        st.captured = mailbox[cap]; remove(cap); halfmove = 0;
    } else if (is_capture(m)) {
        st.captured = mailbox[to]; remove(to); halfmove = 0;
    }
    move_piece(from, to);
    if (type_of(pc) == PAWN) {
        halfmove = 0;
        if (flags == DOUBLE_PUSH) { ep = (from + to) / 2; key ^= EpKeys[file_of(ep)]; }
        else if (is_promo(m)) { remove(to); put(make_piece(us, promo_type(m)), to); }
    } else if (flags == KING_CASTLE) {
        move_piece(to + 1, to - 1);  // h-rook to f-file
    } else if (flags == QUEEN_CASTLE) {
        move_piece(to - 2, to + 1);  // a-rook to d-file
    }
    key ^= CastleKeys[castling];
    castling &= CastlingMask[from] & CastlingMask[to];
    key ^= CastleKeys[castling];
    stm = ~us;
    if (us == BLACK) ++fullmove;
    ++game_ply;
    if (attacked(king_sq(us), stm)) { unmake(m); return false; }
    return true;
}

void Board::unmake(Move m) {
    --game_ply;
    const StateInfo& st = history[game_ply];
    const int from = from_sq(m), to = to_sq(m), flags = flags_of(m);
    stm = ~stm;
    const Color us = stm;
    if (us == BLACK) --fullmove;
    if (is_promo(m)) { remove(to); put(make_piece(us, PAWN), to); }
    if (flags == KING_CASTLE) move_piece(to - 1, to + 1);
    else if (flags == QUEEN_CASTLE) move_piece(to + 1, to - 2);
    move_piece(to, from);
    if (flags == EP_CAPTURE) put(st.captured, to + (us == WHITE ? -8 : 8));
    else if (st.captured != NO_PIECE) put(st.captured, to);
    key = st.key; castling = st.castling; ep = st.ep; halfmove = st.halfmove;  // restore exactly
}

void Board::trim_history() {
    int keep = std::min(halfmove, game_ply);
    std::memmove(history, history + (game_ply - keep), size_t(keep) * sizeof(StateInfo));
    game_ply = keep;
}
```

- [ ] **Step 6: Run the tests and verify they pass.**
Run: `make test`
Expected: all ok, `all tests passed`.
If perft is off, debug with perft divide per position. The UCI `go perft` divide arrives in Task 9; until then add a temporary loop in a test. Compare against any known-good engine, e.g. `stockfish` if installed: `position fen …` then `go perft N`.

- [ ] **Step 7: Commit.**
```bash
git add -A && git commit -m "M0: make/unmake, pseudo-legal movegen, perft suite passes"
```

---

### Task 5: Draw detection and null move

**Files:**
- Modify: `src/board.cpp` (append)
- Create: `tests/test_draws.cpp`

- [ ] **Step 1: Write the failing tests `tests/test_draws.cpp`.**

```cpp
#include "test.h"
#include "../src/movegen.h"

static Board B;

static Move find(const char* uci) {
    MoveList l;
    generate(B, l, false);
    for (int i = 0; i < l.size; ++i) if (move_to_uci(l.moves[i]) == uci) return l.moves[i];
    return NO_MOVE;
}

TEST(repetition) {
    CHECK(B.set_fen(START_FEN));
    const char* seq[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int i = 0; i < 4; ++i) {
        CHECK(!B.is_repetition());
        Move m = find(seq[i]);
        CHECK(m != NO_MOVE && B.make(m));
    }
    CHECK(B.is_repetition());
    CHECK(B.is_draw());
}

TEST(fifty_move_and_material) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80"));
    CHECK(B.is_draw());
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1"));
    CHECK(B.is_draw());   // K+N vs K
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
    CHECK(!B.is_draw());  // K+R vs K is a win
    CHECK(B.has_non_pawn_material(WHITE));
    CHECK(!B.has_non_pawn_material(BLACK));
}

TEST(null_move_roundtrip) {
    CHECK(B.set_fen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"));
    const std::string before = B.fen();
    const Key key = B.key;
    B.make_null();
    CHECK(B.stm == BLACK && B.ep == NO_SQ);
    CHECK(B.key == B.compute_key());
    B.unmake_null();
    CHECK(B.fen() == before);
    CHECK(B.key == key);
}
```

- [ ] **Step 2: Run and verify the tests fail.**
Run: `make test`
Expected: link errors for `is_repetition`, `is_draw`, `make_null`, `unmake_null`, `has_non_pawn_material`.

- [ ] **Step 3: Append to `src/board.cpp`.**

```cpp
void Board::make_null() {
    StateInfo& st = history[game_ply];
    st.key = key; st.castling = castling; st.ep = ep; st.halfmove = halfmove; st.captured = NO_PIECE;
    key ^= SideKey;
    if (ep != NO_SQ) { key ^= EpKeys[file_of(ep)]; ep = NO_SQ; }
    halfmove = 0;  // repetition checks must not look back across a null move
    stm = ~stm;
    ++game_ply;
}

void Board::unmake_null() {
    --game_ply;
    const StateInfo& st = history[game_ply];
    stm = ~stm;
    key = st.key; castling = st.castling; ep = st.ep; halfmove = st.halfmove;
}

bool Board::is_repetition() const {
    // history[i].key is the position at ply i; same side to move every 2 plies; nothing repeats across an irreversible move.
    const int stop = game_ply - std::min(halfmove, game_ply);
    for (int i = game_ply - 4; i >= stop; i -= 2)
        if (history[i].key == key) return true;
    return false;
}

bool Board::is_draw() const {
    if (halfmove >= 100 || is_repetition()) return true;
    const Bitboard heavy = pieces[make_piece(WHITE, PAWN)] | pieces[make_piece(BLACK, PAWN)]
                         | pieces[make_piece(WHITE, ROOK)] | pieces[make_piece(BLACK, ROOK)]
                         | pieces[make_piece(WHITE, QUEEN)] | pieces[make_piece(BLACK, QUEEN)];
    return !heavy && popcount(occ) <= 3;  // K v K or K + minor v K
}

bool Board::has_non_pawn_material(Color c) const {
    const Bitboard* p = pieces + int(c) * 6;
    return (p[KNIGHT] | p[BISHOP] | p[ROOK] | p[QUEEN]) != 0;
}
```

- [ ] **Step 4: Run the tests and verify they pass.**
Run: `make test`
Expected: `all tests passed`.

- [ ] **Step 5: Commit.**
```bash
git add -A && git commit -m "M0: repetition, 50-move, insufficient material, null move"
```

---

### Task 6: PeSTO evaluation

**Files:**
- Create: `src/pesto_tables.h` (generated)
- Create: `src/eval.h`, `src/eval.cpp`
- Create: `tests/test_eval.cpp`
- Modify: `tests/test_main.cpp` (call `eval_init()`)

- [ ] **Step 1: Generate `src/pesto_tables.h` from chessprogramming.org.** The script checks that every table has exactly 64 values.

```bash
cd ~/Projects/Chess-Engine && python3 - <<'EOF'
import re, html, urllib.request
req = urllib.request.Request("https://www.chessprogramming.org/PeSTO%27s_Evaluation_Function",
                             headers={"User-Agent": "Mozilla/5.0"})
page = urllib.request.urlopen(req, timeout=60).read().decode("utf-8", "ignore")
code = html.unescape(re.sub(r"<[^>]+>", "", re.search(r"<pre[^>]*>(.*?mg_pawn_table.*?)</pre>", page, re.S).group(1)))
def nums(decl):
    return [int(v) for v in re.findall(r"-?\d+", code.split(decl)[1].split("};")[0])]
pieces = ["pawn", "knight", "bishop", "rook", "queen", "king"]
out = ["#pragma once",
       "// PeSTO piece-square tables (Ronald Friederich / Rofchade), from chessprogramming.org",
       "// \"PeSTO's Evaluation Function\". Bootstrap evaluation for M1 only; replaced by NNUE in M2.",
       "// Index 0 = a8: each table reads like a board diagram from White's side.", ""]
for name in ("mg_value", "eg_value"):
    v = nums(f"int {name}[6] ="); assert len(v) == 6
    out.append(f"constexpr int {name.upper()}[6] = {{" + ", ".join(map(str, v)) + "};")
for ph in ("mg", "eg"):
    out.append(f"constexpr int {ph.upper()}_PST[6][64] = {{")
    for p in pieces:
        v = nums(f"int {ph}_{p}_table[64] ="); assert len(v) == 64, (ph, p)
        out.append(f"    {{  // {p}")
        out += ["        " + ", ".join(f"{x:4d}" for x in v[r*8:(r+1)*8]) + "," for r in range(8)]
        out.append("    },")
    out.append("};")
open("src/pesto_tables.h", "w").write("\n".join(out) + "\n")
print("wrote src/pesto_tables.h")
EOF
grep -c "" src/pesto_tables.h && grep "MG_VALUE" src/pesto_tables.h
```
Expected: `wrote src/pesto_tables.h` and `constexpr int MG_VALUE[6] = {82, 337, 365, 477, 1025, 0};`.

- [ ] **Step 2: Write the failing tests `tests/test_eval.cpp`.**

```cpp
#include "test.h"
#include <cctype>
#include <sstream>
#include <string>
#include "../src/board.h"
#include "../src/eval.h"

static Board B;

// Colour-flipped FEN: mirror ranks, swap piece colours, side to move, castling, ep.
static std::string flip_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string place, side, cast, ep, hm, fm;
    ss >> place >> side >> cast >> ep >> hm >> fm;
    std::string ranks[8], cur;
    int r = 0;
    for (char c : place) { if (c == '/') { ranks[r++] = cur; cur.clear(); } else cur += c; }
    ranks[r] = cur;
    std::string out;
    for (int i = 7; i >= 0; --i) {
        for (char c : ranks[i]) out += std::isalpha(c) ? char(std::isupper(c) ? std::tolower(c) : std::toupper(c)) : c;
        if (i) out += '/';
    }
    std::string fc;
    for (char c : cast) fc += c == '-' ? '-' : char(std::isupper(c) ? std::tolower(c) : std::toupper(c));
    std::string sorted;  // canonical KQkq order
    for (char c : std::string("KQkq")) if (fc.find(c) != std::string::npos) sorted += c;
    if (sorted.empty()) sorted = "-";
    std::string fep = ep == "-" ? "-" : std::string(1, ep[0]) + (ep[1] == '3' ? "6" : "3");
    return out + (side == "w" ? " b " : " w ") + sorted + " " + fep + " " + hm + " " + fm;
}

TEST(eval_startpos_is_zero) {
    CHECK(B.set_fen(START_FEN));
    CHECK_EQ(evaluate(B), 0);
}

TEST(eval_is_colour_symmetric) {
    const char* fens[] = {
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    for (const char* f : fens) {
        CHECK(B.set_fen(f));
        int a = evaluate(B);
        CHECK(B.set_fen(flip_fen(f)));
        CHECK_EQ(evaluate(B), a);
    }
}

TEST(eval_material_sign) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1"));
    CHECK(evaluate(B) > 800);   // white to move, a queen up
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/Q3K3 b - - 0 1"));
    CHECK(evaluate(B) < -800);  // side-to-move relative
}
```

- [ ] **Step 3: Add `eval_init()` to the test main.** In `tests/test_main.cpp`, add `#include "../src/eval.h"`. Then change the init line to `init_bitboards(); Board::init(); eval_init();`.

- [ ] **Step 4: Run and verify the tests fail.**
Run: `make test`
Expected: compile error (`eval.h` not found).

- [ ] **Step 5: Write `src/eval.h` and `src/eval.cpp`.**

```cpp
// src/eval.h
#pragma once
#include "board.h"

void eval_init();                // build tables; call once at startup
int evaluate(const Board& b);    // centipawns, from the side to move's point of view
```

```cpp
// src/eval.cpp — tapered PeSTO evaluation (M1 bootstrap).
#include "eval.h"
#include "pesto_tables.h"

namespace {
int MgTable[12][64], EgTable[12][64];
const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};
}

void eval_init() {
    for (int t = PAWN; t <= KING; ++t)
        for (int sq = 0; sq < 64; ++sq) {
#ifdef MATERIAL_ONLY  // handicapped build for the SPRT harness self-check
            const int wm = 0, we = 0, bm = 0, be = 0;
#else
            const int wm = MG_PST[t][sq ^ 56], we = EG_PST[t][sq ^ 56];  // our a1 = 0, tables have a8 = 0
            const int bm = MG_PST[t][sq], be = EG_PST[t][sq];
#endif
            MgTable[t][sq] = MG_VALUE[t] + wm;
            EgTable[t][sq] = EG_VALUE[t] + we;
            MgTable[t + 6][sq] = MG_VALUE[t] + bm;
            EgTable[t + 6][sq] = EG_VALUE[t] + be;
        }
}

int evaluate(const Board& b) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    for (int pc = 0; pc < 12; ++pc)
        for (Bitboard x = b.pieces[pc]; x;) {
            int sq = pop_lsb(x);
            mg[color_of(pc)] += MgTable[pc][sq];
            eg[color_of(pc)] += EgTable[pc][sq];
            phase += PhaseInc[type_of(pc)];
        }
    const int us = b.stm, them = us ^ 1;
    const int mgp = phase > 24 ? 24 : phase;
    return ((mg[us] - mg[them]) * mgp + (eg[us] - eg[them]) * (24 - mgp)) / 24;
}
```

- [ ] **Step 6: Run the tests and verify they pass.**
Run: `make test`
Expected: `all tests passed`.

- [ ] **Step 7: Commit.**
```bash
git add -A && git commit -m "M1: tapered PeSTO evaluation (bootstrap, disclosed)"
```

---

### Task 7: Transposition table

**Files:**
- Create: `src/tt.h`, `src/tt.cpp`
- Create: `tests/test_tt.cpp`

- [ ] **Step 1: Write the failing tests `tests/test_tt.cpp`.**

```cpp
#include "test.h"
#include "../src/tt.h"

TEST(tt_store_probe_replace) {
    TT tt;
    CHECK(tt.resize(1 << 20, false));
    TTEntry e;
    CHECK(!tt.probe(0x1234, e));
    tt.store(0x1234, make_move(12, 28, DOUBLE_PUSH), 55, 6, BOUND_EXACT);
    CHECK(tt.probe(0x1234, e));
    CHECK(e.move == make_move(12, 28, DOUBLE_PUSH) && e.score == 55 && e.depth == 6 && e.bound == BOUND_EXACT);
    tt.store(0x1234, NO_MOVE, 10, 3, BOUND_LOWER);  // shallower non-exact: keep the deeper entry
    CHECK(tt.probe(0x1234, e) && e.depth == 6);
    tt.clear();
    CHECK(!tt.probe(0x1234, e));
}

TEST(tt_mate_scores_are_ply_relative) {
    CHECK_EQ(score_to_tt(MATE - 5, 3), MATE - 2);
    CHECK_EQ(score_from_tt(MATE - 2, 3), MATE - 5);
    CHECK_EQ(score_to_tt(-MATE + 5, 3), -MATE + 2);
    CHECK_EQ(score_from_tt(-MATE + 2, 3), -MATE + 5);
    CHECK_EQ(score_to_tt(120, 7), 120);
}
```

- [ ] **Step 2: Run and verify the tests fail.**
Run: `make test`
Expected: compile error (`tt.h` not found).

- [ ] **Step 3: Write `src/tt.h` and `src/tt.cpp`.**

```cpp
// src/tt.h
#pragma once
#include <cstddef>
#include "types.h"

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };
struct TTEntry { Key key; Move move; int16_t score; uint8_t depth; uint8_t bound; };  // 16 bytes

class TT {
public:
    bool resize(size_t bytes, bool fast);  // largest power-of-two entry count that fits; false if allocation fails
    void clear();
    bool probe(Key key, TTEntry& out) const;
    void store(Key key, Move move, int score, int depth, int bound);
private:
    TTEntry* table = nullptr;
    size_t mask = 0;
};

// Mate scores are stored relative to the node, not the root.
inline int score_to_tt(int s, int ply) { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
inline int score_from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }
```

```cpp
// src/tt.cpp
#include "tt.h"
#include <cstdlib>
#include <cstring>
#include "platform.h"

bool TT::resize(size_t bytes, bool fast) {
    size_t n = 1;
    while (n * 2 * sizeof(TTEntry) <= bytes) n *= 2;
    std::free(table);
    table = static_cast<TTEntry*>(alloc_mem(n * sizeof(TTEntry), fast));
    if (!table) { mask = 0; return false; }
    mask = n - 1;
    clear();
    return true;
}

void TT::clear() { if (table) std::memset(table, 0, (mask + 1) * sizeof(TTEntry)); }

bool TT::probe(Key key, TTEntry& out) const {
    if (!table) return false;
    const TTEntry& e = table[key & mask];
    if (e.bound == BOUND_NONE || e.key != key) return false;
    out = e;
    return true;
}

// ponytail: one entry per slot, depth-preferred only for the same key; buckets/aging if SPRT shows a gain (M3).
void TT::store(Key key, Move move, int score, int depth, int bound) {
    if (!table) return;
    TTEntry& e = table[key & mask];
    if (e.key == key && depth < e.depth && bound != BOUND_EXACT) return;
    if (move == NO_MOVE && e.key == key) move = e.move;
    e = TTEntry{key, move, int16_t(score), uint8_t(depth < 0 ? 0 : depth), uint8_t(bound)};
}
```

- [ ] **Step 4: Run the tests and verify they pass.**
Run: `make test`
Expected: `all tests passed`.

- [ ] **Step 5: Commit.**
```bash
git add -A && git commit -m "M1: transposition table with ply-relative mate scores"
```

---

### Task 8: Search — time budget, quiescence, PVS, iterative deepening

**Files:**
- Create: `src/search.h`, `src/search.cpp`
- Create: `tests/test_search.cpp`
- Modify: `tests/test_main.cpp` (add `search_init()` and a TT)

- [ ] **Step 1: Write the failing tests `tests/test_search.cpp`.**

```cpp
#include "test.h"
#include <cstdlib>
#include "../src/search.h"

static Board B;

static SearchResult run(const char* fen, int depth) {
    CHECK(B.set_fen(fen));
    clear_search_state();
    Limits l;
    l.depth = depth;
    return search(B, l, false);
}

TEST(finds_mate_in_one) {
    SearchResult r = run("6k1/4Rppp/8/8/8/8/5PPP/6K1 w - - 0 1", 3);
    CHECK(move_to_uci(r.best) == "e7e8");
    CHECK_EQ(r.score, MATE - 1);
}

TEST(finds_mate_in_two) {
    SearchResult r = run("7k/8/8/8/8/8/R7/1R4K1 w - - 0 1", 5);  // e.g. 1.Ra7 Kg8 2.Rb8#
    CHECK_EQ(r.score, MATE - 3);
}

TEST(bare_kings_is_draw) {
    SearchResult r = run("8/8/8/8/8/8/8/K6k w - - 0 1", 4);
    CHECK_EQ(r.score, 0);
    CHECK(r.best != NO_MOVE);
}

TEST(no_legal_moves_returns_no_move) {
    SearchResult r = run("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3);  // black is stalemated
    CHECK(r.best == NO_MOVE);
}

TEST(takes_free_queen) {
    SearchResult r = run("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 4);
    CHECK(move_to_uci(r.best) == "e4d5");
}

TEST(node_limit_is_respected) {
    CHECK(B.set_fen(START_FEN));
    clear_search_state();
    Limits l;
    l.nodes = 5000;
    SearchResult r = search(B, l, false);
    CHECK(r.nodes <= 5000 + 64);
    CHECK(r.best != NO_MOVE);
}

TEST(time_budget) {
    Limits l;
    l.time[WHITE] = 60000; l.inc[WHITE] = 600;
    TimeBudget t = compute_budget(l, WHITE);
    CHECK_EQ(t.soft, 3300);   // 60000/20 + 600/2
    CHECK_EQ(t.hard, 9900);   // 3 * soft, below 60000 - 50
    l.time[WHITE] = 40; l.inc[WHITE] = 0;
    t = compute_budget(l, WHITE);
    CHECK_EQ(t.hard, 1);
    CHECK_EQ(t.soft, 1);
    Limits m; m.movetime = 1000;
    t = compute_budget(m, BLACK);
    CHECK_EQ(t.soft, 950); CHECK_EQ(t.hard, 950);
    Limits d;  // depth-only search: no clock
    t = compute_budget(d, WHITE);
    CHECK_EQ(t.hard, -1);
}
```

- [ ] **Step 2: Initialise the search in the test main.** In `tests/test_main.cpp`, add `#include "../src/search.h"`. Change the init line to:
`init_bitboards(); Board::init(); eval_init(); search_init(); g_tt.resize(16u << 20, false);`

- [ ] **Step 3: Run and verify the tests fail.**
Run: `make test`
Expected: compile error (`search.h` not found).

- [ ] **Step 4: Write `src/search.h`.**

```cpp
#pragma once
#include <atomic>
#include "board.h"
#include "tt.h"

struct Limits {
    int64_t time[2] = {-1, -1}, inc[2] = {0, 0};  // ms; time -1 = no clock given
    int movestogo = 0;
    int depth = MAX_PLY - 2;
    int64_t movetime = 0;
    uint64_t nodes = 0;
    bool infinite = false;
};
struct TimeBudget { int64_t soft, hard; };  // ms since search start; -1 = unlimited
TimeBudget compute_budget(const Limits& l, Color us);

struct SearchResult { Move best; int score; int depth; uint64_t nodes; };

extern TT g_tt;
extern std::atomic<bool> g_stop;

void search_init();         // reduction table; call once
void clear_search_state();  // ucinewgame: TT, history, killers
// Searches a copy of `root`. Never returns NO_MOVE if a legal move exists. Prints UCI info lines if verbose.
SearchResult search(const Board& root, const Limits& limits, bool verbose);
```

- [ ] **Step 5: Write `src/search.cpp`.**

```cpp
#include "search.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include "eval.h"
#include "movegen.h"
#include "platform.h"

TT g_tt;
std::atomic<bool> g_stop{false};

namespace {

struct Searcher {
    Board board;
    Limits limits;
    TimeBudget budget;
    int64_t start;
    uint64_t nodes;
    bool stopped;
    int seldepth;
    Move killers[MAX_PLY][2];
    int16_t history[2][64][64];
    Move pv[MAX_PLY][MAX_PLY];
    int pv_len[MAX_PLY];
    uint8_t lmr[64][64];
};
Searcher S;  // static storage: these arrays must not live on the small ESP32 task stack

bool should_stop() {
    if (S.stopped) return true;
    if (S.limits.nodes && S.nodes >= S.limits.nodes) return S.stopped = true;
    if ((S.nodes & 1023) == 0 &&
        (g_stop.load(std::memory_order_relaxed) || (S.budget.hard >= 0 && now_ms() - S.start >= S.budget.hard)))
        S.stopped = true;
    return S.stopped;
}

void score_moves(const MoveList& list, int16_t* scores, Move tt_move, int ply) {
    for (int i = 0; i < list.size; ++i) {
        const Move m = list.moves[i];
        if (m == tt_move) scores[i] = 30000;
        else if (is_capture(m) || is_promo(m)) {  // MVV-LVA
            int victim = flags_of(m) == EP_CAPTURE ? PAWN : is_capture(m) ? int(type_of(S.board.mailbox[to_sq(m)])) : 0;
            int attacker = type_of(S.board.mailbox[from_sq(m)]);
            int promo = is_promo(m) && promo_type(m) == QUEEN ? 64 : 0;
            scores[i] = int16_t(20000 + victim * 8 - attacker + promo);
        } else if (m == S.killers[ply][0]) scores[i] = 19000;
        else if (m == S.killers[ply][1]) scores[i] = 18999;
        else scores[i] = S.history[S.board.stm][from_sq(m)][to_sq(m)];  // bounded to +-16384
    }
}

Move pick(MoveList& list, int16_t* scores, int i) {  // selection sort, one step
    int best = i;
    for (int j = i + 1; j < list.size; ++j) if (scores[j] > scores[best]) best = j;
    std::swap(list.moves[i], list.moves[best]);
    std::swap(scores[i], scores[best]);
    return list.moves[i];
}

void update_history(Move m, int bonus) {  // "gravity": keeps values within +-16384
    int16_t& h = S.history[S.board.stm][from_sq(m)][to_sq(m)];
    h = int16_t(h + bonus - h * std::abs(bonus) / 16384);
}

int qsearch(int alpha, int beta, int ply) {
    ++S.nodes;
    if (should_stop()) return 0;
    if (ply > S.seldepth) S.seldepth = ply;
    if (ply >= MAX_PLY - 1) return evaluate(S.board);
    const bool in_check = S.board.in_check();
    int best = -INF;
    if (!in_check) {  // stand pat
        best = evaluate(S.board);
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }
    MoveList list;
    generate(S.board, list, !in_check);  // in check: all evasions
    int16_t scores[MAX_MOVES];
    score_moves(list, scores, NO_MOVE, ply);
    int legal = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = pick(list, scores, i);
        if (!S.board.make(m)) continue;
        ++legal;
        int score = -qsearch(-beta, -alpha, ply + 1);
        S.board.unmake(m);
        if (S.stopped) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) { alpha = score; if (score >= beta) break; }
        }
    }
    if (in_check && legal == 0) return -MATE + ply;
    return best;
}

int negamax(int alpha, int beta, int depth, int ply, bool null_ok) {
    const bool pv_node = beta - alpha > 1;
    S.pv_len[ply] = ply;
    if (ply > 0) {
        if (S.board.is_draw()) return 0;
        if (ply >= MAX_PLY - 1) return evaluate(S.board);
    }
    const bool in_check = S.board.in_check();
    if (in_check) ++depth;  // check extension
    if (depth <= 0) return qsearch(alpha, beta, ply);
    ++S.nodes;
    if (should_stop()) return 0;
    if (ply > S.seldepth) S.seldepth = ply;

    TTEntry tte;
    Move tt_move = NO_MOVE;
    if (g_tt.probe(S.board.key, tte)) {
        tt_move = tte.move;
        const int s = score_from_tt(tte.score, ply);
        if (!pv_node && tte.depth >= depth &&
            (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && s >= beta) || (tte.bound == BOUND_UPPER && s <= alpha)))
            return s;
    }

    // Null-move pruning: if passing still fails high, this node is very likely a cut-node.
    if (!pv_node && !in_check && null_ok && depth >= 3 && S.board.has_non_pawn_material(S.board.stm)
        && evaluate(S.board) >= beta) {
        S.board.make_null();
        int s = -negamax(-beta, -beta + 1, depth - 1 - (3 + depth / 6), ply + 1, false);
        S.board.unmake_null();
        if (S.stopped) return 0;
        if (s >= beta) return s >= MATE_BOUND ? beta : s;
    }

    MoveList list;
    generate(S.board, list, false);
    int16_t scores[MAX_MOVES];
    score_moves(list, scores, tt_move, ply);
    Move quiets[64];
    int n_quiets = 0, legal = 0, best = -INF;
    const int alpha0 = alpha;
    Move best_move = NO_MOVE;
    for (int i = 0; i < list.size; ++i) {
        const Move m = pick(list, scores, i);
        if (!S.board.make(m)) continue;
        ++legal;
        const bool quiet = !is_capture(m) && !is_promo(m);
        const int new_depth = depth - 1;
        int score;
        if (legal == 1) {
            score = -negamax(-beta, -alpha, new_depth, ply + 1, true);
        } else {
            int r = 0;  // late move reductions for quiet, non-checking moves
            if (depth >= 3 && legal > 3 && quiet && !in_check && !S.board.in_check())
                r = std::max(0, std::min<int>(S.lmr[std::min(depth, 63)][std::min(legal, 63)], new_depth - 1));
            score = -negamax(-alpha - 1, -alpha, new_depth - r, ply + 1, true);
            if (score > alpha && r > 0) score = -negamax(-alpha - 1, -alpha, new_depth, ply + 1, true);
            if (score > alpha && score < beta) score = -negamax(-beta, -alpha, new_depth, ply + 1, true);
        }
        S.board.unmake(m);
        if (S.stopped) return 0;
        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                S.pv[ply][ply] = m;
                for (int j = ply + 1; j < S.pv_len[ply + 1]; ++j) S.pv[ply][j] = S.pv[ply + 1][j];
                S.pv_len[ply] = std::max(S.pv_len[ply + 1], ply + 1);
                if (score >= beta) {
                    if (quiet) {
                        if (S.killers[ply][0] != m) { S.killers[ply][1] = S.killers[ply][0]; S.killers[ply][0] = m; }
                        const int bonus = std::min(depth * depth, 1200);
                        update_history(m, bonus);
                        for (int q = 0; q < n_quiets; ++q) update_history(quiets[q], -bonus);
                    }
                    break;
                }
            }
        }
        if (quiet && n_quiets < 64) quiets[n_quiets++] = m;
    }
    if (legal == 0) return in_check ? -MATE + ply : 0;
    g_tt.store(S.board.key, best_move, score_to_tt(best, ply), depth,
               best >= beta ? BOUND_LOWER : best > alpha0 ? BOUND_EXACT : BOUND_UPPER);
    return best;
}

void print_info(int depth, int score) {
    const int64_t ms = now_ms() - S.start;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(S.seldepth) + " score ";
    if (score >= MATE_BOUND) s += "mate " + std::to_string((MATE - score + 1) / 2);
    else if (score <= -MATE_BOUND) s += "mate " + std::to_string(-(MATE + score) / 2);
    else s += "cp " + std::to_string(score);
    s += " nodes " + std::to_string(S.nodes) + " nps " + std::to_string(ms > 0 ? S.nodes * 1000 / uint64_t(ms) : 0)
       + " time " + std::to_string(ms) + " pv";
    for (int i = 0; i < S.pv_len[0]; ++i) s += " " + move_to_uci(S.pv[0][i]);
    write_line(s);
}

}  // namespace

TimeBudget compute_budget(const Limits& l, Color us) {
    const int64_t overhead = 50;
    if (l.movetime > 0) { int64_t t = std::max<int64_t>(1, l.movetime - overhead); return {t, t}; }
    if (l.infinite || l.time[us] < 0) return {-1, -1};
    const int64_t t = l.time[us], inc = l.inc[us];
    int64_t soft = (l.movestogo > 0 ? t / (l.movestogo + 1) : t / 20) + inc / 2;
    int64_t hard = std::max<int64_t>(1, std::min(3 * soft, t - overhead));
    return {std::min(soft, hard), hard};
}

void search_init() {
    for (int d = 0; d < 64; ++d)
        for (int m = 0; m < 64; ++m)
            S.lmr[d][m] = (d && m) ? uint8_t(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25) : 0;
}

void clear_search_state() {
    g_tt.clear();
    std::memset(S.history, 0, sizeof S.history);
    std::memset(S.killers, 0, sizeof S.killers);
}

SearchResult search(const Board& root, const Limits& limits, bool verbose) {
    S.board = root;
    S.limits = limits;
    S.start = now_ms();
    S.budget = compute_budget(limits, root.stm);
    S.nodes = 0;
    S.stopped = false;
    std::memset(S.killers, 0, sizeof S.killers);
    SearchResult res{NO_MOVE, 0, 0, 0};
    {  // fallback: first legal move, so we always have something to play
        MoveList l;
        generate(S.board, l, false);
        for (int i = 0; i < l.size && res.best == NO_MOVE; ++i)
            if (S.board.make(l.moves[i])) { S.board.unmake(l.moves[i]); res.best = l.moves[i]; }
    }
    if (res.best == NO_MOVE) return res;  // checkmated or stalemated at the root
    for (int d = 1; d <= limits.depth && d < MAX_PLY - 1; ++d) {
        S.seldepth = 0;
        const int score = negamax(-INF, INF, d, 0, true);
        const bool complete = !S.stopped;
        if (S.pv_len[0] > 0 && (complete || d == 1)) { res.best = S.pv[0][0]; res.score = score; res.depth = d; }
        if (!complete) break;
        if (verbose) print_info(d, score);
        if (S.budget.soft >= 0 && now_ms() - S.start >= S.budget.soft) break;
        if (limits.nodes && S.nodes >= limits.nodes) break;
    }
    res.nodes = S.nodes;
    return res;
}
```

- [ ] **Step 6: Run the tests and verify they pass.**
Run: `make test`
Expected: all `ok`, `all tests passed`.
If `finds_mate_in_two` fails, print `r.score` and `r.depth`. A score of `MATE - 3` needs depth ≥ 3 plus the check extension, and depth 5 is generous. Do not loosen the assertion.

- [ ] **Step 7: Commit.**
```bash
git add -A && git commit -m "M1: PVS search with TT, null move, LMR, quiescence, time budget"
```

---

### Task 9: UCI loop, bench, perft divide, PC binary

**Files:**
- Create: `src/uci.h`, `src/uci.cpp`
- Modify: `pc/main.cpp`
- Create: `tests/test_uci.cpp`
- Modify: `tests/test_main.cpp` (use `engine_init()`)

- [ ] **Step 1: Write the failing tests `tests/test_uci.cpp`.**

```cpp
#include "test.h"
#include "../src/uci.h"

static Board B;

TEST(uci_position_with_moves) {
    uci_command("position startpos moves e2e4 e7e5 g1f3");
    CHECK(uci_board().fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
    uci_command("position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1 moves a7a8q");
    CHECK(uci_board().fen() == "Q7/7k/8/8/8/8/8/K7 b - - 0 1");
    uci_command("position fen not a fen");  // invalid input falls back to startpos
    CHECK(uci_board().fen() == START_FEN);
}

TEST(bench_positions_are_valid) {
    for (int i = 0; i < BENCH_COUNT; ++i) CHECK(B.set_fen(BENCH_FENS[i]));
}
```

- [ ] **Step 2: Switch the test main to `engine_init()`.** In `tests/test_main.cpp`, replace the includes of `bitboard.h`, `board.h`, `eval.h` and `search.h` with `#include "../src/uci.h"`. Replace the whole init line with `engine_init();`.

- [ ] **Step 3: Run and verify the tests fail.**
Run: `make test`
Expected: compile error (`uci.h` not found).

- [ ] **Step 4: Write `src/uci.h`.**

```cpp
#pragma once
#include <string>
#include "search.h"

#ifndef TT_DEFAULT_BYTES
#define TT_DEFAULT_BYTES (16u << 20)
#endif
#ifndef TT_FAST
#define TT_FAST 0
#endif
#ifndef BENCH_DEPTH
#define BENCH_DEPTH 9
#endif
#ifndef ENGINE_NAME
#define ENGINE_NAME "Chess-Engine 0.1"
#endif

extern const char* const BENCH_FENS[];
extern const int BENCH_COUNT;

void engine_init();                       // idempotent: tables, keys, eval, search, TT
bool uci_command(const std::string& line);  // false on "quit"
void uci_loop();                          // reads lines via platform read_line until quit/EOF
const Board& uci_board();                 // current position (for tests)
uint64_t run_bench(int depth);            // prints "<nodes> nodes <nps> nps" (OpenBench format)
int uci_main(int argc, char** argv);      // PC entry: `engine bench [depth]` or the UCI loop
```

- [ ] **Step 5: Write `src/uci.cpp`.**

```cpp
#include "uci.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include "eval.h"
#include "movegen.h"
#include "platform.h"

// 32 positions taken in file order from the Lichess CC0 eval database (2026-10-05), 8 per piece-count band
// (>=26, 18-25, 10-17, <=9 pieces). Fixed forever: the bench node count is the functional-change signature.
const char* const BENCH_FENS[] = {
    "r1b2rk1/1p2bppp/p1nppn2/q7/2P1P3/N1N5/PP2BPPP/R1BQ1RK1 w - - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r2qk2r/3n2p1/1pp1p3/3pPpb1/P2P1nBp/1NB4P/1PP2P2/R3QR1K w kq f6 0 1",
    "1r2kb1r/pBp2ppp/4pn2/5b2/Q1pq4/6P1/PP1NPP1P/R1B2RK1 b k - 0 1",
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pp2pppp/2p5/3P4/3P4/8/PP2PPPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkb1r/pp2pp1p/5np1/2pp4/3P4/1P1BPN2/P1P2PPP/RNBQK2R b KQkq - 0 1",
    "1R4k1/3q1pp1/6n1/b2p2Pp/2pP2b1/p1P5/P1BQrPPB/5NK1 b - - 0 1",
    "1k1r1r2/pbp3pp/1p1q1p2/2p2Q2/4P3/1P1PB3/P1P3PP/4RRK1 w - - 0 1",
    "1R6/3q1ppk/6n1/b2p2Pp/2pP2b1/p1P5/P1B1rPPB/2Q2NK1 b - - 0 1",
    "r2k2r1/pppb1p1p/2p5/8/3Bn3/8/PPP2PPP/2KR1B1R b - - 0 1",
    "r1b2rk1/pp3ppp/1q2p3/2npP1N1/8/8/PPQ2PPP/R3RBK1 b - - 0 1",
    "8/p5p1/3kp1p1/PPp1np2/2P1p3/4P2P/3KBPP1/8 w - - 0 1",
    "8/p4pp1/3kp1p1/PPp1n3/2P1p3/4P2P/4BPP1/4K3 w - - 0 1",
    "8/p4pp1/3kp1p1/PPp1n3/2P1p3/4P2P/3KBPP1/8 b - - 0 1",
    "7r/1p3k2/p1bPR3/5p2/2B2P1p/8/PP4P1/3K4 b - - 0 1",
    "8/4r3/2R2pk1/6pp/3P4/6P1/5K1P/8 b - - 0 1",
    "8/1r6/2R2pk1/6pp/3P4/6P1/5K1P/8 w - - 0 1",
    "8/3B4/8/p4p1k/5P1p/Pb6/1P4P1/6K1 w - - 0 1",
    "3r4/1p3k2/p1bPR3/5p2/2B2P1p/8/PP4P1/3K4 w - - 0 1",
    "3r4/6k1/2bPR3/pp3p2/2B2P1p/P7/1P3KP1/8 w - - 0 1",
    "2r1r1k1/5ppp/8/8/Q7/8/5PPP/4R1K1 w - - 0 1",
    "2r1r1k1/3Q1p2/7R/4N1p1/P4n2/1q5P/5PP1/4R1K1 w - - 0 1",
    "6k1/6p1/8/4K3/4NN2/8/8/8 w - - 0 1",
    "6k1/4Rppp/8/8/8/8/5PPP/6K1 w - - 0 1",
    "6k1/6p1/6N1/4K3/4N3/8/8/8 b - - 0 1",
    "8/8/2N2k2/8/1p2p3/p7/K7/8 b - - 0 1",
    "8/5kp1/6N1/4K3/4N3/8/8/8 w - - 0 1",
    "8/4k3/8/4K3/8/4P3/8/8 b - - 0 1",
    "k1K5/8/8/1P6/8/8/8/8 b - - 0 1",
    "8/8/2b5/5B1k/1P3P1p/7K/6P1/8 w - - 0 1",
};
const int BENCH_COUNT = int(sizeof(BENCH_FENS) / sizeof(BENCH_FENS[0]));

namespace {

Board g_board;   // static: Board is ~25 KB
Board g_scratch;
Limits g_limits;

Move parse_move(Board& b, const std::string& s) {
    MoveList list;
    generate(b, list, false);
    for (int i = 0; i < list.size; ++i) if (move_to_uci(list.moves[i]) == s) return list.moves[i];
    return NO_MOVE;
}

void stop_search() { g_stop = true; join_worker(); g_stop = false; }  // reset: nothing is running now

void search_worker(void*) {
    SearchResult r = search(g_board, g_limits, true);
    write_line("bestmove " + move_to_uci(r.best));
}

void set_position(std::istringstream& ss) {
    std::string token, fen;
    ss >> token;
    if (token == "startpos") { fen = START_FEN; ss >> token; }
    else if (token == "fen") { while (ss >> token && token != "moves") fen += token + " "; }
    if (fen.empty() || !g_board.set_fen(fen)) {
        write_line("info string invalid position, using startpos");
        g_board.set_fen(START_FEN);
        return;
    }
    while (ss >> token) {
        Move m = parse_move(g_board, token);
        if (m == NO_MOVE || !g_board.make(m)) { write_line("info string illegal move " + token); return; }
        if (g_board.game_ply >= MAX_GAME_PLY - MAX_PLY - 8) g_board.trim_history();
    }
}

void perft_divide(int depth) {
    if (depth < 1) return;
    const int64_t t0 = now_ms();
    MoveList list;
    generate(g_board, list, false);
    uint64_t total = 0;
    for (int i = 0; i < list.size; ++i) {
        if (!g_board.make(list.moves[i])) continue;
        uint64_t n = perft(g_board, depth - 1);
        g_board.unmake(list.moves[i]);
        total += n;
        write_line(move_to_uci(list.moves[i]) + ": " + std::to_string(n));
    }
    const int64_t ms = std::max<int64_t>(1, now_ms() - t0);
    write_line("info string perft depth " + std::to_string(depth) + " nodes " + std::to_string(total) +
               " time " + std::to_string(ms) + " nps " + std::to_string(total * 1000 / uint64_t(ms)));
}

void start_go(std::istringstream& ss) {
    Limits l;
    std::string t;
    while (ss >> t) {
        int64_t v = 0;
        if (t == "infinite") { l.infinite = true; continue; }
        if (!(ss >> v)) break;
        if (t == "wtime") l.time[WHITE] = std::max<int64_t>(0, v);
        else if (t == "btime") l.time[BLACK] = std::max<int64_t>(0, v);
        else if (t == "winc") l.inc[WHITE] = std::max<int64_t>(0, v);
        else if (t == "binc") l.inc[BLACK] = std::max<int64_t>(0, v);
        else if (t == "movestogo") l.movestogo = int(std::max<int64_t>(0, v));
        else if (t == "depth") l.depth = int(std::clamp<int64_t>(v, 1, MAX_PLY - 2));
        else if (t == "nodes") l.nodes = uint64_t(std::max<int64_t>(1, v));
        else if (t == "movetime") l.movetime = std::max<int64_t>(1, v);
        else if (t == "perft") { perft_divide(int(v)); return; }
    }
    g_limits = l;
    g_stop = false;
    start_worker(search_worker, nullptr);
}

void set_option(std::istringstream& ss) {
    std::string t, name, value;
    ss >> t;  // "name"
    while (ss >> t && t != "value") name += (name.empty() ? "" : " ") + t;
    ss >> value;
    if (name == "Hash") {
        long mb = std::clamp(std::strtol(value.c_str(), nullptr, 10), 1L, 4096L);
        if (!g_tt.resize(size_t(mb) << 20, TT_FAST)) {
            write_line("info string hash allocation failed, using default size");
            g_tt.resize(TT_DEFAULT_BYTES, TT_FAST);
        }
    }
}

}  // namespace

void engine_init() {
    static bool done = false;
    if (done) return;
    done = true;
    init_bitboards();
    Board::init();
    eval_init();
    search_init();
    if (!g_tt.resize(TT_DEFAULT_BYTES, TT_FAST)) write_line("info string TT allocation failed");
    g_board.set_fen(START_FEN);
}

const Board& uci_board() { return g_board; }

bool uci_command(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "uci") {
        write_line("id name " ENGINE_NAME);
        write_line("id author Kavin Jain");
        write_line("option name Hash type spin default " + std::to_string(TT_DEFAULT_BYTES >> 20) + " min 1 max 4096");
        write_line("uciok");
    } else if (cmd == "isready") write_line("readyok");
    else if (cmd == "ucinewgame") { stop_search(); clear_search_state(); }
    else if (cmd == "position") { stop_search(); set_position(ss); }
    else if (cmd == "go") { stop_search(); start_go(ss); }
    else if (cmd == "stop") stop_search();
    else if (cmd == "setoption") { stop_search(); set_option(ss); }
    else if (cmd == "bench") { stop_search(); int d = 0; ss >> d; run_bench(d > 0 ? d : BENCH_DEPTH); }
    else if (cmd == "d") write_line(g_board.fen());
    else if (cmd == "quit") { stop_search(); return false; }
    else if (!cmd.empty()) write_line("info string unknown command: " + cmd);
    return true;
}

void uci_loop() {
    std::string line;
    while (read_line(line) && uci_command(line)) {}
    stop_search();
}

uint64_t run_bench(int depth) {
    uint64_t total = 0;
    const int64_t t0 = now_ms();
    for (int i = 0; i < BENCH_COUNT; ++i) {
        g_scratch.set_fen(BENCH_FENS[i]);
        clear_search_state();
        Limits l;
        l.depth = depth;
        total += search(g_scratch, l, false).nodes;
    }
    const int64_t ms = std::max<int64_t>(1, now_ms() - t0);
    write_line(std::to_string(total) + " nodes " + std::to_string(total * 1000 / uint64_t(ms)) + " nps");
    return total;
}

int uci_main(int argc, char** argv) {
    engine_init();
    if (argc > 1 && std::string(argv[1]) == "bench") {
        run_bench(argc > 2 ? std::atoi(argv[2]) : BENCH_DEPTH);
        return 0;
    }
    uci_loop();
    return 0;
}
```

- [ ] **Step 6: Replace `pc/main.cpp`.**

```cpp
#include "../src/uci.h"

int main(int argc, char** argv) { return uci_main(argc, argv); }
```

- [ ] **Step 7: Run the tests and verify they pass.**
Run: `make test`
Expected: `all tests passed`. Note: `uci_command("ucinewgame")` is not exercised in tests, so no worker thread is started.

- [ ] **Step 8: Build the engine and smoke-test UCI.**
Run: `make && printf 'uci\nisready\nposition startpos moves e2e4\ngo depth 8\n' | ./engine; sleep 1`
Expected: lines `uciok`, `readyok`, then `info depth 1 …` up to `info depth 8 …`, then `bestmove <move>`.
Note: stdin hits EOF right away. `uci_loop` then calls `stop_search()`, which sets `g_stop` and joins. On EOF the search may therefore stop early, but a `bestmove` line is still printed. For a full-depth check use: `(printf 'position startpos\ngo depth 8\n'; sleep 5; echo quit) | ./engine`.

- [ ] **Step 9: Calibrate the bench depth and record the signature.**
Run: `./engine bench`
Expected: `<N> nodes <M> nps`, taking ≤ ~15 s on the M3. If it takes longer than 15 s, lower `BENCH_DEPTH` in `src/uci.h` to 8, rebuild and rerun.
Record in `docs/measurements.md`: `PC bench (depth D): N nodes, M nps (MacBook Air M3, 2026-10-xx)`.

- [ ] **Step 10: Commit, with the bench signature in the message (OpenBench convention).**
```bash
git add -A && git commit -m "M1: UCI loop, bench, perft divide

Bench: <N>"
```

---

### Task 10: ESP32 engine port **[DEVICE for steps 4–7]**

**Files:**
- Modify: `esp32/platformio.ini` (`build_src_filter`, engine defines)
- Replace: `esp32/main.cpp` (platform implementation + Arduino entry)
- Modify: `docs/measurements.md`

- [ ] **Step 1: Point the ESP32 build at the shared core.** In `esp32/platformio.ini`:
- Change `build_src_filter = +<esp32/main.cpp>` to `build_src_filter = +<src/> +<esp32/main.cpp>`.
- Append these lines to `build_flags`:
```ini
    -DMAX_PLY=48
    -DTT_DEFAULT_BYTES=131072
    -DTT_FAST=1
    -DBENCH_DEPTH=6
```
`MAX_PLY=48` keeps the recursive search within a 72 KB task stack (~1.3 KB per ply).

- [ ] **Step 2: Replace `esp32/main.cpp` with the platform implementation.**

```cpp
// ESP32-S3 target: platform.h over the native USB-CDC port, search on core 0.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "../src/platform.h"
#include "../src/uci.h"

static SemaphoreHandle_t out_mutex = nullptr;
static SemaphoreHandle_t worker_done = nullptr;
static TaskHandle_t worker_task = nullptr;
static void (*worker_fn)(void*) = nullptr;
static void* worker_arg = nullptr;

int64_t now_ms() { return esp_timer_get_time() / 1000; }

bool read_line(std::string& line) {
    line.clear();
    for (;;) {
        int c = Serial.read();
        if (c < 0) { vTaskDelay(1); continue; }
        if (c == '\n') return true;
        if (c != '\r') line += char(c);
    }
}

void write_line(const std::string& line) {
    xSemaphoreTake(out_mutex, portMAX_DELAY);  // search task and UCI task both print
    Serial.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    Serial.write('\n');
    xSemaphoreGive(out_mutex);
}

void* alloc_mem(size_t bytes, bool fast) {
    return heap_caps_malloc(bytes, fast ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : MALLOC_CAP_SPIRAM);
}

static void worker_entry(void*) {
    worker_fn(worker_arg);
    write_line("info string stack_free " + std::to_string(uxTaskGetStackHighWaterMark(nullptr)));
    xSemaphoreGive(worker_done);
    vTaskDelete(nullptr);
}

void start_worker(void (*fn)(void*), void* arg) {
    join_worker();
    worker_fn = fn;
    worker_arg = arg;
    xTaskCreatePinnedToCore(worker_entry, "search", 72 * 1024, nullptr, 1, &worker_task, 0);
}

void join_worker() {
    if (!worker_task) return;
    xSemaphoreTake(worker_done, portMAX_DELAY);
    worker_task = nullptr;
}

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    out_mutex = xSemaphoreCreateMutex();
    worker_done = xSemaphoreCreateBinary();
    disableCore0WDT();  // the search task saturates core 0; don't let the idle watchdog reset the chip
    const int64_t t0 = now_ms();
    engine_init();
    write_line("info string ready init_ms " + std::to_string(now_ms() - t0) +
               " free_internal " + std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) +
               " free_psram " + std::to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void loop() { uci_loop(); }
```

- [ ] **Step 3: Build.**
Run: `cd esp32 && pio run -e s3`
Expected: `[SUCCESS]`. Record RAM/flash usage from the build summary in `docs/measurements.md`.
If `-std=gnu++17` is not applied (errors about `std::clamp` or structured bindings), run `pio run -e s3 -v | grep -o "std=[a-z+0-9]*" | sort -u` and fix the unflag.

- [ ] **Step 4 [DEVICE]: Flash and read the boot line.**
Run: `pio run -e s3 -t upload && sleep 2 && ../.venv/bin/python -c "import serial,time; s=serial.Serial('$(ls /dev/cu.usbmodem* | head -1)',115200,timeout=10); s.write(b'isready\n'); print(s.read_until(b'readyok'))"`
Expected: `readyok`. The boot line may have printed before we connected. If so, send `uci` and confirm `uciok`.
Then reset the board (button) with the port open, so the `info string ready init_ms … free_internal … free_psram …` line is captured. Record those numbers.
If `free_internal` < 20000 or TT allocation failed, set `-DTT_DEFAULT_BYTES=65536` and repeat.

- [ ] **Step 5 [DEVICE]: Device perft suite (depth 4, exact counts).** Uses the bridge from Task 11 Step 4. Do that step first if it hasn't been done yet.
Run (each position; this example is Kiwipete):
```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
(echo "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"; echo "go perft 4"; sleep 40; echo quit) \
  | ../.venv/bin/python ../tools/uci_bridge.py "$PORT" | grep "perft depth"
```
Expected node counts (same as the PC test): start 197281 · Kiwipete 4085603 · pos3 43238 · pos4 422333 · pos5 2103487 · pos6 3894594.
Record each `nps` figure. **M0 device gate: every count exact. Compare the nps against CST Retro's 418 knps.**

- [ ] **Step 6 [DEVICE]: Device bench and a timed search.**
Run: `(echo "bench"; sleep 120; echo quit) | ../.venv/bin/python ../tools/uci_bridge.py "$PORT"`
Then: `(echo "position startpos"; echo "go movetime 5000"; sleep 8; echo quit) | ../.venv/bin/python ../tools/uci_bridge.py "$PORT"`
Expected:
- the bench prints `<N> nodes <M> nps`;
- the timed search prints info lines, `bestmove …` and `info string stack_free <bytes>`, with `stack_free` > 4096.

Record the bench nodes, the search nps, `stack_free` and the depth reached in 5 s.

- [ ] **Step 7: Commit.**
```bash
cd .. && git add -A && git commit -m "M0: ESP32-S3 port — shared core, USB-CDC UCI, device perft verified"
```
If the board was not connected: commit the build-only state with the message `M0: ESP32-S3 port builds (device verification deferred)`. Leave the [DEVICE] steps unchecked.

---

### Task 11: Test tooling — venv, fastchess, opening book, sparring engine, scripts

**Files:**
- Create: `tools/uci_bridge.py`, `tools/sprt.sh`, `tools/match.sh`
- Create: `docs/testing.md`

- [ ] **Step 1: Python venv for pyserial** (Homebrew Python is externally managed, so no global pip).
Run: `cd ~/Projects/Chess-Engine && python3 -m venv .venv && .venv/bin/pip install -q pyserial && .venv/bin/python -c "import serial; print(serial.__version__)"`
Expected: a version number (e.g. `3.5`).

- [ ] **Step 2: Build fastchess into `.deps/` (project-local, gitignored).**
Run: `mkdir -p .deps && git clone --depth 1 https://github.com/Disservin/fastchess .deps/fastchess && make -C .deps/fastchess -j8 && .deps/fastchess/fastchess -version`
Expected: a version string. Record it in `docs/testing.md`.

- [ ] **Step 3: Opening book and sparring engine.**
```bash
mkdir -p books .deps/bbc
curl -L -o books/UHO_Lichess_4852_v1.epd.zip https://github.com/official-stockfish/books/raw/master/UHO_Lichess_4852_v1.epd.zip
unzip -o books/UHO_Lichess_4852_v1.epd.zip -d books && rm books/UHO_Lichess_4852_v1.epd.zip
wc -l books/UHO_Lichess_4852_v1.epd && shasum -a 256 books/UHO_Lichess_4852_v1.epd
curl -L -o .deps/bbc/bbc_1.1.c https://raw.githubusercontent.com/maksimKorzh/bbc/1.1/src/bbc_1.1.c
cc -O3 -o .deps/bbc/bbc .deps/bbc/bbc_1.1.c && printf 'uci\nquit\n' | .deps/bbc/bbc | grep -E "id name|uciok"
```
Expected:
- the book has 2632036 lines (matches official-stockfish/books README);
- a SHA-256 is printed;
- BBC prints `id name` and `uciok`.

If BBC fails to compile on clang, try `cc -O3 -std=gnu99`. Record whatever worked.

- [ ] **Step 4: Write `tools/uci_bridge.py`.**

```python
#!/usr/bin/env python3
"""Relay UCI between stdin/stdout (fastchess) and the ESP32 engine over USB-CDC serial.

Usage: uci_bridge.py /dev/cu.usbmodemXXXX
Exits non-zero if the serial link drops; fastchess then counts the game as a loss (conservative, by design).
"""
import os
import sys
import threading
import time

import serial


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: uci_bridge.py <serial-port>")
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = sys.argv[1], 115200, 0.1
    ser.dtr = ser.rts = False  # don't toggle the ESP32-S3 into reset/bootloader on open
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()

    def pump() -> None:
        buf = b""
        while True:
            try:
                buf += ser.read(4096)
            except serial.SerialException:
                os._exit(2)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                sys.stdout.write(line.rstrip(b"\r").decode(errors="replace") + "\n")
                sys.stdout.flush()

    threading.Thread(target=pump, daemon=True).start()
    for line in sys.stdin:
        try:
            ser.write(line.encode())
            ser.flush()
        except serial.SerialException:
            os._exit(2)
        if line.strip() == "quit":
            break
    time.sleep(0.2)


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Write `tools/sprt.sh` and `tools/match.sh`.**

```bash
#!/usr/bin/env bash
# SPRT: is NEW stronger than BASE?  Usage: tools/sprt.sh NEW_EXE BASE_EXE [ELO0] [ELO1] [TC]
# MacBook Air M3 is fanless with 4 P-cores: concurrency 4, run plugged in.
set -euo pipefail
cd "$(dirname "$0")/.."
NEW=$1 BASE=$2 ELO0=${3:-0} ELO1=${4:-5} TC=${5:-8+0.08}
mkdir -p results/sprt
.deps/fastchess/fastchess \
  -engine cmd="$NEW" name=new -engine cmd="$BASE" name=base \
  -each tc="$TC" option.Hash=16 \
  -openings file=books/UHO_Lichess_4852_v1.epd format=epd order=random \
  -rounds 30000 -repeat -concurrency "${CONCURRENCY:-4}" \
  -sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 model=normalized \
  -report penta=true -recover \
  -pgnout file="results/sprt/$(date +%Y-%m-%d_%H%M%S).pgn"
```

```bash
#!/usr/bin/env bash
# Fixed-length match for rating estimates. Usage: tools/match.sh OURS OPPONENT GAMES TC NAME
# Hash is set only for our engine (the opponent's option names may differ).
set -euo pipefail
cd "$(dirname "$0")/.."
OURS=$1 OPP=$2 GAMES=$3 TC=$4 NAME=$5
mkdir -p results
.deps/fastchess/fastchess \
  -engine cmd="$OURS" name=ours option.Hash=16 -engine cmd="$OPP" name=opponent \
  -each tc="$TC" \
  -openings file=books/UHO_Lichess_4852_v1.epd format=epd order=random \
  -rounds $((GAMES / 2)) -repeat -concurrency "${CONCURRENCY:-4}" \
  -report penta=true -recover \
  -pgnout file="results/$NAME.pgn" | tee "results/$NAME.log"
```

Run: `chmod +x tools/*.sh tools/uci_bridge.py`

- [ ] **Step 6: Write `docs/testing.md`.**

```markdown
# Testing setup (pinned)

| Item | Value |
|---|---|
| Match runner | fastchess `<version from Task 11 step 2>`, built in `.deps/fastchess` |
| Opening book | `UHO_Lichess_4852_v1.epd` (official-stockfish/books), 2,632,036 positions, SHA-256 `<hash>` |
| SPRT | `tools/sprt.sh`: STC 8+0.08, Hash 16, pentanomial `normalized`, alpha = beta = 0.05; gainers [0, 5], simplifications [-5, 0] |
| Concurrency | 4 (MacBook Air M3: 4 P-cores, fanless; macOS cannot pin threads) |
| Sparring engine (M1 gate) | BBC 1.1 (maksimKorzh/bbc, `src/bbc_1.1.c`), CCRL Blitz 2020 ±17 (list of 2026-10-03, open-source class) |
| Device link | `tools/uci_bridge.py <port>`; link failure ends the game as a loss |

Rules:
- A change merges only on SPRT H1.
- Never compare Elo across separate runs (thermal state differs).
```

- [ ] **Step 7: Commit.**
```bash
git add tools docs/testing.md && git commit -m "Tooling: fastchess/SPRT/match scripts, USB bridge, pinned test setup"
```

---

### Task 12: SPRT harness self-check (expected H1)

**Files:**
- Modify: `docs/testing.md` (append result)

- [ ] **Step 1: Build both binaries.**
Run: `make && make material && ls -la engine engine-material`
Expected: both binaries exist.

- [ ] **Step 2: Run the SPRT: PeSTO vs material-only.**
Run: `tools/sprt.sh ./engine ./engine-material 0 10`
Expected: it stops with `H1 was accepted` (PeSTO is far stronger), most likely within a few hundred games (~10–30 min). If it ends in H0, the harness or the engine is broken: stop and debug before Task 13.

- [ ] **Step 3: Record and commit.** Append the games played, Elo ± error and the result line to `docs/testing.md` under "Harness self-check".
```bash
git add docs/testing.md && git commit -m "Harness self-check: SPRT PeSTO vs material-only (H1)"
```

---

### Task 13: M1 sanity gauntlet vs BBC 1.1 (gate: estimate ≥ 2200)

**Files:**
- Create: `results/m1-vs-bbc.pgn`, `results/m1-vs-bbc.log` (committed: publication evidence)
- Modify: `docs/measurements.md`

- [ ] **Step 1: Run 1,000 games at 10+0.1** (~1.5–2 h at concurrency 4; plugged in).
Run: `tools/match.sh ./engine .deps/bbc/bbc 1000 10+0.1 m1-vs-bbc`
Expected: a final report with `Elo: X +/- Y` for `ours` vs `opponent`.

- [ ] **Step 2: Compute the estimate and apply the gate.**
- **Estimate** = 2020 + X (BBC's CCRL Blitz rating plus our Elo difference). It is approximate: 10+0.1 is not CCRL's time control, and the spec's M5 gauntlet does the real rating.
- **Pass:** estimate ≥ 2200, i.e. X ≥ +180, roughly a 74% score.
- **Fail** (X < 180): do not start M2. Check, in order:
  1. Do time losses appear in the log (`loses on time`)? If yes, the budget is wrong.
  2. Does `bench` stay deterministic across two runs?
  3. Does disabling null move (`depth >= 3` → `depth >= 99`) or LMR change the result in an SPRT? If yes, that feature has a bug.

- [ ] **Step 3: Record and commit.** Add to `docs/measurements.md`: games, W/D/L, Elo ± error, the estimate, pass or fail, and the date.
```bash
git add results/m1-vs-bbc.pgn results/m1-vs-bbc.log docs/measurements.md && git commit -m "M1 gate: sanity gauntlet vs BBC 1.1"
```

---

### Task 14: README, license, vault log, explain-it-back note

**Files:**
- Create: `README.md`, `LICENSE` (GPL-3.0 full text)
- Create: `docs/explain-it-back/M0-M1.md` (template Kavin fills in)
- Modify: `~/Documents/Obsidian Vault/Coding/Chess Engine.md` (Log)

- [ ] **Step 1: Fetch the GPL-3.0 text.**
Run: `curl -sL https://www.gnu.org/licenses/gpl-3.0.txt -o LICENSE && head -3 LICENSE`
Expected: `GNU GENERAL PUBLIC LICENSE` / `Version 3, 29 June 2007`.

- [ ] **Step 2: Write `README.md`.** Use real measured numbers only, copied from `docs/measurements.md`, and no claims beyond them.

```markdown
# Chess-Engine (working name)

A from-scratch UCI chess engine in C++17 that runs on a PC and on a $10 ESP32-S3 microcontroller.

**Status (M1):**
- Alpha-beta (PVS) search with a transposition table, null-move pruning, late-move reductions and quiescence search.
- PeSTO evaluation as a bootstrap; NNUE comes next.
- Sanity estimate ≈ <estimate> CCRL-anchored (1,000 games vs BBC 1.1, see `results/`).

## Build
- PC: `make` → `./engine` (UCI). `make test` runs the test suite. `./engine bench` prints the node-count signature.
- ESP32-S3: `cd esp32 && pio run -e s3 -t upload`, then talk UCI over USB (`tools/uci_bridge.py <port>`).

## Honesty notes
- Code written with AI assistance (Claude), directed, tested and reviewed by Kavin Jain.
- The M1 evaluation uses the published PeSTO tables (Ronald Friederich), credited in `src/pesto_tables.h`.
- From M2 the network trains on Stockfish evaluations from the Lichess CC0 database; this will be stated with every result.
- All rating claims come with the games (`results/`), the method (`docs/testing.md`) and confidence intervals.

## License
GPL-3.0
```

- [ ] **Step 3: Write `docs/explain-it-back/M0-M1.md`.** This is the template Kavin answers in his own words. Claude then checks the answers.

```markdown
# Explain it back — M0/M1 (answer in your own words, 1–3 sentences each)
1. What is a bitboard, and why is "where can this rook move?" fast with magic bitboards?
2. What does perft count, and why does a perft mismatch prove a move-generation bug?
3. Alpha-beta: why can the engine skip searching some moves without changing the result?
4. What does the transposition table store, and why are mate scores adjusted by ply?
5. Null-move pruning: what is the idea, and in which positions (zugzwang) does it fail?
6. What did the SPRT self-check prove, and why isn't one match against BBC a real rating?
```

- [ ] **Step 4: Log in the vault.** Append a dated entry under `## Log` in `~/Documents/Obsidian Vault/Coding/Chess Engine.md`. Include:
- M0/M1 done or failed per gate
- the PC bench signature and nps
- device perft and nps (or "deferred")
- the BBC gauntlet estimate
- the next step (M2 plan)

- [ ] **Step 5: Commit.**
```bash
git add README.md LICENSE docs/explain-it-back && git commit -m "Docs: README with honesty notes, GPL-3.0, explain-it-back template"
```

---

## Done-when for this plan (= spec M0 + M1)
- [ ] `make test` passes: perft suite exact, make/unmake/key invariants, eval symmetry, mate-in-1/2, TT, UCI parsing.
- [ ] `./engine bench` is deterministic and its signature is recorded.
- [ ] ESP32 build succeeds; [DEVICE] perft exact at depth 4 and device nps recorded (or explicitly deferred).
- [ ] SPRT self-check accepted H1.
- [ ] Gauntlet vs BBC 1.1: estimate ≥ 2200 (or the bug hunt is documented and M2 is blocked).
- [ ] README, LICENSE and the vault log are updated.
