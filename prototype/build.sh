#!/bin/sh
arm-linux-gnueabi-gcc prototype.c shim.c memory.c synthesise.c \
	-Wl,-Bdynamic -lc -ldl -lm \
	-I../../panloader/include \
	-Wall -Werror -Wextra -Wno-missing-braces -D_FILE_OFFSET_BITS=64
