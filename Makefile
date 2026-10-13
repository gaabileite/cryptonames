CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Isrc -isystem third_party
BUILD    := build
HEADERS  := $(wildcard src/*/*.h)

.PHONY: all test clean

all: $(BUILD)/game $(BUILD)/server

# Console version of the game
$(BUILD)/game: src/console/main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -lsqlite3 -o $@

# HTTP server for the front-end
$(BUILD)/server: src/server/server.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -lsqlite3 -pthread -o $@

$(BUILD)/tests: tests/tests.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(BUILD)/tests
	./$(BUILD)/tests

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
