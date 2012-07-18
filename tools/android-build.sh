#!/bin/bash
# Copyright 2012 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

if [ ${#@} -lt 4 ] ; then
  echo "Error: $0 needs 4 arguments."
  exit 1
fi

ARCH=$1
MODE=$2
OUTDIR=$3
GYPFLAGS=$4

host_os=$(uname -s | sed -e 's/Linux/linux/;s/Darwin/mac/')

case "${host_os}" in
  "linux")
    toolchain_dir="linux-x86"
    ;;
  *)
    echo "Host platform ${host_os} is not supported" >& 2
    exit 1
esac

case "${ARCH}" in
  "android_arm")
    DEFINES=" target_arch=arm v8_target_arch=arm android_target_arch=arm"
    DEFINES+=" arm_neon=0 armv7=1"
    toolchain_arch="arm-linux-androideabi-4.4.3"
    ;;
  "android_ia32")
    DEFINES=" target_arch=ia32 v8_target_arch=ia32 android_target_arch=x86"
    toolchain_arch="x86-4.4.3"
    ;;
  *)
    echo "Architecture: ${ARCH} is not supported." >& 2
    echo "Current supported architectures: arm|ia32." >& 2
    exit 1
esac

toolchain_path="${ANDROID_NDK_ROOT}/toolchains/${toolchain_arch}/prebuilt/"
ANDROID_TOOLCHAIN="${toolchain_path}/${toolchain_dir}/bin"
if [ ! -d "${ANDROID_TOOLCHAIN}" ]; then
  echo "Cannot find Android toolchain in ${ANDROID_TOOLCHAIN}." >& 2
  echo "The NDK version might be wrong." >& 2
  exit 1
fi

# The set of GYP_DEFINES to pass to gyp. 
export GYP_DEFINES="${DEFINES}"

export GYP_GENERATORS=make
export CC=${ANDROID_TOOLCHAIN}/*-gcc
export CXX=${ANDROID_TOOLCHAIN}/*-g++
build/gyp/gyp --generator-output="${OUTDIR}" build/all.gyp \
              -Ibuild/standalone.gypi --depth=. -Ibuild/android.gypi \
              -S.${ARCH} ${GYPFLAGS}

export AR=${ANDROID_TOOLCHAIN}/*-ar
export RANLIB=${ANDROID_TOOLCHAIN}/*-ranlib
export LD=${ANDROID_TOOLCHAIN}/*-ld
export LINK=${ANDROID_TOOLCHAIN}/*-g++
export BUILDTYPE=${MODE[@]^}
export builddir=$(readlink -f ${PWD})/${OUTDIR}/${ARCH}.${MODE}
make -C "${OUTDIR}" -f Makefile.${ARCH}
