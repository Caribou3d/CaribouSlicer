# In PrusaSlicer 2.6.0 we switched from https://github.com/memononen/nanosvg to its fork https://github.com/fltk/nanosvg
# because this last implements the new function nsvgRasterizeXY() which we now use in GLTexture::load_from_svg()
# for rasterizing svg files from their original size to a squared power of two texture on Windows systems using
# AMD Radeon graphics cards

add_cmake_project(NanoSVG
    URL https://github.com/fltk/nanosvg/archive/refs/tags/fltk_2023-12-02.tar.gz
    URL_HASH SHA256=7f3d654c91169499466bdd43b914175c58811bb7e41a55f98c90902250e7ef8e 
)