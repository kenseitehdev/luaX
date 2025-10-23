# ======================================
# LuaX Cross-Platform Makefile (Final)
# ======================================

# Project info
TARGET_NAME := luaX
SRC_DIR     := src
LIB_DIR     := lib
INC_DIR     := include
BIN_DIR     := bin

# Platform build dirs
BUILD_MAC   := build/macos
BUILD_LINUX := build/linux

# Toolchains
CC_MAC      := clang
CC_LINUX    := x86_64-unknown-linux-gnu-gcc
STRIP       := strip

# Common Flags
CFLAGS_COMMON := -std=c11 -Wall -Wextra -O2 -I$(INC_DIR) -DHAVE_VM_LOAD_AND_RUN_FILE \
                 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
                 -Wno-unused-function -Wno-unused-variable -Wno-implicit-function-declaration

# macOS Flags
CFLAGS_MAC  := $(CFLAGS_COMMON)
LDFLAGS_MAC := -lm

# Linux Flags
CFLAGS_LINUX := $(CFLAGS_COMMON)
LDFLAGS_LINUX := -lm -ldl

# Sources
SRCS_SRC    := $(wildcard $(SRC_DIR)/*.c)
SRCS_LIB    := $(wildcard $(LIB_DIR)/*.c)

OBJS_SRC_MAC    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_MAC)/src_%.o,$(SRCS_SRC))
OBJS_LIB_MAC    := $(patsubst $(LIB_DIR)/%.c,$(BUILD_MAC)/lib_%.o,$(SRCS_LIB))
OBJS_MAC        := $(OBJS_SRC_MAC) $(OBJS_LIB_MAC)

OBJS_SRC_LINUX  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_LINUX)/src_%.o,$(SRCS_SRC))
OBJS_LIB_LINUX  := $(patsubst $(LIB_DIR)/%.c,$(BUILD_LINUX)/lib_%.o,$(SRCS_LIB))
OBJS_LINUX      := $(OBJS_SRC_LINUX) $(OBJS_LIB_LINUX)

# Binaries
BIN_MAC    := $(BIN_DIR)/$(TARGET_NAME)-macos
BIN_LINUX  := $(BIN_DIR)/$(TARGET_NAME)-linux

# Pretty colors
YELLOW = \033[1;33m
GREEN  = \033[1;32m
RED    = \033[1;31m
RESET  = \033[0m

# ======================================
# Default Targets
# ======================================

all: mac linux

mac: $(BIN_MAC)
	@echo "$(GREEN)[✔] macOS build complete: $(BIN_MAC)$(RESET)"

linux: $(BIN_LINUX)
	@echo "$(GREEN)[✔] Linux build complete: $(BIN_LINUX)$(RESET)"

# ======================================
# macOS build
# ======================================

$(BIN_MAC): $(OBJS_MAC) | $(BIN_DIR)
	$(CC_MAC) $(CFLAGS_MAC) -o $@ $(OBJS_MAC) $(LDFLAGS_MAC)
	$(STRIP) $@

$(BUILD_MAC)/src_%.o: $(SRC_DIR)/%.c | $(BUILD_MAC)
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@

$(BUILD_MAC)/lib_%.o: $(LIB_DIR)/%.c | $(BUILD_MAC)
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@

# ======================================
# Linux build (cross-compile)
# ======================================

$(BIN_LINUX): $(OBJS_LINUX) | $(BIN_DIR)
	@if ! command -v $(CC_LINUX) >/dev/null 2>&1; then \
		echo "$(YELLOW)⚠ Cross-compiler $(CC_LINUX) not found!$(RESET)"; \
		echo "   Install with: brew tap messense/macos-cross-toolchains && brew install x86_64-unknown-linux-gnu"; \
		exit 1; \
	fi
	$(CC_LINUX) $(CFLAGS_LINUX) -o $@ $(OBJS_LINUX) $(LDFLAGS_LINUX)
	$(STRIP) $@

$(BUILD_LINUX)/src_%.o: $(SRC_DIR)/%.c | $(BUILD_LINUX)
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

$(BUILD_LINUX)/lib_%.o: $(LIB_DIR)/%.c | $(BUILD_LINUX)
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

# ======================================
# Directories
# ======================================

$(BUILD_MAC):
	mkdir -p $(BUILD_MAC)

$(BUILD_LINUX):
	mkdir -p $(BUILD_LINUX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ======================================
# Clean
# ======================================

clean:
	rm -rf $(BUILD_MAC) $(BUILD_LINUX) $(BIN_MAC) $(BIN_LINUX)
	@echo "$(RED)[✗] Cleaned all build artifacts$(RESET)"

.PHONY: all mac linux clean
