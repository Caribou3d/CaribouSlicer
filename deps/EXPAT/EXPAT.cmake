prusaslicer_add_cmake_project(EXPAT
  URL https://github.com/libexpat/libexpat/archive/refs/tags/R_2_6_2.zip
  URL_HASH SHA256=9cddaf9abdac4cb3308c24fea13219a7879c0ad9a8e6797240b9cf4770b337b4
  SOURCE_SUBDIR expat
  CMAKE_ARGS
    -DEXPAT_BUILD_TOOLS:BOOL=OFF
    -DEXPAT_BUILD_EXAMPLES:BOOL=OFF
    -DEXPAT_BUILD_TESTS:BOOL=OFF
    -DEXPAT_BUILD_DOCS=OFF
    -DEXPAT_BUILD_PKGCONFIG=OFF
)

if (MSVC)
    add_debug_dep(dep_EXPAT)
endif ()
