SHELL := /bin/bash

CXX := g++
CXXFLAGS := `pkg-config --cflags gtk4` -std=c++17 -Wall -Wno-unused-function -Wno-deprecated-declarations -g -I./src -I/usr/include/nlohmann
LDFLAGS := `pkg-config --libs gtk4` `pkg-config --libs fontconfig` -lsqlite3 -lcurl -lstdc++fs
SRC := src/main.cpp src/database.cpp src/utils.cpp src/scryfall.cpp
OBJ := $(SRC:.cpp=.o)
TARGET := magicdb

.PHONY: all clean distclean

COLOR_BOLD := \033[1m
COLOR_RESET := \033[0m
COLOR_OK := \033[1;32m
COLOR_FAIL := \033[1;31m
COLOR_INFO := \033[1;34m
COLOR_FILE := \033[1;33m

all: banner $(TARGET)

banner:
	@printf "\n${COLOR_BOLD}==> Building MagicDatabase${COLOR_RESET}\n"

$(TARGET): $(OBJ)
	@printf "\n${COLOR_INFO}Linking:${COLOR_RESET} %s -> ${COLOR_BOLD}%s${COLOR_RESET}\n" "$^" "$(TARGET)"
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@printf "${COLOR_OK}Build complete.${COLOR_RESET}\n\n"

# Pretty compile rule: shows [i/N] and per-file timing; hides normal compiler output unless there's an error



%.o: %.cpp
	@total=$$(echo $(OBJ) | wc -w); idx=1; for f in $(OBJ); do if [ "$$f" = "$@" ]; then break; fi; idx=$$((idx+1)); done; idx_padded=$$(printf "%02d" $$idx); total_padded=$$(printf "%02d" $$total); printf "${COLOR_INFO}[${idx_padded}/${total_padded}]${COLOR_RESET} Compiling ${COLOR_FILE}%s${COLOR_RESET} ... " "$<"; tmp=$$(mktemp /tmp/magicdb-build.XXXXXX); start=$$(date +%s); $(CXX) $(CXXFLAGS) -c -o $@ $< >"$$tmp" 2>&1; rc=$$?; end=$$(date +%s); d=$$((end-start)); if [ $$rc -eq 0 ]; then printf "${COLOR_OK}OK${COLOR_RESET} (${COLOR_BOLD}%ds${COLOR_RESET})\n" $$d; rm -f "$$tmp"; else printf "${COLOR_FAIL}FAIL${COLOR_RESET}\n"; cat "$$tmp"; rm -f "$$tmp"; exit $$rc; fi

clean:
	@printf "${COLOR_INFO}Cleaning...${COLOR_RESET}\n"; \
	rm -f $(OBJ) $(TARGET); \
	printf "${COLOR_OK}Cleaned.${COLOR_RESET}\n"
