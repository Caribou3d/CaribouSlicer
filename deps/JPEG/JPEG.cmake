caribouslicer_add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.3.zip
    URL_HASH SHA256=8ccb5c9241e33dd0dbb6d6db6e883ecd849070dfabf57ae1cc451df938c266cd
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
)
