project(panwrap)

set(SRCS panwrap-syscall.c panwrap-util.c)

add_library(panwrap SHARED ${SRCS})
target_link_libraries(panwrap decoder)
