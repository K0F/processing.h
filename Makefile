# Makefile: converts all .pde -> .c using pde2c and builds each into an executable
# Usage:
#   make all        -> builds pde2c, converts & compiles all .pde files
#   make clean

CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall
LDFLAGS ?= -lraylib -lm -lpthread -ldl

# On macOS adjust LDFLAGS if needed
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS := -lraylib -lm
endif

CONVERTER := pde2c
CONVERTER_SRC := pde2c.c

PDE_SRCS := $(wildcard *.pde)
C_SRCS := $(PDE_SRCS:.pde=.c)
EXES := $(PDE_SRCS:.pde=)

.PHONY: all clean convert build

all: $(CONVERTER) convert build

$(CONVERTER): $(CONVERTER_SRC)
	$(CC) $(CFLAGS) -o $@ $<

convert: $(C_SRCS)

%.c: %.pde $(CONVERTER)
	./$(CONVERTER) $< $@

build: $(EXES)

%: %.c processing.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	-rm -f $(CONVERTER) $(C_SRCS) $(EXES) *.o
