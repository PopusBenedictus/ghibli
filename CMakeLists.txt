cmake_minimum_required(VERSION 3.21)
project(ghibli C)

set(CMAKE_C_STANDARD 11)

# Project Targets
if (MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /std:c11")
else()
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -mshstk -fcommon -fstack-protector -Wall -Wextra -fstrict-overflow -fstrict-aliasing -Wshadow -Wstrict-aliasing -Wstrict-overflow=4")
endif()


if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64_CPU)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64")
    endif()
endif()

if (MSVC)
    set (CMAKE_C_FLAGS_DEBUG "/Od /Zi /DDEBUG /w44996")
    set (CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG /w44996")
    set (CMAKE_C_FLAGS_RELWITHDEBINFO "/O2 /DNDEBUG /Zi /w44996")
else()
    set (CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG -fsanitize-undefined-trap-on-error")
    set (CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set (CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -DNDEBUG -g")
endif()

include_directories("src")

add_subdirectory("src/app")