# ======================================
# LuaX Cross-Platform Makefile
# With Lua C API Compatibility Layer
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
CFLAGS_COMMON := -std=c11 -Wall -Wextra -O2 -I$(INC_DIR) \
                 -DHAVE_VM_LOAD_AND_RUN_FILE \
                 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
                 -Wno-unused-function -Wno-unused-variable \
                 -Wno-implicit-function-declaration

# macOS Flags
CFLAGS_MAC  := $(CFLAGS_COMMON) -fPIC
LDFLAGS_MAC := -lm -L/opt/homebrew/lib -llua5.4 \
               -Wl,-rpath,/opt/homebrew/lib \
               -Wl,-export_dynamic -Wl,-undefined,dynamic_lookup
STRIP_MAC   := strip -x

# Linux Flags
CFLAGS_LINUX := $(CFLAGS_COMMON)
LDFLAGS_LINUX := -lm -ldl

# Sources (include lua_compat.c)
SRCS_SRC    := $(wildcard $(SRC_DIR)/*.c)
SRCS_LIB    := $(wildcard $(LIB_DIR)/*.c)

# Objects
OBJS_SRC_MAC    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_MAC)/src_%.o,$(SRCS_SRC))
OBJS_LIB_MAC    := $(patsubst $(LIB_DIR)/%.c,$(BUILD_MAC)/lib_%.o,$(SRCS_LIB))
OBJS_MAC        := $(OBJS_SRC_MAC) $(OBJS_LIB_MAC)

OBJS_SRC_LINUX  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_LINUX)/src_%.o,$(SRCS_SRC))
OBJS_LIB_LINUX  := $(patsubst $(LIB_DIR)/%.c,$(BUILD_LINUX)/lib_%.o,$(SRCS_LIB))
OBJS_LINUX      := $(OBJS_SRC_LINUX) $(OBJS_LIB_LINUX)

# Binaries
BIN_MAC    := $(BIN_DIR)/$(TARGET_NAME)-macos
BIN_LINUX  := $(BIN_DIR)/$(TARGET_NAME)-linux

# Test module
TEST_MODULE_MAC   := test.dylib
TEST_MODULE_LINUX := test.so

# Pretty colors
YELLOW = \033[1;33m
GREEN  = \033[1;32m
RED    = \033[1;31m
BLUE   = \033[1;34m
RESET  = \033[0m

# ======================================
# Default Targets
# ======================================

.PHONY: all mac linux test-module-mac test-module-linux clean help

all: mac linux
	@echo "$(GREEN)[✔] All builds complete$(RESET)"

mac: $(BIN_MAC)
	@echo "$(GREEN)[✔] macOS build complete: $(BIN_MAC)$(RESET)"

linux: $(BIN_LINUX)
	@echo "$(GREEN)[✔] Linux build complete: $(BIN_LINUX)$(RESET)"

# ======================================
# macOS build
# ======================================

$(BIN_MAC): $(OBJS_MAC) | $(BIN_DIR)
	@echo "$(BLUE)[→] Linking macOS binary...$(RESET)"
	$(CC_MAC) $(CFLAGS_MAC) -o $@ $(OBJS_MAC) $(LDFLAGS_MAC)
	$(STRIP_MAC) $@

$(BUILD_MAC)/src_%.o: $(SRC_DIR)/%.c | $(BUILD_MAC)
	@echo "$(BLUE)[→] Compiling $< (macOS)$(RESET)"
	$(CC_MAC) $(CFLAGS_MAC) -c $< -o $@

$(BUILD_MAC)/lib_%.o: $(LIB_DIR)/%.c | $(BUILD_MAC)
	@echo "$(BLUE)[→] Compiling $< (macOS)$(RESET)"
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
	@echo "$(BLUE)[→] Linking Linux binary...$(RESET)"
	$(CC_LINUX) $(CFLAGS_LINUX) -o $@ $(OBJS_LINUX) $(LDFLAGS_LINUX)
	$(STRIP) $@

$(BUILD_LINUX)/src_%.o: $(SRC_DIR)/%.c | $(BUILD_LINUX)
	@echo "$(BLUE)[→] Compiling $< (Linux)$(RESET)"
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

$(BUILD_LINUX)/lib_%.o: $(LIB_DIR)/%.c | $(BUILD_LINUX)
	@echo "$(BLUE)[→] Compiling $< (Linux)$(RESET)"
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

# ======================================
# Test C Module
# ======================================

test-module-mac: $(TEST_MODULE_MAC)
	@echo "$(GREEN)[✔] macOS test module built: $(TEST_MODULE_MAC)$(RESET)"

$(TEST_MODULE_MAC): test_cmodule.c $(BUILD_MAC)/lib_lua_compat.o | $(BUILD_MAC)
	@echo "$(BLUE)[→] Building test module (macOS)$(RESET)"
	$(CC_MAC) $(CFLAGS_MAC) -dynamiclib -undefined dynamic_lookup \
		-o $@ test_cmodule.c $(BUILD_MAC)/lib_lua_compat.o

test-module-linux: $(TEST_MODULE_LINUX)
	@echo "$(GREEN)[✔] Linux test module built: $(TEST_MODULE_LINUX)$(RESET)"

$(TEST_MODULE_LINUX): test_cmodule.c $(BUILD_LINUX)/lib_lua_compat.o | $(BUILD_LINUX)
	@echo "$(BLUE)[→] Building test module (Linux)$(RESET)"
	$(CC_LINUX) $(CFLAGS_LINUX) -shared -fPIC \
		-o $@ test_cmodule.c $(BUILD_LINUX)/lib_lua_compat.o

# ======================================
# Testing
# ======================================

test: mac test-module-mac
	@echo "$(BLUE)[→] Running test suite...$(RESET)"
	./$(BIN_MAC) test_require.lua

test-linux: linux test-module-linux
	@echo "$(BLUE)[→] Running Linux test suite...$(RESET)"
	./$(BIN_LINUX) test_require.lua

# ======================================
# Directories
# ======================================

$(BUILD_MAC):
	@mkdir -p $(BUILD_MAC)

$(BUILD_LINUX):
	@mkdir -p $(BUILD_LINUX)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# ======================================
# Dependencies
# ======================================

# Core interpreter dependencies
$(BUILD_MAC)/src_interpreter.o: $(SRC_DIR)/interpreter.c $(INC_DIR)/interpreter.h $(INC_DIR)/parser.h
$(BUILD_LINUX)/src_interpreter.o: $(SRC_DIR)/interpreter.c $(INC_DIR)/interpreter.h $(INC_DIR)/parser.h

$(BUILD_MAC)/src_parser.o: $(SRC_DIR)/parser.c $(INC_DIR)/parser.h $(INC_DIR)/lexer.h
$(BUILD_LINUX)/src_parser.o: $(SRC_DIR)/parser.c $(INC_DIR)/parser.h $(INC_DIR)/lexer.h

$(BUILD_MAC)/src_lexer.o: $(SRC_DIR)/lexer.c $(INC_DIR)/lexer.h
$(BUILD_LINUX)/src_lexer.o: $(SRC_DIR)/lexer.c $(INC_DIR)/lexer.h

# Lua compatibility layer
$(BUILD_MAC)/lib_lua_compat.o: $(LIB_DIR)/lua_compat.c $(INC_DIR)/lua_compat.h $(INC_DIR)/interpreter.h
$(BUILD_LINUX)/lib_lua_compat.o: $(LIB_DIR)/lua_compat.c $(INC_DIR)/lua_compat.h $(INC_DIR)/interpreter.h

# Package library (uses lua_compat)
$(BUILD_MAC)/lib_package.o: $(LIB_DIR)/package.c $(INC_DIR)/interpreter.h $(INC_DIR)/lua_compat.h
$(BUILD_LINUX)/lib_package.o: $(LIB_DIR)/package.c $(INC_DIR)/interpreter.h $(INC_DIR)/lua_compat.h

# ======================================
# Clean
# ======================================

clean:
	@echo "$(RED)[✗] Cleaning build artifacts...$(RESET)"
	rm -rf $(BUILD_MAC) $(BUILD_LINUX) 
	rm -f $(BIN_MAC) $(BIN_LINUX)
	rm -f $(TEST_MODULE_MAC) $(TEST_MODULE_LINUX)
	@echo "$(RED)[✗] Clean complete$(RESET)"

# ======================================
# Help
# ======================================

help:
	@echo "$(YELLOW)Available targets:$(RESET)"
	@echo "  $(GREEN)all$(RESET)              - Build for both macOS and Linux"
	@echo "  $(GREEN)mac$(RESET)              - Build macOS binary"
	@echo "  $(GREEN)linux$(RESET)            - Build Linux binary (cross-compile)"
	@echo "  $(GREEN)test-module-mac$(RESET)  - Build test C module for macOS"
	@echo "  $(GREEN)test-module-linux$(RESET) - Build test C module for Linux"
	@echo "  $(GREEN)test$(RESET)             - Build and run macOS tests"
	@echo "  $(GREEN)test-linux$(RESET)       - Build and run Linux tests"
	@echo "  $(GREEN)clean$(RESET)            - Remove all build artifacts"
	@echo "  $(GREEN)help$(RESET)             - Show this help message"
	@echo ""
	@echo "$(YELLOW)Features:$(RESET)"
	@echo "  • Lua C API compatibility layer for LuaRocks modules"
	@echo "  • Cross-platform dynamic library loading"
	@echo "  • Standard require() implementation"
	@echo ""
	@echo "$(YELLOW)Usage examples:$(RESET)"
	@echo "  make mac              # Build for macOS"
	@echo "  make test             # Build and test"
	@echo "  make test-module-mac  # Build test C module"
	@echo ""
	@echo "$(YELLOW)Testing LuaRocks modules:$(RESET)"
	@echo "  1. Install a module: luarocks install luasocket --local"
	@echo "  2. Set cpath: export LUA_CPATH=\"./?.so;~/.luarocks/lib/lua/5.1/?.so\""
	@echo "  3. Use in Lua: local socket = require('socket')"
