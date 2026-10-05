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
