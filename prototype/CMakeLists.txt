project(prototype)

# TODO: Include driver correctly

set(SRCS prototype.c ../driver/shim.c ../driver/memory.c
	../driver/synthesise)

add_executable(prototype ${SRCS})

target_link_libraries(prototype m dl)
