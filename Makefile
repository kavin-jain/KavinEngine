# PC build. OpenBench/CCRL convention: `make EXE=<path> EVALFILE=<net>`.
EXE         ?= engine
EVALFILE    ?= nets/leela4-256-l400.bin
NNUE_HIDDEN ?= 256
NNUE_KB     ?= 10
NNUE_OB     ?= 8
# Default of the UCI Threads option (cloud matches set it per side).
THREADS     ?= 1
# clang++ when installed: it vectorises the SCReLU dot product much better than g++ (+8.0 % nps on GitHub's
# runners, identical bench; nps workflow, 2026-10-08). An explicit CXX=... still wins.
ifeq ($(origin CXX),default)
  CXX := $(shell command -v clang++ > /dev/null 2>&1 && echo clang++ || echo c++)
endif
ARCH        := $(shell uname -m)
ifeq ($(ARCH),arm64)
  NATIVE := -mcpu=native
else
  NATIVE := -march=native
endif
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -DNDEBUG $(NATIVE)
CXXFLAGS += -DDEFAULT_THREADS=$(THREADS) -DNNUE_HIDDEN=$(NNUE_HIDDEN) -DNNUE_KING_BUCKETS=$(NNUE_KB) -DNNUE_OUTPUT_BUCKETS=$(NNUE_OB)
ifeq ($(TUNE),1)
  CXXFLAGS += -DTUNE  # search constants as UCI options, for SPSA (tools/spsa.py)
endif
SRC      := $(wildcard src/*.cpp) pc/platform_pc.cpp
HDR      := $(wildcard src/*.h)
NET_CPP  := build/net_$(basename $(notdir $(EVALFILE))).cpp
CORE_SRC := src/bitboard.cpp src/board.cpp src/movegen.cpp pc/platform_pc.cpp

$(EXE): $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(NET_CPP) pc/main.cpp -pthread

$(NET_CPP): $(EVALFILE) tools/bin2cpp.py
	@mkdir -p build
	python3 tools/bin2cpp.py $< $@

# Profile-guided build (`make pgo`): instrument, run bench, rebuild with the profile. Same search (same bench nodes),
# faster code. clang needs llvm-profdata (PROFDATA=llvm-profdata-18 if only the versioned name is installed).
PROFDATA ?= llvm-profdata
pgo: $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	@rm -rf build/pgo && mkdir -p build/pgo
ifneq ($(findstring clang,$(CXX)),)
	$(CXX) $(CXXFLAGS) -fprofile-instr-generate -o $(EXE) $(SRC) $(NET_CPP) pc/main.cpp -pthread
	LLVM_PROFILE_FILE=build/pgo/%p.profraw ./$(EXE) bench > /dev/null
	$(PROFDATA) merge -o build/pgo/default.profdata build/pgo/*.profraw
	$(CXX) $(CXXFLAGS) -fprofile-instr-use=build/pgo/default.profdata -o $(EXE) $(SRC) $(NET_CPP) pc/main.cpp -pthread
else
	$(CXX) $(CXXFLAGS) -fprofile-generate -fprofile-dir=build/pgo -o $(EXE) $(SRC) $(NET_CPP) pc/main.cpp -pthread
	./$(EXE) bench > /dev/null
	$(CXX) $(CXXFLAGS) -fprofile-use -fprofile-dir=build/pgo -fprofile-correction -Wno-missing-profile -o $(EXE) $(SRC) $(NET_CPP) pc/main.cpp -pthread
endif

# PeSTO-evaluation build: the SPRT base for the NNUE gate.
pesto: $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -DUSE_PESTO -o engine-pesto $(SRC) $(NET_CPP) pc/main.cpp -pthread

# Deliberately weak build (material-only eval) for the SPRT harness self-check.
material: $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -DUSE_PESTO -DMATERIAL_ONLY -o engine-material $(SRC) $(NET_CPP) pc/main.cpp -pthread

test: $(SRC) $(HDR) $(NET_CPP) $(wildcard tests/*.cpp tests/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -DNNUE_HIDDEN=$(NNUE_HIDDEN) -DNNUE_KING_BUCKETS=$(NNUE_KB) -DNNUE_OUTPUT_BUCKETS=$(NNUE_OB) -DEVALS_TXT='"$(basename $(EVALFILE)).evals.txt"' -o build/tests $(SRC) $(NET_CPP) $(wildcard tests/*.cpp) -pthread
	./build/tests

convert: tools/lichess_convert.cpp tools/chessboard_record.h tools/lichess_parse.h $(CORE_SRC) $(HDR)
	@mkdir -p build
	$(CXX) -std=c++17 -O3 -Wall -Wextra $(NATIVE) -o build/lichess_convert tools/lichess_convert.cpp $(CORE_SRC)

# Held-out loss of a quantised net (net loaded at run time, so any checkpoint works).
loss: tools/nnue_loss.cpp tools/chessboard_record.h src/nnue.cpp $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/nnue_loss_$(NNUE_HIDDEN) tools/nnue_loss.cpp src/nnue.cpp

# Self-play data with game results (one process per core; see the file header).
datagen: tools/datagen.cpp tools/chessboard_record.h $(SRC) $(HDR) $(NET_CPP)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/datagen tools/datagen.cpp $(SRC) $(NET_CPP) -pthread

.PHONY: pesto material test convert loss datagen
