# Customize below to fit your system

# compiler and linker
CC = cc
LD = cc

# flags
CPPFLAGS = -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os -fPIE
LDFLAGS = -s -pie

TLSLIBS = -ltls

# installation paths
PREFIX = /usr/local

