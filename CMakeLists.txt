cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBRAW_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_raw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_raw.c
)

# No libc++/libc++abi here: the C++ runtime comes from the solver, for the
# same reason as libjxl and libheif - a second copy in the side module breaks
# libc++'s static state across the module boundary.
SET(LIBRAW_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libraw.a
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
