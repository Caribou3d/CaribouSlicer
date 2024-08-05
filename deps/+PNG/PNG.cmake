if (APPLE)
    # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
    set(_disable_neon_extension "-DPNG_ARM_NEON:STRING=off")
else ()
    set(_disable_neon_extension "")
endif ()

set(_patch_cmd PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.patched CMakeLists.txt)
if(APPLE AND IS_CROSS_COMPILE)
# TODO: check if it doesn't create problem when compiling from arm to x86_64
    caribouslicer_add_cmake_project(PNG
        GIT_REPOSITORY https://github.com/glennrp/libpng.git
        GIT_TAG v1.6.35
        DEPENDS ${ZLIB_PKG}
        PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df &&
                            ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/macos-arm64.patch
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=prusaslicer_
            -DPNG_TESTS=OFF
            -DDISABLE_DEPENDENCY_TRACKING=OFF
            ${_disable_neon_extension}
    )
else ()

    if (APPLE)
        set(_patch_cmd ${_patch_cmd} && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/PNG.patch)
    endif ()

    add_cmake_project(PNG 
        URL https://github.com/glennrp/libpng/archive/refs/tags/v1.6.35.zip
        URL_HASH SHA256=3d22d46c566b1761a0e15ea397589b3a5f36ac09b7c785382e6470156c04247f
     #   URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz
     #   URL_HASH SHA256=fecc95b46cf05e8e3fc8a414750e0ba5aad00d89e9fdf175e94ff041caf1a03a
        PATCH_COMMAND "${_patch_cmd}"
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=prusaslicer_
            -DPNG_TESTS=OFF
            -DPNG_EXECUTABLES=OFF
            ${_disable_neon_extension}
)
endif()

set(DEP_PNG_DEPENDS ZLIB)
