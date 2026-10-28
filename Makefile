CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
BUILD    := build

CORE := src/map_data.cpp src/pathfinding.cpp src/game.cpp
GAME := $(CORE) src/db.cpp src/draw.cpp src/main.cpp

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf)

# MySQL client library for the leaderboard. Point MYSQL_CONFIG at another
# mysql_config to use a different install, e.g. the Homebrew one:
#   make MYSQL_CONFIG=/opt/homebrew/opt/mysql/bin/mysql_config
MYSQL_CONFIG ?= /usr/local/mysql/bin/mysql_config
MYSQL_CFLAGS := $(shell $(MYSQL_CONFIG) --cflags)
# libmysqlclient pulls in its own ssl/crypto, so only -L and -lmysqlclient are needed
MYSQL_LIBS_RAW := $(filter-out -lssl -lcrypto -lresolv,$(shell $(MYSQL_CONFIG) --libs))
# the library is not on the default search path, so bake in where to find it
MYSQL_LIBS   := $(MYSQL_LIBS_RAW) $(foreach d,$(patsubst -L%,%,$(filter -L%,$(MYSQL_LIBS_RAW))),-Wl,-rpath,$(d))

all: sneakpath tests

sneakpath: $(GAME) $(wildcard src/*.hpp)
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) $(SDL_CFLAGS) -o $@ $(GAME) $(MYSQL_LIBS) $(SDL_LIBS)

$(BUILD)/test_astar: tests/test_astar.cpp $(CORE) $(wildcard src/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_astar.cpp src/map_data.cpp src/pathfinding.cpp

$(BUILD)/test_game: tests/test_game.cpp $(CORE) $(wildcard src/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/test_game.cpp $(CORE)

# Needs a running MySQL with the test database (see README); skips if it can't connect
$(BUILD)/test_db: tests/test_db.cpp src/db.cpp $(CORE) $(wildcard src/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(MYSQL_CFLAGS) -o $@ tests/test_db.cpp src/db.cpp src/map_data.cpp $(MYSQL_LIBS)

tests: $(BUILD)/test_astar $(BUILD)/test_game

test: tests
	./$(BUILD)/test_astar | tail -2
	./$(BUILD)/test_game

test-db: $(BUILD)/test_db
	./$(BUILD)/test_db

run: sneakpath
	./sneakpath

clean:
	rm -rf $(BUILD) sneakpath

.PHONY: all tests test test-db run clean
