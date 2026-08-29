cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBRAW_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_raw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_raw.c
)

SET(LIBRAW_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libraw.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libc++.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libc++abi.a
)

SET(LIBRAW_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libraw
        "${LIBRAW_SRC}"
        "${LIBRAW_LIB}"
        []
        ""
        "${LIBRAW_INC}"
        "-s USE_ZLIB=1"
        "1")
