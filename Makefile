NAME        := bebisc
LIB_STATIC  := lib$(NAME).a
LIB_SHARED  := lib$(NAME).so

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

MODE        ?= release

ifeq ($(MODE),debug)
    CFLAGS     += -O0 -g3 -DDEBUG
endif

ifeq ($(MODE),release)
    CFLAGS     += -O2 -DNDEBUG
endif

ifeq ($(MODE),asan)
    CFLAGS     += -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
    SO_LDFLAGS += -fsanitize=address,undefined
endif

OBJ_S       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(MODE)/static/%.o,$(SRCS))
OBJ_D       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(MODE)/shared/%.o,$(SRCS))
DEPS        := $(OBJ_S:.o=.d) $(OBJ_D:.o=.d)

.PHONY: all static shared deps clean debug release asan format lint help

all: static shared

static: $(LIB_STATIC)
shared: $(LIB_SHARED)

$(LIB_STATIC): $(OBJ_S)
	$(AR) rcs $@ $^

$(LIB_SHARED): $(OBJ_D) $(SDL_LIB) $(CGLM_LIB)
	$(CC) $(SO_LDFLAGS) $(OBJ_D) -o $@ \
	      -L$(SDL_BUILD) -L$(CGLM_BUILD) -lm -lpthread -ldl

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
	rm -rf $(BUILD_DIR) $(LIB_STATIC) $(LIB_SHARED)

format:
	clang-format -i $(shell find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h')

lint:
	clang-tidy $(SRCS) -- $(CPPFLAGS) $(CFLAGS)

help:
	@echo "Available targets:"
	@echo "  make          -> libbebisc.a + libbebisc.so (release)"
	@echo "  make static   -> build libbebisc.a only"
	@echo "  make shared   -> build libbebisc.so only"
	@echo "  make debug    -> debug build with -O0 -g3"
	@echo "  make asan     -> build with AddressSanitizer + UBSan"
	@echo "  make clean    -> remove build directory and libraries"
	@echo "  make format   -> format sources with clang-format"
	@echo "  make lint     -> run static analysis with clang-tidy"

-include $(DEPS)
