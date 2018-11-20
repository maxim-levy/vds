#!/bin/bash

ARCH=x86_64

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Add the standalone toolchain to the search path.
export ANDROID_NDK=${DIR}/build-toolchain
export PATH=${DIR}/build-toolchain/bin:$PATH

# Tell configure what tools to use.
export AS=clang
export CC=clang
export CXX=clang++
export LD=clang

mkdir ${DIR}/zlib_debug
cd ${DIR}/zlib_debug
cmake ${DIR}/zlib

cmake --build .
#cmake --build . --target install

