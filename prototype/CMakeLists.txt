project(prototype)

# TODO: Include driver correctly

set(SRCS prototype.c ../driver/shim.c ../driver/memory.c
	../driver/synthesise.c)

if(PRECOMPILED_SHADERS)
	add_definitions("-DPRECOMPILED_SHADERS")
endif()

add_executable(prototype ${SRCS})

target_link_libraries(prototype m dl)
