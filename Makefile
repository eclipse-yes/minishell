# Makefile for NKU-OS mini shell.
# Author: Shuhao Zhang

VERSION = 1
DEBUG   = 0

ifeq ($(DEBUG), 1)
ZSH     = ./zsh_d
CFLAGS  = -Wall -O2 -g -DDEBUG
else
ZSH     = ./zsh
CFLAGS  = -Wall -O2
endif
CC      = gcc
ZSHARGS = "-v"
BINS    = $(ZSH)

all: $(BINS)

$(ZSH): main.c
	$(CC) $(CFLAGS) -o $(ZSH) $^

clean:
	rm -f $(BINS)*
