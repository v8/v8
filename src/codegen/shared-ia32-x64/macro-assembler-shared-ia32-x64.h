// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
#define V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_

#include "src/base/macros.h"
#include "src/codegen/turbo-assembler.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE SharedTurboAssembler : public TurboAssemblerBase {
 public:
  using TurboAssemblerBase::TurboAssemblerBase;

  void I16x8ExtMulHighS(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch);
  void I16x8ExtMulHighU(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch);
  void I16x8SConvertI8x16High(XMMRegister dst, XMMRegister src);
  void I16x8UConvertI8x16High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
  // Requires that dst == src1 if AVX is not supported.
  void I32x4ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed);
  void I32x4SConvertI16x8High(XMMRegister dst, XMMRegister src);
  void I32x4UConvertI16x8High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
  void I64x2Abs(XMMRegister dst, XMMRegister src, XMMRegister scratch);
  void I64x2GtS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch);
  void I64x2GeS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch);
  void I64x2ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed);
  void I64x2SConvertI32x4High(XMMRegister dst, XMMRegister src);
  void I64x2UConvertI32x4High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
};
}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
