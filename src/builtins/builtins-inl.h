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
constexpr Builtin Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return Builtin::kNonPrimitiveToPrimitive_Default;
    case ToPrimitiveHint::kNumber:
      return Builtin::kNonPrimitiveToPrimitive_Number;
    case ToPrimitiveHint::kString:
      return Builtin::kNonPrimitiveToPrimitive_String;
  }
  UNREACHABLE();
}

// static
constexpr Builtin Builtins::OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint) {
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      return Builtin::kOrdinaryToPrimitive_Number;
    case OrdinaryToPrimitiveHint::kString:
      return Builtin::kOrdinaryToPrimitive_String;
  }
  UNREACHABLE();
}

// static
constexpr Builtin Builtins::StringAdd(StringAddFlags flags) {
  switch (flags) {
    case STRING_ADD_CHECK_NONE:
      return Builtin::kStringAdd_CheckNone;
    case STRING_ADD_CONVERT_LEFT:
      return Builtin::kStringAddConvertLeft;
    case STRING_ADD_CONVERT_RIGHT:
      return Builtin::kStringAddConvertRight;
  }
  UNREACHABLE();
}

// static
constexpr Builtin Builtins::LoadGlobalIC(TypeofMode typeof_mode) {
  return typeof_mode == TypeofMode::kNotInside
             ? Builtin::kLoadGlobalICTrampoline
             : Builtin::kLoadGlobalICInsideTypeofTrampoline;
}

// static
constexpr Builtin Builtins::LoadGlobalICInOptimizedCode(
    TypeofMode typeof_mode) {
  return typeof_mode == TypeofMode::kNotInside
             ? Builtin::kLoadGlobalIC
             : Builtin::kLoadGlobalICInsideTypeof;
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
