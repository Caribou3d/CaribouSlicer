cmake_minimum_required(VERSION 2.8.12)
project(nowide)

add_library(nowide STATIC
    nowide/args.hpp
    nowide/config.hpp
    nowide/convert.hpp
    nowide/cstdio.hpp
    nowide/cstdlib.hpp
    nowide/cstdlib.cpp    
    nowide/detail
    nowide/filebuf.hpp
    nowide/filesystem.hpp
    nowide/fstream.hpp
    nowide/iostream.cpp
    nowide/iostream.hpp
    nowide/quoted.hpp
    nowide/replacement.hpp
    nowide/stackstring.hpp
    nowide/stat.hpp
    nowide/utf
    nowide/utf8_codecvt.hpp
    nowide/windows.hpp
    nowide/detail/convert.hpp
    nowide/detail/is_path.hpp
    nowide/detail/is_string_container.hpp
    nowide/detail/utf.hpp
    nowide/utf/convert.hpp
    nowide/utf/utf.hpp
)

target_link_libraries(nowide PUBLIC boost_headeronly)


