# ============================================================
# Makefile — Simulador de Kernel & Scheduling Engine
# Compilador: g++ con C++17 y soporte POSIX threads (-pthread)
# ============================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -pthread
SRC      := src/main.cpp src/shared_state.cpp src/scheduler.cpp src/cli.cpp
OBJ      := $(SRC:.cpp=.o)
TARGET   := scheduler

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)
