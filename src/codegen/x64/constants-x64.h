// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_X64_CONSTANTS_X64_H_
#define V8_CODEGEN_X64_CONSTANTS_X64_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// The actual value of the kRootRegister is offset from the IsolateData's start
// to take advantage of negative displacement values.
#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
// On x64, the smallest operand encoding allows int8 offsets, thus we select the
// bias s.t. the first 32 8-byte slots of IsolateData are can be encoded this
// way.
constexpr int kRootRegisterBias = 128;
// Problems with #include order prevent this static_assert:
// static_assert(kRootRegister != kPtrComprCageBaseRegister);
#else
constexpr int kRootRegisterBias = 0;
// Problems with #include order prevent this static_assert:
// static_assert(kRootRegister == kPtrComprCageBaseRegister);
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

// The maximum size of the code range s.t. pc-relative calls are possible
// between all Code objects in the range.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 2048;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_X64_CONSTANTS_X64_H_
