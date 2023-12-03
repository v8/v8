// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_INL_H_
#define V8_BUILTINS_BUILTINS_INL_H_

#include "src/builtins/builtins.h"

namespace v8 {
namespace internal {

// static
constexpr Builtin Builtins::RecordWrite(SaveFPRegsMode fp_mode) {
  switch (fp_mode) {
    case SaveFPRegsMode::kIgnore:
      return Builtin::kRecordWriteIgnoreFP;
    case SaveFPRegsMode::kSave:
      return Builtin::kRecordWriteSaveFP;
  }
}

// static
constexpr Builtin Builtins::IndirectPointerBarrier(SaveFPRegsMode fp_mode) {
  switch (fp_mode) {
    case SaveFPRegsMode::kIgnore:
      return Builtin::kIndirectPointerBarrierIgnoreFP;
    case SaveFPRegsMode::kSave:
      return Builtin::kIndirectPointerBarrierSaveFP;
  }
}

// static
constexpr Builtin Builtins::EphemeronKeyBarrier(SaveFPRegsMode fp_mode) {
  switch (fp_mode) {
    case SaveFPRegsMode::kIgnore:
      return Builtin::kEphemeronKeyBarrierIgnoreFP;
    case SaveFPRegsMode::kSave:
      return Builtin::kEphemeronKeyBarrierSaveFP;
  }
}

// static
constexpr Builtin Builtins::CallFunction(ConvertReceiverMode mode) {
  switch (mode) {
    case ConvertReceiverMode::kNullOrUndefined:
      return Builtin::kCallFunction_ReceiverIsNullOrUndefined;
    case ConvertReceiverMode::kNotNullOrUndefined:
      return Builtin::kCallFunction_ReceiverIsNotNullOrUndefined;
    case ConvertReceiverMode::kAny:
      return Builtin::kCallFunction_ReceiverIsAny;
  }
  UNREACHABLE();
}

// static
constexpr Builtin Builtins::Call(ConvertReceiverMode mode) {
  switch (mode) {
    case ConvertReceiverMode::kNullOrUndefined:
      return Builtin::kCall_ReceiverIsNullOrUndefined;
    case ConvertReceiverMode::kNotNullOrUndefined:
      return Builtin::kCall_ReceiverIsNotNullOrUndefined;
    case ConvertReceiverMode::kAny:
      return Builtin::kCall_ReceiverIsAny;
  }
  UNREACHABLE();
}

// static
constexpr bool Builtins::IsJSEntryVariant(Builtin builtin) {
  switch (builtin) {
    case Builtin::kJSEntry:
    case Builtin::kJSConstructEntry:
    case Builtin::kJSRunMicrotasksEntry:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_INL_H_
