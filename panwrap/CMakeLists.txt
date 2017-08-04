project(panwrap)

set(SRCS panwrap-syscall.c panwrap-util.c ../decoder/trace.c)

add_library(panwrap SHARED ${SRCS})
