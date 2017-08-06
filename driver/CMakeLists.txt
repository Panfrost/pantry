project(driver)

set(SRCS memory.c shim.c)

if (BUILD_SYNTHESISER)
	list(APPEND SRCS synthesiser.c)
endif()

add_library(driver STATIC ${SRCS})
