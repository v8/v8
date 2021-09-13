// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/deoptimizer.h"

namespace v8 {
namespace internal {

const bool Deoptimizer::kSupportsFixedDeoptExitSizes = true;

// These constants should *not* change unless the instruction sequence
// of deoptimization exits (CallForDeoptimization) is changed. Changes
// due to additional IsolateData fields (e.g. roots, builtins) should
// be made s.t. exit sizes remain unchanged.
// TODO(crbug.com/v8/12203)
const int Deoptimizer::kNonLazyDeoptExitSize = 3 * kInstrSize;
const int Deoptimizer::kLazyDeoptExitSize = 3 * kInstrSize;
const int Deoptimizer::kEagerWithResumeBeforeArgsSize = 4 * kInstrSize;
const int Deoptimizer::kEagerWithResumeDeoptExitSize =
    kEagerWithResumeBeforeArgsSize + 2 * kSystemPointerSize;
const int Deoptimizer::kEagerWithResumeImmedArgs1PcOffset = kInstrSize;
const int Deoptimizer::kEagerWithResumeImmedArgs2PcOffset =
    kInstrSize + kSystemPointerSize;

Float32 RegisterValues::GetFloatRegister(unsigned n) const {
  const int kShift = n % 2 == 0 ? 0 : 32;

  return Float32::FromBits(
      static_cast<uint32_t>(double_registers_[n / 2].get_bits() >> kShift));
}

void FrameDescription::SetCallerPc(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}

void FrameDescription::SetCallerFp(unsigned offset, intptr_t value) {
  SetFrameSlot(offset, value);
}

void FrameDescription::SetCallerConstantPool(unsigned offset, intptr_t value) {
  // No embedded constant pool support.
  UNREACHABLE();
}

void FrameDescription::SetPc(intptr_t pc) { pc_ = pc; }

}  // namespace internal
}  // namespace v8
