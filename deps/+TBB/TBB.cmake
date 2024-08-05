add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.13.0.zip"
#    URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
    CMAKE_ARGS          
        -DTBB_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DTBB_TEST=OFF
        -DTBB_EXAMPLES=OFF
        -DTBB_STRICT=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)


# option(TBB_TEST "Enable testing" ON)
# option(TBB_EXAMPLES "Enable examples" OFF)
# option(TBB_STRICT "Treat compiler warnings as errors" ON)
# option(TBB_WINDOWS_DRIVER "Build as Universal Windows Driver (UWD)" OFF)
# option(TBB_NO_APPCONTAINER "Apply /APPCONTAINER:NO (for testing binaries for Windows Store)" OFF)
# option(TBB4PY_BUILD "Enable tbb4py build" OFF)
# option(TBB_BUILD "Enable tbb build" ON)
# option(TBBMALLOC_BUILD "Enable tbbmalloc build" ON)
# cmake_dependent_option(TBBMALLOC_PROXY_BUILD "Enable tbbmalloc_proxy build" ON "TBBMALLOC_BUILD" OFF)
# option(TBB_CPF "Enable preview features of the library" OFF)
# option(TBB_FIND_PACKAGE "Enable search for external oneTBB using find_package instead of build from sources" OFF)
# option(TBB_DISABLE_HWLOC_AUTOMATIC_SEARCH "Disable HWLOC automatic search by pkg-config tool" ${CMAKE_CROSSCOMPILING})
# option(TBB_ENABLE_IPO "Enable Interprocedural Optimization (IPO) during the compilation" ON)
# option(TBB_FUZZ_TESTING "Enable fuzz testing" OFF)
# option(TBB_INSTALL "Enable installation" ON)
# if(APPLE)
# option(TBB_BUILD_APPLE_FRAMEWORKS "Build as Apple Frameworks" OFF)
# endif()
