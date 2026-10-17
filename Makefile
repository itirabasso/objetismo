CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
OMPFLAGS ?= -fopenmp
PNG_CFLAGS := $(shell pkg-config --cflags libpng)
PNG_LIBS := $(shell pkg-config --libs libpng)
PYTHON ?= python3

BIN = objetismo
SRC = objetismo.cpp

.PHONY: all tiles test asan clean

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $(PNG_CFLAGS) $< -o $@ $(LDFLAGS) $(PNG_LIBS)

# Address + undefined behavior sanitizers, single-threaded build.
$(BIN)-asan: $(SRC)
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(PNG_CFLAGS) $< -o $@ $(LDFLAGS) $(PNG_LIBS)

asan: $(BIN)-asan

# Regenerates resources/tiles.png and resources/tiles.tsv from the game data.
tiles:
	$(PYTHON) -I tools/build_tiles.py

test: $(BIN)
	$(PYTHON) -I tests/check_matches.py ./$(BIN)

clean:
	rm -f $(BIN) $(BIN)-asan
