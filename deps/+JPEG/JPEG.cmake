add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.0.3/libjpeg-turbo-3.0.3.tar.gz
    URL_HASH SHA256=343e789069fc7afbcdfe44dbba7dbbf45afa98a15150e079a38e60e44578865d

    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
)

set(DEP_JPEG_DEPENDS ZLIB)
