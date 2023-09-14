// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_INL_H_
#define V8_BUILTINS_BUILTINS_INL_H_

#include "src/builtins/builtins.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

namespace builtins {
namespace detail {

struct BuiltinInfo {
  const char* name;
  Builtins::Kind kind;
};

#define DECL_CPP(Name, ...) {#Name, Builtins::CPP},
#define DECL_TFJ(Name, ...) {#Name, Builtins::TFJ},
#define DECL_TFC(Name, ...) {#Name, Builtins::TFC},
#define DECL_TFS(Name, ...) {#Name, Builtins::TFS},
#define DECL_TFH(Name, ...) {#Name, Builtins::TFH},
#define DECL_BCH(Name, ...) {#Name, Builtins::BCH},
#define DECL_ASM(Name, ...) {#Name, Builtins::ASM},
constexpr BuiltinInfo builtin_info[] = {BUILTIN_LIST(
    DECL_CPP, DECL_TFJ, DECL_TFC, DECL_TFS, DECL_TFH, DECL_BCH, DECL_ASM)};
#undef DECL_CPP
#undef DECL_TFJ
#undef DECL_TFC
#undef DECL_TFS
#undef DECL_TFH
#undef DECL_BCH
#undef DECL_ASM

}  // namespace detail
}  // namespace builtins

// static
constexpr BytecodeOffset Builtins::GetContinuationBytecodeOffset(
    Builtin builtin) {
  DCHECK(Builtins::KindOf(builtin) == TFJ || Builtins::KindOf(builtin) == TFC ||
         Builtins::KindOf(builtin) == TFS);
  return BytecodeOffset(BytecodeOffset::kFirstBuiltinContinuationId +
                        ToInt(builtin));
}

// static
constexpr Builtin Builtins::GetBuiltinFromBytecodeOffset(BytecodeOffset id) {
  Builtin builtin = Builtins::FromInt(
      id.ToInt() - BytecodeOffset::kFirstBuiltinContinuationId);
  DCHECK(Builtins::KindOf(builtin) == TFJ || Builtins::KindOf(builtin) == TFC ||
         Builtins::KindOf(builtin) == TFS);
  return builtin;
}

// stati
constexpr Builtin Builtins::GetRecordWriteStub(SaveFPRegsMode fp_mode,
                                               PointerType type) {
  switch (type) {
    case PointerType::kDirect:
      switch (fp_mode) {
        case SaveFPRegsMode::kIgnore:
          return Builtin::kRecordWriteIgnoreFP;
        case SaveFPRegsMode::kSave:
          return Builtin::kRecordWriteSaveFP;
      }
    case PointerType::kIndirect:
      switch (fp_mode) {
        case SaveFPRegsMode::kIgnore:
          return Builtin::kIndirectPointerBarrierIgnoreFP;
        case SaveFPRegsMode::kSave:
          return Builtin::kIndirectPointerBarrierSaveFP;
      }
  }
}

// static
constexpr Builtin Builtins::GetEphemeronKeyBarrierStub(SaveFPRegsMode fp_mode) {
  switch (fp_mode) {
    case SaveFPRegsMode::kIgnore:
      return Builtin::kEphemeronKeyBarrierIgnoreFP;
    case SaveFPRegsMode::kSave:
      return Builtin::kEphemeronKeyBarrierSaveFP;
  }
}

// static
constexpr const char* Builtins::name(Builtin builtin) {
  int index = ToInt(builtin);
  DCHECK(IsBuiltinId(index));
  return builtins::detail::builtin_info[index].name;
}

// static
constexpr const char* Builtins::NameForStackTrace(Builtin builtin) {
#if V8_ENABLE_WEBASSEMBLY
  // Most builtins are never shown in stack traces. Those that are exposed
  // to JavaScript get their name from the object referring to them. Here
  // we only support a few internal builtins that have special reasons for
  // being shown on stack traces:
  // - builtins that are allowlisted in {StubFrame::Summarize}.
  // - builtins that throw the same error as one of those above, but would
  //   lose information and e.g. print "indexOf" instead of "String.indexOf".
  switch (builtin) {
    case Builtin::kStringPrototypeToLocaleLowerCase:
      return "String.toLocaleLowerCase";
    case Builtin::kStringPrototypeIndexOf:
    case Builtin::kThrowIndexOfCalledOnNull:
      return "String.indexOf";
#if V8_INTL_SUPPORT
    case Builtin::kStringPrototypeToLowerCaseIntl:
#endif
    case Builtin::kThrowToLowerCaseCalledOnNull:
      return "String.toLowerCase";
    case Builtin::kWasmIntToString:
      return "Number.toString";
    default:
      // Callers getting this might well crash, which might be desirable
      // because it's similar to {UNREACHABLE()}, but contrary to that a
      // careful caller can also check the value and use it as an "is a
      // name available for this builtin?" check.
      return nullptr;
  }
#else
  return nullptr;
#endif  // V8_ENABLE_WEBASSEMBLY
}

// static
constexpr Builtins::Kind Builtins::KindOf(Builtin builtin) {
  DCHECK(IsBuiltinId(builtin));
  return builtins::detail::builtin_info[ToInt(builtin)].kind;
}

// static
constexpr const char* Builtins::KindNameOf(Builtin builtin) {
  Kind kind = Builtins::KindOf(builtin);
  // clang-format off
  switch (kind) {
    case CPP: return "CPP";
    case TFJ: return "TFJ";
    case TFC: return "TFC";
    case TFS: return "TFS";
    case TFH: return "TFH";
    case BCH: return "BCH";
    case ASM: return "ASM";
  }
  // clang-format on
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

// static
constexpr bool Builtins::IsCpp(Builtin builtin) {
  return Builtins::KindOf(builtin) == CPP;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_INL_H_
