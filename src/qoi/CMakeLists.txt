# PrusaSlicer specific CMake

cmake_minimum_required(VERSION 3.20.0)
project(qoi)

add_library(qoi STATIC
    qoi.h
    qoilib.c
)
