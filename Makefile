CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
OMPFLAGS ?= -fopenmp
PNG_CFLAGS := $(shell pkg-config --cflags libpng)
PNG_LIBS := $(shell pkg-config --libs libpng)
PYTHON ?= python3

# emcc if it is installed, otherwise the pinned Emscripten Docker image.
EMSDK_IMAGE = emscripten/emsdk:4.0.10
EMCC ?= $(if $(shell command -v emcc),emcc,docker run --rm -u $(shell id -u):$(shell id -g) \
	-v $(CURDIR):/src -w /src $(EMSDK_IMAGE) emcc)
EMFLAGS = -std=c++17 -O3 -Wall -Wextra -Wno-unknown-pragmas \
	-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
	-sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=4GB -sEXPORTED_FUNCTIONS=_malloc,_free \
	-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32,UTF8ToString,stringToNewUTF8

BIN = objetismo
SRC = objetismo.cpp
HDR = objetismo.h
WEB_ASSETS = web/tiles.png web/tiles.tsv web/sample.png

.PHONY: all tiles test test-wasm asan wasm web serve clean

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $(PNG_CFLAGS) $< -o $@ $(LDFLAGS) $(PNG_LIBS)

# Address + undefined behavior sanitizers, single-threaded build.
$(BIN)-asan: $(SRC) $(HDR)
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(PNG_CFLAGS) $< -o $@ $(LDFLAGS) $(PNG_LIBS)

asan: $(BIN)-asan

# Regenerates resources/tiles.png and resources/tiles.tsv from the game data.
tiles:
	$(PYTHON) -I tools/build_tiles.py

test: $(BIN)
	$(PYTHON) -I tests/check_matches.py ./$(BIN)

# WebAssembly build for the browser front end in web/. The output is committed
# so the site deploys as static files; rebuild it after changing objetismo.h.
wasm: web/objetismo.js

web/objetismo.js: web/objetismo_wasm.cpp $(HDR)
	$(EMCC) $(EMFLAGS) $< -o $@

# Checks that the WebAssembly build picks the same tiles as the native one.
test-wasm: $(BIN) web/objetismo.js
	$(PYTHON) -I tests/check_wasm.py ./$(BIN) web/objetismo.js

# Copies the tile set and the sample image next to the page (Vercel runs the
# same copy, see vercel.json).
web: web/objetismo.js $(WEB_ASSETS)

web/tiles.%: resources/tiles.%
	cp $< $@

web/sample.png: test.png
	cp $< $@

serve: web
	$(PYTHON) -m http.server -d web 8000

clean:
	rm -f $(BIN) $(BIN)-asan $(WEB_ASSETS)
