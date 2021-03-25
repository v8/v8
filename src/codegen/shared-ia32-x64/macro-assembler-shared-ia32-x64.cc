// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/shared-ia32-x64/macro-assembler-shared-ia32-x64.h"

#include "src/codegen/assembler.h"
#include "src/codegen/cpu-features.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

void SharedTurboAssembler::I16x8SConvertI8x16High(XMMRegister dst,
                                                  XMMRegister src) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // src = |a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p| (high)
    // dst = |i|i|j|j|k|k|l|l|m|m|n|n|o|o|p|p|
    vpunpckhbw(dst, src, src);
    vpsraw(dst, dst, 8);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // 2 bytes shorter than pshufd, but has depdency on dst.
      movhlps(dst, src);
      pmovsxbw(dst, dst);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovsxbw(dst, dst);
    }
  }
}

void SharedTurboAssembler::I16x8UConvertI8x16High(XMMRegister dst,
                                                  XMMRegister src,
                                                  XMMRegister scratch) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // tmp = |0|0|0|0|0|0|0|0 | 0|0|0|0|0|0|0|0|
    // src = |a|b|c|d|e|f|g|h | i|j|k|l|m|n|o|p|
    // dst = |0|a|0|b|0|c|0|d | 0|e|0|f|0|g|0|h|
    XMMRegister tmp = dst == src ? scratch : dst;
    vpxor(tmp, tmp, tmp);
    vpunpckhbw(dst, src, tmp);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // xorps can be executed on more ports than pshufd.
      xorps(scratch, scratch);
      punpckhbw(dst, scratch);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovzxbw(dst, dst);
    }
  }
}

void SharedTurboAssembler::I32x4SConvertI16x8High(XMMRegister dst,
                                                  XMMRegister src) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // src = |a|b|c|d|e|f|g|h| (high)
    // dst = |e|e|f|f|g|g|h|h|
    vpunpckhwd(dst, src, src);
    vpsrad(dst, dst, 16);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // 2 bytes shorter than pshufd, but has depdency on dst.
      movhlps(dst, src);
      pmovsxwd(dst, dst);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovsxwd(dst, dst);
    }
  }
}

void SharedTurboAssembler::I32x4UConvertI16x8High(XMMRegister dst,
                                                  XMMRegister src,
                                                  XMMRegister scratch) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // scratch = |0|0|0|0|0|0|0|0|
    // src     = |a|b|c|d|e|f|g|h|
    // dst     = |0|a|0|b|0|c|0|d|
    XMMRegister tmp = dst == src ? scratch : dst;
    vpxor(tmp, tmp, tmp);
    vpunpckhwd(dst, src, tmp);
  } else {
    if (dst == src) {
      // xorps can be executed on more ports than pshufd.
      xorps(scratch, scratch);
      punpckhwd(dst, scratch);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovzxwd(dst, dst);
    }
  }
}

void SharedTurboAssembler::I64x2SConvertI32x4High(XMMRegister dst,
                                                  XMMRegister src) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpunpckhqdq(dst, src, src);
    vpmovsxdq(dst, dst);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      movhlps(dst, src);
    } else {
      pshufd(dst, src, 0xEE);
    }
    pmovsxdq(dst, dst);
  }
}

void SharedTurboAssembler::I64x2UConvertI32x4High(XMMRegister dst,
                                                  XMMRegister src,
                                                  XMMRegister scratch) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpxor(scratch, scratch, scratch);
    vpunpckhdq(dst, src, scratch);
  } else {
    if (dst != src) {
      movaps(dst, src);
    }
    xorps(scratch, scratch);
    punpckhdq(dst, scratch);
  }
}

}  // namespace internal
}  // namespace v8
