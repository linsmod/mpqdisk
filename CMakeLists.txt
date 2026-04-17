cmake_minimum_required(VERSION 3.15)
project(mpqdisk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(STORMLIB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../StormLib")
set(DOKAN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(DOKAN_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../x64/Release")

set(STORM_UNICODE ON CACHE BOOL "Compile UNICODE version" FORCE)

add_subdirectory(${STORMLIB_SRC_DIR} ${CMAKE_BINARY_DIR}/StormLib)

add_executable(mpqdisk
    main.cpp
    mpqdisk_config.cpp
    mpqfs.cpp
    mpqfs_ops.cpp
)

target_include_directories(mpqdisk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DOKAN_SRC_DIR}
    ${DOKAN_SRC_DIR}/sys
    ${STORMLIB_SRC_DIR}/src
)

target_link_libraries(mpqdisk PRIVATE
    storm
    ${DOKAN_LIB_DIR}/dokan2.lib
)

target_compile_definitions(mpqdisk PRIVATE
    UNICODE
    _UNICODE
    __STORMLIB_NO_STATIC_LINK__
)

if(MSVC)
    target_compile_options(mpqdisk PRIVATE /W4)
endif()