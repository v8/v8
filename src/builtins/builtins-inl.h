// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_INL_H_
#define V8_BUILTINS_BUILTINS_INL_H_

#include "src/builtins/builtins.h"
// Include the non-inl header before the rest of the headers.

#include "src/execution/isolate.h"

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
constexpr Builtin Builtins::AdaptorWithBuiltinExitFrame(
    int formal_parameter_count) {
  switch (formal_parameter_count) {
    case kDontAdaptArgumentsSentinel:
    case JSParameterCount(0):
      return Builtin::kAdaptorWithBuiltinExitFrame0;
    case JSParameterCount(1):
      return Builtin::kAdaptorWithBuiltinExitFrame1;
    case JSParameterCount(2):
      return Builtin::kAdaptorWithBuiltinExitFrame2;
    case JSParameterCount(3):
      return Builtin::kAdaptorWithBuiltinExitFrame3;
    case JSParameterCount(4):
      return Builtin::kAdaptorWithBuiltinExitFrame4;
    case JSParameterCount(5):
      return Builtin::kAdaptorWithBuiltinExitFrame5;
  }
  UNREACHABLE();
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
constexpr bool Builtins::IsAnyCall(Builtin builtin) {
  switch (builtin) {
    case Builtin::kCallFunction_ReceiverIsNullOrUndefined:
    case Builtin::kCallFunction_ReceiverIsNotNullOrUndefined:
    case Builtin::kCallFunction_ReceiverIsAny:
    case Builtin::kCall_ReceiverIsNullOrUndefined:
    case Builtin::kCall_ReceiverIsNotNullOrUndefined:
    case Builtin::kCall_ReceiverIsAny:
      return true;
    default:
      return false;
  }
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
constexpr Builtin Builtins::CEntry(int result_size, ArgvMode argv_mode,
                                   bool builtin_exit_frame,
                                   bool switch_to_central_stack) {
  // Aliases for readability below.
  const int rs = result_size;
  const ArgvMode am = argv_mode;
  const bool be = builtin_exit_frame;

  if (switch_to_central_stack) {
    DCHECK_EQ(result_size, 1);
    DCHECK_EQ(argv_mode, ArgvMode::kStack);
    DCHECK_EQ(builtin_exit_frame, false);
    return Builtin::kWasmCEntry;
  }

  if (rs == 1 && am == ArgvMode::kStack && !be) {
    return Builtin::kCEntry_Return1_ArgvOnStack_NoBuiltinExit;
  } else if (rs == 1 && am == ArgvMode::kStack && be) {
    return Builtin::kCEntry_Return1_ArgvOnStack_BuiltinExit;
  } else if (rs == 1 && am == ArgvMode::kRegister && !be) {
    return Builtin::kCEntry_Return1_ArgvInRegister_NoBuiltinExit;
  } else if (rs == 2 && am == ArgvMode::kStack && !be) {
    return Builtin::kCEntry_Return2_ArgvOnStack_NoBuiltinExit;
  } else if (rs == 2 && am == ArgvMode::kStack && be) {
    return Builtin::kCEntry_Return2_ArgvOnStack_BuiltinExit;
  } else if (rs == 2 && am == ArgvMode::kRegister && !be) {
    return Builtin::kCEntry_Return2_ArgvInRegister_NoBuiltinExit;
  }

  UNREACHABLE();
}

// static
constexpr Builtin Builtins::RuntimeCEntry(int result_size,
                                          bool switch_to_central_stack) {
  return CEntry(result_size, ArgvMode::kStack, false, switch_to_central_stack);
}

// static
constexpr Builtin Builtins::InterpreterCEntry(int result_size) {
  return CEntry(result_size, ArgvMode::kRegister);
}

// static
constexpr Builtin Builtins::InterpreterPushArgsThenCall(
    ConvertReceiverMode receiver_mode, InterpreterPushArgsMode mode) {
  switch (mode) {
    case InterpreterPushArgsMode::kArrayFunction:
      // There is no special-case handling of calls to Array. They will all go
      // through the kOther case below.
      UNREACHABLE();
    case InterpreterPushArgsMode::kWithFinalSpread:
      return Builtin::kInterpreterPushArgsThenCallWithFinalSpread;
    case InterpreterPushArgsMode::kOther:
      switch (receiver_mode) {
        case ConvertReceiverMode::kNullOrUndefined:
          return Builtin::kInterpreterPushUndefinedAndArgsThenCall;
        case ConvertReceiverMode::kNotNullOrUndefined:
        case ConvertReceiverMode::kAny:
          return Builtin::kInterpreterPushArgsThenCall;
      }
  }
  UNREACHABLE();
}

// static
constexpr Builtin Builtins::InterpreterPushArgsThenConstruct(
    InterpreterPushArgsMode mode) {
  switch (mode) {
    case InterpreterPushArgsMode::kArrayFunction:
      return Builtin::kInterpreterPushArgsThenConstructArrayFunction;
    case InterpreterPushArgsMode::kWithFinalSpread:
      return Builtin::kInterpreterPushArgsThenConstructWithFinalSpread;
    case InterpreterPushArgsMode::kOther:
      return Builtin::kInterpreterPushArgsThenConstruct;
  }
  UNREACHABLE();
}

// static
Address Builtins::EntryOf(Builtin builtin, Isolate* isolate) {
#ifdef V8_ENABLE_WEBASSEMBLY
  // We don't use the isolate-specific copy of the WasmToJS wrapper; use
  // EmbeddedEntryOf() instead to get the isolate-independent copy.
  DCHECK(builtin != Builtin::kWasmToJsWrapperCSA &&
         builtin != Builtin::kWasmToJsWrapperAsm &&
         builtin != Builtin::kWasmToJsWrapperInvalidSig);
#endif
  return isolate->builtin_entry_table()[Builtins::ToInt(builtin)];
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

// static
int Builtins::GetFormalParameterCount(Builtin builtin) {
  CHECK(HasJSLinkage(builtin));

  // TODO(saelo): consider merging GetFormalParameterCount and
  // GetStackParameterCount into a single function.
  if (Builtins::KindOf(builtin) == TFJ_TSA ||
      Builtins::KindOf(builtin) == TFJ) {
    return Builtins::GetStackParameterCount(builtin);
  } else if (Builtins::KindOf(builtin) == ASM ||
             Builtins::KindOf(builtin) == TFC) {
    // At the moment, all ASM builtins are varargs builtins. This is verified
    // in CheckFormalParameterCount.
    return kDontAdaptArgumentsSentinel;
  } else if (Builtins::KindOf(builtin) == CPP) {
#define CPP_BUILTIN(Name, Argc) \
  case Builtin::k##Name:        \
    return Argc;

    switch (builtin) {
      BUILTIN_LIST_C(CPP_BUILTIN)
      default:
        UNREACHABLE();
    }
#undef CPP_BUILTIN
  } else {
    UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_INL_H_
