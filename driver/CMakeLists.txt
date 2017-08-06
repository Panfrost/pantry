project(driver)

set(SRCS memory.c shim.c)

add_library(driver STATIC ${SRCS})
