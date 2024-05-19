find_package(OpenGL QUIET REQUIRED)

prusaslicer_add_cmake_project(TIFF
URL https://gitlab.com/libtiff/libtiff/-/archive/v4.6.0/libtiff-v4.6.0.zip
URL_HASH SHA256=5d652432123223338a6ee642a6499d98ebc5a702f8a065571e1001d4c08c37e6

DEPENDS ${ZLIB_PKG} ${PNG_PKG} dep_JPEG
CMAKE_ARGS
    -Dlzma:BOOL=OFF
    -Dwebp:BOOL=OFF
    -Djbig:BOOL=OFF
    -Dzstd:BOOL=OFF
    -Dlibdeflate:BOOL=OFF
    -Dpixarlog:BOOL=OFF
    -Dlerc:BOOL=OFF
)
