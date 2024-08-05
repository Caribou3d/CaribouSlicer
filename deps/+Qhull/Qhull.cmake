include(GNUInstallDirs)

set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=ON")
set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=OFF")
if (BUILD_SHARED_LIBS)
    set(_qhull_static_libs "-DBUILD_STATIC_LIBS:BOOL=OFF")
    set(_qhull_shared_libs "-DBUILD_SHARED_LIBS:BOOL=ON")
endif ()

add_cmake_project(Qhull
    URL https://github.com/qhull/qhull/archive/refs/tags/v8.1.alpha4.tar.gz
    URL_HASH SHA256=aa831e24d588fb49ca098ed564f011ccb028fcfc3f3cef61e9cc364e73c01f6f
    CMAKE_ARGS 
        -DINCLUDE_INSTALL_DIR=${CMAKE_INSTALL_INCLUDEDIR}
        -DBUILD_APPLICATIONS:BOOL=OFF
        ${_qhull_shared_libs}
        ${_qhull_static_libs}
        -DQHULL_ENABLE_TESTING:BOOL=OFF
)
