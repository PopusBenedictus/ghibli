cmake_minimum_required(VERSION 3.21)
project(ghibli-app C)

set(CMAKE_C_STANDARD 11)

add_executable(ghibli-app main.c)

if(WIN32)
    add_library(bass STATIC IMPORTED)
    set_target_properties(bass PROPERTIES IMPORTED_LOCATION "${BASSLIB_LOCATION}/x64/bass.lib")
    target_include_directories(bass INTERFACE "${BASSLIB_LOCATION}")

    add_library(bassfx STATIC IMPORTED)
    set_target_properties(bassfx PROPERTIES IMPORTED_LOCATION "${BASSFX_LOCATION}/x64/bass_fx.lib")
    target_include_directories(bassfx INTERFACE "${BASSFX_LOCATION}")

    add_library(bassmix STATIC IMPORTED)
    set_target_properties(bassmix PROPERTIES IMPORTED_LOCATION "${BASSMIX_LOCATION}/x64/bassmix.lib")
    target_include_directories(bassmix INTERFACE "${BASSMIX_LOCATION}")

    target_link_libraries(ghibli-app PRIVATE bass bassfx bassmix)
else()
    find_library(BASS_LIB bass
            HINTS "${BASSLIB_LOCATION}/lib")

    if (NOT BASS_LIB)
        message(FATAL_ERROR "Bass library was not found. Use -DBASS_LOCATION to specify custom prefix for that library if needed.")
    endif()

    if (DEFINED BASS_LOCATION)
        include_directories(${INCLUDE_DIRECTORIES} ${BASS_LOCATION}/include)
    endif()


    find_library(BASSFX_LIB bass_fx
            HINTS "${BASSFX_LOCATION}/lib")

    if (NOT BASSFX_LIB)
        message(FATAL_ERROR "BassFX library was not found. Use -DBASSFX_LOCATION to specify custom prefix for that library if needed.")
    endif()

    if (DEFINED BASSFX_LOCATION)
        include_directories(${INCLUDE_DIRECTORIES} ${BASSFX_LOCATION}/include)
    endif()


    find_library(BASSMIX_LIB bassmix
            HINTS "${BASSMIX_LOCATION}/lib")

    if (NOT BASSMIX_LIB)
        message(FATAL_ERROR "Bassmix library was not found. Use -DBASSMIX_LOCATION to specify custom prefix for that library if needed.")
    endif()

    if (DEFINED BASSMIX_LOCATION)
        include_directories(${INCLUDE_DIRECTORIES} ${BASSMIX_LOCATION}/include)
    endif()

    target_link_libraries(ghibli-app PRIVATE ${BASS_LIB} ${BASSFX_LIB} ${BASSMIX_LIB})
endif()

find_package(SDL2 CONFIG REQUIRED)


if(WIN32)
    find_package(sdl2-image CONFIG REQUIRED)
    target_link_libraries(ghibli-app PRIVATE SDL2::SDL2_image)

    find_package(sdl2-ttf CONFIG REQUIRED)
    target_link_libraries(ghibli-app PRIVATE SDL2::SDL2_ttf)

    find_package(sdl2-gfx CONFIG REQUIRED)
    target_link_libraries(ghibli-app PRIVATE SDL2::SDL2_gfx)
    add_definitions(-DSDL_MAIN_HANDLED)
else()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(SDL2_image REQUIRED IMPORTED_TARGET SDL2_image)
    target_link_libraries(ghibli-app PRIVATE PkgConfig::SDL2_image)
    include_directories(${SDL2_image_INCLUDE_DIRS})

    pkg_check_modules(SDL2_ttf REQUIRED IMPORTED_TARGET SDL2_ttf)
    target_link_libraries(ghibli-app PRIVATE PkgConfig::SDL2_ttf)
    include_directories(${SDL2_ttf_INCLUDE_DIRS})

    pkg_check_modules(SDL2_gfx REQUIRED IMPORTED_TARGET SDL2_gfx)
    target_link_libraries(ghibli-app PRIVATE PkgConfig::SDL2_gfx)
    include_directories(${SDL2_gfx_INCLUDE_DIRS})

    target_link_libraries(ghibli-app PRIVATE -lm)
endif()

find_package(Threads REQUIRED)
target_link_libraries(ghibli-app PRIVATE Threads::Threads)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})