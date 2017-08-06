cmake_minimum_required(VERSION 3.5)
include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckCCompilerFlag)
include(CheckIncludeFiles)
include(CheckSymbolExists)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions("-Wall -Werror -Wextra -Wno-unused-parameter -D_GNU_SOURCE")

macro(c_attribute_required attrname code)
    if (NOT DEFINED ATTRIBUTE_${attrname}_SUPPORT OR
        NOT ATTRIBUTE_${attrname}_SUPPORT)

        message(STATUS "Checking if compiler supports __attribute__((${attrname}))")
        check_c_source_compiles("${code}" ATTRIBUTE_${attrname}_SUPPORT)
        if (NOT ATTRIBUTE_${attrname}_SUPPORT)
            message(FATAL_ERROR "Compiler must support __attribute__((${attrname}))")
        endif()
    endif()
endmacro()

macro(c_function_required f)
    if (NOT DEFINED FUNCTION_${f}_SUPPORT)
        check_function_exists("${f}" FUNCTION_${f}_SUPPORT)
        if (NOT FUNCTION_${f}_SUPPORT)
            message(FATAL_ERROR "libc must support ${f}")
        endif()
    endif()
endmacro()

macro(include_files_required files)
    string(REGEX REPLACE "[/;.]" "_" files_var ${files})

    if (NOT DEFINED "HAVE_${files_var}")
        check_include_files(${files} "HAVE_${files_var}")
        if (NOT HAVE_${files_var})
            message(FATAL_ERROR "Missing required header files: " ${files})
        endif()
    endif()
endmacro()

macro(symbols_from_header_required header)
    foreach(s ${ARGN})
        if (NOT DEFINED ${s}_IS_DEFINED)
            check_symbol_exists(${s} ${header} "${s}_IS_DEFINED")
            if (NOT ${s}_IS_DEFINED)
                message(FATAL_ERROR "${header} doesn't define ${s}")
            endif()
        endif()
    endforeach()
endmacro()

c_attribute_required(packed "struct s { int a; } __attribute__((packed)); main;")
c_attribute_required(constructor "void __attribute__((constructor)) f(){}main;")
c_attribute_required(format
    "void __attribute__((format (printf, 1, 2))) f(const char *, ...);main;")

c_function_required(asprintf)

include_files_required("sys/user.h")
symbols_from_header_required("sys/user.h" PAGE_SIZE)

check_c_compiler_flag("-mfp16-format=alternative" MFP16_FLAG_SUPPORT)
if (MFP16_FLAG_SUPPORT)
    add_definitions("-mfp16-format=alternative")
else()
    # Check to make sure we can still use __fp16 with the compiler
    check_c_source_compiles("__fp16 f; main;" MFP16_BUILTIN_SUPPORT)
    if (NOT MFP16_BUILTIN_SUPPORT)
        message(FATAL_ERROR "Compiler doesn't support __fp16 type")
    endif()
endif()

# Optional checks
check_c_source_compiles("int main(){return __builtin_ffs(1);}" HAVE_BUILTIN_FFS)
if (HAVE_BUILTIN_FFS)
    add_definitions("-DHAVE_BUILTIN_FFS")
endif()

# Configurable project options options
option(PRECOMPILED_SHADER "whether to use precompiled shaders" 0)

include_directories("include")
add_subdirectory(prototype)
add_subdirectory(panwrap)

# vim: tabstop=4 softtabstop=4 shiftwidth=4 expandtab :
