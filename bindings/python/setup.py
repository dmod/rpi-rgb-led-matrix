#!/usr/bin/env python
"""Setup script for rgbmatrix Python bindings with automatic C++ library build."""

import os
import subprocess
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

# Paths relative to this setup.py file
BINDING_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(BINDING_DIR, '..', '..'))
LIB_DIR = os.path.join(ROOT_DIR, 'lib')
INCLUDE_DIR = os.path.join(ROOT_DIR, 'include')


class BuildRGBMatrixLib(build_ext):
    """Custom build command that compiles the C++ library first."""
    
    def run(self):
        """Build the C++ library before building Python extensions."""
        # Build the C++ library
        print("Building rpi-rgb-led-matrix C++ library...")
        try:
            subprocess.check_call(['make', '-C', LIB_DIR])
            print("C++ library built successfully")
        except subprocess.CalledProcessError as e:
            print(f"Error building C++ library: {e}", file=sys.stderr)
            sys.exit(1)
        
        # Now build the Python extensions
        super().run()


core_ext = Extension(
    name                = 'core',
    sources             = ['rgbmatrix/core.cpp', 'rgbmatrix/shims/pillow.c'],
    include_dirs        = ['../../include', 'rgbmatrix/shims'],
    library_dirs        = ['../../lib'],
    libraries           = ['rgbmatrix'],
    extra_compile_args  = ["-O3", "-Wall"],
    language            = 'c++'
)

graphics_ext = Extension(
    name                = 'graphics',
    sources             = ['rgbmatrix/graphics.cpp'],
    include_dirs        = ['../../include'],
    library_dirs        = ['../../lib'],
    libraries           = ['rgbmatrix'],
    extra_compile_args  = ["-O3", "-Wall"],
    language            = 'c++'
)

setup(
    name                = 'rgbmatrix',
    version             = '0.0.1',
    author              = 'Christoph Friedrich',
    author_email        = 'christoph.friedrich@vonaffenfels.de',
    classifiers         = ['Development Status :: 3 - Alpha'],
    ext_package         = 'rgbmatrix',
    ext_modules         = [core_ext, graphics_ext],
    packages            = ['rgbmatrix']
)
