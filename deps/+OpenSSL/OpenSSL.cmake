
include(ProcessorCount)
ProcessorCount(NPROC)

set(_conf_cmd "./config")
set(_cross_arch "")
set(_cross_comp_prefix_line "")
if (CMAKE_CROSSCOMPILING)
    set(_conf_cmd "./Configure")
    set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

    if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
        set(_cross_arch "linux-aarch64")
    elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
        # TODO: verify
        set(_cross_arch "linux-armv4")
    endif ()
endif ()

ExternalProject_Add(dep_OpenSSL
    EXCLUDE_FROM_ALL ON
    URL "https://github.com/openssl/openssl/archive/refs/tags/openssl-3.3.1.zip"
    URL_HASH SHA256=307284f39bfb7061229c57e263e707655aa80aa9950bf6def28ed63fec91a726
    DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/OpenSSL
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--prefix=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}"
        ${_cross_comp_prefix_line}
        no-shared
        no-ssl3-method
        no-dynamic-engine
        -Wa,--noexecstack
    BUILD_COMMAND make depend && make "-j${NPROC}"
    INSTALL_COMMAND make install_sw
)