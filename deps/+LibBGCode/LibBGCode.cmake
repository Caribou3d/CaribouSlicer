set(LibBGCode_SOURCE_DIR "" CACHE PATH "Optionally specify local LibBGCode source directory")

set(_source_dir_line
    URL https://github.com/prusa3d/libbgcode/archive/5347c3399ec933f590d1f96903a406647f287d8f.zip
    URL_HASH SHA256=f575aef9bb7b81e2c0f0b1291e1f23670acb5f16bb0348844671de474fb62b66)

if (LibBGCode_SOURCE_DIR)
    set(_source_dir_line "SOURCE_DIR;${LibBGCode_SOURCE_DIR};BUILD_ALWAYS;ON")
endif ()

add_cmake_project(LibBGCode
    ${_source_dir_line}
    CMAKE_ARGS
        -DLibBGCode_BUILD_TESTS:BOOL=OFF
        -DLibBGCode_BUILD_CMD_TOOL:BOOL=OFF
)

set(DEP_LibBGCode_DEPENDS ZLIB Boost heatshrink)
