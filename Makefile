NAME        := bebisc
LIB_STATIC  := lib$(NAME).a
LIB_SHARED  := lib$(NAME).so
CLI_BIN     := bebisc_cli

SRC_DIR     := src
INC_DIR     := inc
BUILD_DIR   := build

SRCS        := $(shell find $(SRC_DIR) -name '*.c')

CC          := cc
AR          := ar
STD         := -std=c99

WARNINGS    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
               -Wsign-conversion -Wcast-align -Wformat=2 \
               -Wundef -Wstrict-prototypes -Wmissing-prototypes

CPPFLAGS    := -D_POSIX_C_SOURCE=200809L -I$(INC_DIR) -MMD -MP
CFLAGS      := $(STD) $(WARNINGS)
SO_LDFLAGS  := -shared -Wl,-soname,$(LIB_SHARED)

PREFIX      ?= /usr/local
INCDIR      ?= $(PREFIX)/include
LIBDIR      ?= $(PREFIX)/lib
BINDIR      ?= $(PREFIX)/bin

MODE        ?= release

ifeq ($(MODE),debug)
    CFLAGS      += -O0 -g3 -DDEBUG
endif

ifeq ($(MODE),release)
    CFLAGS      += -O2 -DNDEBUG
endif

ifeq ($(MODE),asan)
    CFLAGS      += -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
    SO_LDFLAGS += -fsanitize=address,undefined
endif

OBJ_S       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(MODE)/static/%.o,$(SRCS))
OBJ_D       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(MODE)/shared/%.o,$(SRCS))
DEPS        := $(OBJ_S:.o=.d) $(OBJ_D:.o=.d)

.PHONY: all static shared deps clean debug release asan format lint help cli install uninstall

all: static shared cli

static: $(LIB_STATIC)
shared: $(LIB_SHARED)

cli: $(CLI_BIN)

$(LIB_STATIC): $(OBJ_S)
	$(AR) rcs $@ $^

$(LIB_SHARED): $(OBJ_D)
	$(CC) $(SO_LDFLAGS) $(OBJ_D) -o $@ -lm -lpthread -ldl

$(CLI_BIN): bebisc.c $(LIB_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) bebisc.c -o $@ -L. -lbebisc -Wl,-rpath,$(LIBDIR) -lm -lpthread -ldl

$(BUILD_DIR)/$(MODE)/static/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(MODE)/shared/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

debug:
	$(MAKE) MODE=debug

release:
	$(MAKE) MODE=release

asan:
	$(MAKE) MODE=asan

clean:
	rm -rf $(BUILD_DIR) $(LIB_STATIC) $(LIB_SHARED) $(CLI_BIN)

format:
	clang-format -i $(shell find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h')

lint:
	clang-tidy $(SRCS) -- $(CPPFLAGS) $(CFLAGS)

install: all
	@echo "Installing to $(PREFIX)..."
	install -d $(DESTDIR)$(INCDIR)/$(NAME)
	install -m 644 $(INC_DIR)/*.h $(DESTDIR)$(INCDIR)/$(NAME)/

	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/

	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(CLI_BIN) $(DESTDIR)$(BINDIR)/

	@if [ -d /etc/ld.so.conf.d ]; then ldconfig 2>/dev/null || true; fi
	@echo "Installation complete!"

uninstall:
	@echo "Uninstalling from $(PREFIX)..."
	rm -rf $(DESTDIR)$(INCDIR)/$(NAME)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -f $(DESTDIR)$(BINDIR)/$(CLI_BIN)
	@if [ -d /etc/ld.so.conf.d ]; then ldconfig 2>/dev/null || true; fi
	@echo "Uninstallation complete!"

help:
	@echo "Available targets:"
	@echo "  make        -> libbebisc.a + libbebisc.so + cli (release)"
	@echo "  make static   -> build libbebisc.a only"
	@echo "  make shared   -> build libbebisc.so only"
	@echo "  make cli      -> build bebisc CLI executable"
	@echo "  make debug    -> debug build with -O0 -g3"
	@echo "  make asan     -> build with AddressSanitizer + UBSan"
	@echo "  make clean    -> remove build directory and libraries"
	@echo "  make format   -> format sources with clang-format"
	@echo "  make lint     -> run static analysis with clang-tidy"
	@echo "  make install  -> install headers, libraries, and CLI (requires sudo)"
	@echo "  make uninstall-> remove installed files"

-include $(DEPS)
