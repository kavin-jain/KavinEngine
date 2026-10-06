# PC build. OpenBench/CCRL convention: `make EXE=<path> EVALFILE=<net>`.
EXE         ?= engine
EVALFILE    ?= nets/m2-256.bin
NNUE_HIDDEN ?= 256
ARCH        := $(shell uname -m)
ifeq ($(ARCH),arm64)
  NATIVE := -mcpu=native
else
  NATIVE := -march=native
endif
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -DNDEBUG $(NATIVE)
CXXFLAGS += -DNNUE_HIDDEN=$(NNUE_HIDDEN)
SRC      := $(wildcard src/*.cpp) pc/platform_pc.cpp
HDR      := $(wildcard src/*.h)
NET_CPP  := build/net_$(basename $(notdir $(EVALFILE))).cpp
CORE_SRC := src/bitboard.cpp src/board.cpp src/movegen.cpp pc/platform_pc.cpp

$(EXE): $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(NET_CPP) pc/main.cpp -pthread

$(NET_CPP): $(EVALFILE) tools/bin2cpp.py
	@mkdir -p build
	python3 tools/bin2cpp.py $< $@

# PeSTO-evaluation build: the SPRT base for the NNUE gate.
pesto: $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -DUSE_PESTO -o engine-pesto $(SRC) $(NET_CPP) pc/main.cpp -pthread

# Deliberately weak build (material-only eval) for the SPRT harness self-check.
material: $(SRC) $(HDR) pc/main.cpp $(NET_CPP)
	$(CXX) $(CXXFLAGS) -DUSE_PESTO -DMATERIAL_ONLY -o engine-material $(SRC) $(NET_CPP) pc/main.cpp -pthread

test: $(SRC) $(HDR) $(NET_CPP) $(wildcard tests/*.cpp tests/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -DNNUE_HIDDEN=$(NNUE_HIDDEN) -o build/tests $(SRC) $(NET_CPP) $(wildcard tests/*.cpp) -pthread
	./build/tests

convert: tools/lichess_convert.cpp tools/chessboard_record.h tools/lichess_parse.h $(CORE_SRC) $(HDR)
	@mkdir -p build
	$(CXX) -std=c++17 -O3 -Wall -Wextra $(NATIVE) -o build/lichess_convert tools/lichess_convert.cpp $(CORE_SRC)

# Held-out loss of a quantised net (net loaded at run time, so any checkpoint works).
loss: tools/nnue_loss.cpp tools/chessboard_record.h src/nnue.cpp $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o build/nnue_loss_$(NNUE_HIDDEN) tools/nnue_loss.cpp src/nnue.cpp

.PHONY: pesto material test convert loss
