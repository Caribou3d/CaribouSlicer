add_cmake_project(OCCT
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_8_1.tar.gz
    URL_HASH SHA256=7321af48c34dc253bf8aae3f0430e8cb10976961d534d8509e72516978aa82f5
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/occt_toolkit.cmake ./adm/cmake/
    CMAKE_ARGS
        -DINSTALL_DIR_LAYOUT=Unix # LMBBS
        -DBUILD_LIBRARY_TYPE=Static
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FREETYPE=OFF
	    -DUSE_FFMPEG=OFF
	    -DUSE_VTK=OFF
	    -DUSE_FREETYPE=OFF
	    -DBUILD_MODULE_ApplicationFramework=OFF
        #-DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
		-DBUILD_MODULE_FoundationClasses=OFF
		-DBUILD_MODULE_ModelingAlgorithms=OFF
		-DBUILD_MODULE_ModelingData=OFF
		-DBUILD_MODULE_Visualization=OFF
        -DBUILD_MODULE_DETools=OFF
)
