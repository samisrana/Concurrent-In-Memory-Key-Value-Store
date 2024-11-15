# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -std=c++17 -march=native -mavx2 -pthread -O3
LDFLAGS = -lzstd -lstdc++fs

# Directories
INCLUDE_DIRS = -Iinclude
SRC_DIR = src
OBJ_DIR = obj

# Source files
SOURCES = main.cpp \
          $(SRC_DIR)/dictionary_codec.cpp \
          $(SRC_DIR)/benchmark.cpp

# Object files
OBJECTS = $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

# Executable name
OUTPUT = dictionary_codec

# Create necessary directories
$(shell mkdir -p $(OBJ_DIR)/src)

# Main target
$(OUTPUT): $(OBJECTS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

# Rule for main.cpp
$(OBJ_DIR)/main.o: main.cpp include/dictionary_codec.h include/benchmark.h
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Rule for dictionary_codec.cpp
$(OBJ_DIR)/$(SRC_DIR)/dictionary_codec.o: $(SRC_DIR)/dictionary_codec.cpp include/dictionary_codec.h
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Rule for benchmark.cpp
$(OBJ_DIR)/$(SRC_DIR)/benchmark.o: $(SRC_DIR)/benchmark.cpp include/benchmark.h include/dictionary_codec.h
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(OBJ_DIR) $(OUTPUT)

# Phony targets
.PHONY: clean
