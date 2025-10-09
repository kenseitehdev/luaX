# Project settings
TARGET    := bin/luaX
SRC_DIR   := src
LIB_DIR   := lib
INC_DIR   := include
BUILD_DIR := build

# Tools
CC        := gcc
CFLAGS    := -std=c11 -Wall -Wextra -O2 -I$(INC_DIR)
LDFLAGS   := -lm
STRIP     := strip
INSTALL   ?= install
PREFIX    ?= /usr/local
DESTDIR   ?=
BINDIR    := $(DESTDIR)$(PREFIX)/bin
INCLUDEDIR:= $(DESTDIR)$(PREFIX)/include/luaX

# Sources and objects
SRCS_SRC  := $(wildcard $(SRC_DIR)/*.c)
SRCS_LIB  := $(wildcard $(LIB_DIR)/*.c)
OBJS_SRC  := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/src_%.o,$(SRCS_SRC))
OBJS_LIB  := $(patsubst $(LIB_DIR)/%.c,$(BUILD_DIR)/lib_%.o,$(SRCS_LIB))
OBJS      := $(OBJS_SRC) $(OBJS_LIB)
HEADERS   := $(wildcard $(INC_DIR)/*.h)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	$(STRIP) $@

# Compile rules (namespaced to avoid filename collisions)
$(BUILD_DIR)/src_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib_%.o: $(LIB_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure dirs exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

bin:
	mkdir -p bin

# Install/uninstall
install: all
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -m 755 $(TARGET) $(BINDIR)/$(notdir $(TARGET))
	# headers (optional)
	$(INSTALL) -d $(INCLUDEDIR)
	$(INSTALL) -m 644 $(HEADERS) $(INCLUDEDIR)

uninstall:
	rm -f $(BINDIR)/$(notdir $(TARGET))
	rm -rf $(INCLUDEDIR)

# Housekeeping
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean install uninstall
