CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BUILD    := build

CORE := src/map_data.cpp src/pathfinding.cpp src/game.cpp
GAME := $(CORE) src/draw.cpp src/main.cpp

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf)

all: sneakpath tests

sneakpath: $(GAME) $(wildcard src/*.hpp)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $(GAME) $(SDL_LIBS)

$(BUILD)/test_astar: tests/test_astar.cpp $(CORE) $(wildcard src/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_astar.cpp src/map_data.cpp src/pathfinding.cpp

$(BUILD)/test_game: tests/test_game.cpp $(CORE) $(wildcard src/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_game.cpp $(CORE)

tests: $(BUILD)/test_astar $(BUILD)/test_game

test: tests
	./$(BUILD)/test_astar | tail -2
	./$(BUILD)/test_game

run: sneakpath
	./sneakpath

clean:
	rm -rf $(BUILD) sneakpath

.PHONY: all tests test run clean
