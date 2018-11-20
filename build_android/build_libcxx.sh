#!/bin/bash

ARCH=x86_64

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

export NDK=~/Android/Sdk/ndk-bundle

#rm -rf ${DIR}/build-toolchain
#$NDK/build/tools/make_standalone_toolchain.py \
#  --arch ${ARCH} \
#  --api 21 \
#  --stl=libc++ \
#  --install-dir=${DIR}/build-toolchain

# Add the standalone toolchain to the search path.
#export PATH=${DIR}/build-toolchain/bin:$PATH

# Tell configure what tools to use.
#export AS=clang
#export CC=clang
#export CXX=clang++
#export LD=clang

# Tell configure what flags Android requires.
#export CFLAGS="-pthread"
#export CPPFLAGS="-fno-rtti -fcoroutines-ts -DANDROID -std=c++17 -fexceptions -lpthread --disable-shared -v"
#export LDFLAGS="-lstdc++ -Wl -v"


#svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
#cd llvm/projects                                                   
#svn co http://llvm.org/svn/llvm-project/libcxx/trunk libcxx
#svn co http://llvm.org/svn/llvm-project/libcxxabi/trunk libcxxabi
#cd ..

#rm -rf llvm/build
#mkdir llvm/build
#cd llvm/build
#cmake .. \
#  -DCMAKE_SYSTEM_NAME=Android \
#  -DCMAKE_SYSTEM_VERSION=21 \
#  -DCMAKE_ANDROID_ARCH_ABI=${ARCH} \
#  -DCMAKE_ANDROID_STANDALONE_TOOLCHAIN=${DIR}/build-toolchain
#  -DANDROID_STL=c++_shared
#
#make cxx





