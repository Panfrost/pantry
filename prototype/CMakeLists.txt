project(prototype)

set(SRCS prototype.c)

if(PRECOMPILED_SHADERS)
	add_definitions("-DPRECOMPILED_SHADERS")
endif()

add_executable(prototype ${SRCS})
target_link_libraries(prototype m dl driver)
