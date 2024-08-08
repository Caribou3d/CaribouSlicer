if (APPLE)
    # Only disable NEON extension for Apple ARM builds, leave it enabled for Raspberry PI.
    set(_disable_neon_extension "-DPNG_ARM_NEON:STRING=off")
else ()
    set(_disable_neon_extension "")
endif ()

set(_patch_cmd PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.patched CMakeLists.txt)

if(APPLE AND IS_CROSS_COMPILE)
# TODO: check if it doesn't create problem when compiling from arm to x86_64
    add_cmake_project(PNG
        URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz
        URL_HASH SHA256=fecc95b46cf05e8e3fc8a414750e0ba5aad00d89e9fdf175e94ff041caf1a03a
        DEPENDS ${ZLIB_PKG}
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=
            -DPNG_TESTS=OFF
            -DDISABLE_DEPENDENCY_TRACKING=OFF
            -DPNG_TOOLS=OFF            
            ${_disable_neon_extension}
    )
else ()

    if (APPLE)
        set(_patch_cmd ${_patch_cmd} && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/PNG.patch)
    endif ()

    add_cmake_project(PNG 
        URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.43.tar.gz
        URL_HASH SHA256=fecc95b46cf05e8e3fc8a414750e0ba5aad00d89e9fdf175e94ff041caf1a03a
        CMAKE_ARGS
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_PREFIX=
            -DPNG_TESTS=OFF
            -DPNG_TOOLS=OFF
            ${_disable_neon_extension}
)
endif()

set(DEP_PNG_DEPENDS ZLIB)
