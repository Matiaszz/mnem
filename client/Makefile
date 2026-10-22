CXX ?= g++
CXXFLAGS ?= -O2 -std=c++20 -Wall -Wextra

TARGET = out/mnem-cli
SRC = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
