caribouslicer_add_cmake_project(
    CGAL
    # GIT_REPOSITORY https://github.com/CGAL/cgal.git

    URL      https://github.com/CGAL/cgal/archive/refs/tags/v5.5.zip
    URL_HASH SHA256=096a19720f5d19061beb104bb2193f6b39d5cc13b1c93f5bbc71cf711ca485c1

    DEPENDS dep_Boost dep_GMP dep_MPFR
)

include(GNUInstallDirs)

