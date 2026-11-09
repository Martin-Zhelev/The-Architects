# Labyrinth - quick build of the terminal edition and the tests (no raylib needed).
# The 3D app is built with CMake - see README.md.
#   make          build the terminal edition (bin/labyrinth)
#   make run      build and play it
#   make test     build and run the automated tests
#   make clean    remove build output

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2

LOGIC_SOURCES = src/maze.cpp src/game.cpp src/number_trail.cpp src/validation.cpp \
                src/campaign.cpp src/profile.cpp src/savegame.cpp src/custom_map.cpp
UI_SOURCES    = src/input.cpp src/ui.cpp src/main.cpp
HEADERS       = $(wildcard src/*.h)

ifeq ($(OS),Windows_NT)
    EXE = .exe
endif

GAME  = bin/labyrinth$(EXE)
TESTS = bin/labyrinth_tests$(EXE)

all: $(GAME)

$(GAME): $(LOGIC_SOURCES) $(UI_SOURCES) $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LOGIC_SOURCES) $(UI_SOURCES) -o $@

$(TESTS): $(LOGIC_SOURCES) tests/test_main.cpp $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LOGIC_SOURCES) tests/test_main.cpp -o $@

run: $(GAME)
	./$(GAME)

test: $(TESTS)
	./$(TESTS)

clean:
	rm -rf bin

.PHONY: all run test clean
