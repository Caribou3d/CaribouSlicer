add_cmake_project(
    TBB
    URL https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.13.0.zip
    URL_HASH SHA256=f8dba2602f61804938d40c24d8f9b1f1cc093cd003b24901d5c3cc75f3dbb952
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DTBB_EXAMPLES=OFF
        -DTBB_STRICT=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)
