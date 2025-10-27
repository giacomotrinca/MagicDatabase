CXX = g++
CXXFLAGS = `pkg-config --cflags gtk4` -std=c++17 -Wall -Wno-unused-function -Wno-deprecated-declarations -g -I./src -I/usr/include/nlohmann
LDFLAGS = `pkg-config --libs gtk4` -lsqlite3 -lcurl -lstdc++fs
SRC = src/main.cpp src/database.cpp src/utils.cpp src/scryfall.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = magicdb

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)
