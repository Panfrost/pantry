project(driver)

set(SRCS shim.c memory.c synthesiser.c)

add_library(driver STATIC ${SRCS})
