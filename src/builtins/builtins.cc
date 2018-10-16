// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"

#include "src/api-inl.h"
#include "src/assembler-inl.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/callable.h"
#include "src/instruction-stream.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
#include "src/objects-inl.h"
#include "src/visitors.h"

namespace v8 {
namespace internal {

// Forward declarations for C++ builtins.
#define FORWARD_DECLARE(Name) \
  Object* Builtin_##Name(int argc, Object** args, Isolate* isolate);
BUILTIN_LIST_C(FORWARD_DECLARE)
#undef FORWARD_DECLARE

namespace {

// TODO(jgruber): Pack in CallDescriptors::Key.
struct BuiltinMetadata {
  const char* name;
  Builtins::Kind kind;
  union {
    Address cpp_entry;       // For CPP and API builtins.
    int8_t parameter_count;  // For TFJ builtins.
  } kind_specific_data;
};

// clang-format off
#define DECL_CPP(Name, ...) { #Name, Builtins::CPP, \
                              { FUNCTION_ADDR(Builtin_##Name) }},
#define DECL_API(Name, ...) { #Name, Builtins::API, \
                              { FUNCTION_ADDR(Builtin_##Name) }},
#ifdef V8_TARGET_BIG_ENDIAN
#define DECL_TFJ(Name, Count, ...) { #Name, Builtins::TFJ, \
  { static_cast<Address>(static_cast<uintptr_t>(           \
                              Count) << (kBitsPerByte * (kPointerSize - 1))) }},
#else
#define DECL_TFJ(Name, Count, ...) { #Name, Builtins::TFJ, \
                              { static_cast<Address>(Count) }},
#endif
#define DECL_TFC(Name, ...) { #Name, Builtins::TFC, {} },
#define DECL_TFS(Name, ...) { #Name, Builtins::TFS, {} },
#define DECL_TFH(Name, ...) { #Name, Builtins::TFH, {} },
#define DECL_BCH(Name, ...) { #Name, Builtins::BCH, {} },
#define DECL_DLH(Name, ...) { #Name, Builtins::DLH, {} },
#define DECL_ASM(Name, ...) { #Name, Builtins::ASM, {} },
const BuiltinMetadata builtin_metadata[] = {
  BUILTIN_LIST(DECL_CPP, DECL_API, DECL_TFJ, DECL_TFC, DECL_TFS, DECL_TFH,
               DECL_BCH, DECL_DLH, DECL_ASM)
};
#undef DECL_CPP
#undef DECL_API
#undef DECL_TFJ
#undef DECL_TFC
#undef DECL_TFS
#undef DECL_TFH
#undef DECL_BCH
#undef DECL_DLH
#undef DECL_ASM
// clang-format on

}  // namespace

BailoutId Builtins::GetContinuationBailoutId(Name name) {
  DCHECK(Builtins::KindOf(name) == TFJ || Builtins::KindOf(name) == TFC);
  return BailoutId(BailoutId::kFirstBuiltinContinuationId + name);
}

Builtins::Name Builtins::GetBuiltinFromBailoutId(BailoutId id) {
  int builtin_index = id.ToInt() - BailoutId::kFirstBuiltinContinuationId;
  DCHECK(Builtins::KindOf(builtin_index) == TFJ ||
         Builtins::KindOf(builtin_index) == TFC);
  return static_cast<Name>(builtin_index);
}

void Builtins::TearDown() { initialized_ = false; }

const char* Builtins::Lookup(Address pc) {
  // Off-heap pc's can be looked up through binary search.
  if (FLAG_embedded_builtins) {
    Code* maybe_builtin = InstructionStream::TryLookupCode(isolate_, pc);
    if (maybe_builtin != nullptr) return name(maybe_builtin->builtin_index());
  }

  // May be called during initialization (disassembler).
  if (initialized_) {
    for (int i = 0; i < builtin_count; i++) {
      if (isolate_->heap()->builtin(i)->contains(pc)) return name(i);
    }
  }
  return nullptr;
}

Handle<Code> Builtins::NewFunctionContext(ScopeType scope_type) {
  switch (scope_type) {
    case ScopeType::EVAL_SCOPE:
      return builtin_handle(kFastNewFunctionContextEval);
    case ScopeType::FUNCTION_SCOPE:
      return builtin_handle(kFastNewFunctionContextFunction);
    default:
      UNREACHABLE();
  }
  return Handle<Code>::null();
}

Handle<Code> Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return builtin_handle(kNonPrimitiveToPrimitive_Default);
    case ToPrimitiveHint::kNumber:
      return builtin_handle(kNonPrimitiveToPrimitive_Number);
    case ToPrimitiveHint::kString:
      return builtin_handle(kNonPrimitiveToPrimitive_String);
  }
  UNREACHABLE();
}

Handle<Code> Builtins::OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint) {
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      return builtin_handle(kOrdinaryToPrimitive_Number);
    case OrdinaryToPrimitiveHint::kString:
      return builtin_handle(kOrdinaryToPrimitive_String);
  }
  UNREACHABLE();
}

void Builtins::set_builtin(int index, HeapObject* builtin) {
  isolate_->heap()->set_builtin(index, builtin);
}

Code* Builtins::builtin(int index) { return isolate_->heap()->builtin(index); }

Handle<Code> Builtins::builtin_handle(int index) {
  DCHECK(IsBuiltinId(index));
  return Handle<Code>(
      reinterpret_cast<Code**>(isolate_->heap()->builtin_address(index)));
}

// static
int Builtins::GetStackParameterCount(Name name) {
  DCHECK(Builtins::KindOf(name) == TFJ);
  return builtin_metadata[name].kind_specific_data.parameter_count;
}

// static
Callable Builtins::CallableFor(Isolate* isolate, Name name) {
  Handle<Code> code = isolate->builtins()->builtin_handle(name);
  CallDescriptors::Key key;
  switch (name) {
// This macro is deliberately crafted so as to emit very little code,
// in order to keep binary size of this function under control.
#define CASE_OTHER(Name, ...)                          \
  case k##Name: {                                      \
    key = Builtin_##Name##_InterfaceDescriptor::key(); \
    break;                                             \
  }
    BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, CASE_OTHER,
                 CASE_OTHER, CASE_OTHER, IGNORE_BUILTIN, IGNORE_BUILTIN,
                 IGNORE_BUILTIN)
#undef CASE_OTHER
    default:
      Builtins::Kind kind = Builtins::KindOf(name);
      DCHECK(kind != BCH && kind != DLH);
      if (kind == TFJ || kind == CPP) {
        return Callable(code, JSTrampolineDescriptor{});
      }
      UNREACHABLE();
  }
  CallInterfaceDescriptor descriptor(key);
  return Callable(code, descriptor);
}

// static
const char* Builtins::name(int index) {
  DCHECK(IsBuiltinId(index));
  return builtin_metadata[index].name;
}

// static
Address Builtins::CppEntryOf(int index) {
  DCHECK(Builtins::HasCppImplementation(index));
  return builtin_metadata[index].kind_specific_data.cpp_entry;
}

// static
bool Builtins::IsBuiltin(const Code* code) {
  return Builtins::IsBuiltinId(code->builtin_index());
}

bool Builtins::IsBuiltinHandle(Handle<HeapObject> maybe_code,
                               int* index) const {
  Heap* heap = isolate_->heap();
  Address handle_location = maybe_code.address();
  Address start = heap->builtin_address(0);
  Address end = heap->builtin_address(Builtins::builtin_count);
  if (handle_location >= end) return false;
  if (handle_location < start) return false;
  *index = static_cast<int>(handle_location - start) >> kPointerSizeLog2;
  DCHECK(Builtins::IsBuiltinId(*index));
  return true;
}

// static
bool Builtins::IsIsolateIndependentBuiltin(const Code* code) {
  if (FLAG_embedded_builtins) {
    const int builtin_index = code->builtin_index();
    return Builtins::IsBuiltinId(builtin_index) &&
           Builtins::IsIsolateIndependent(builtin_index);
  } else {
    return false;
  }
}

// static
bool Builtins::IsLazy(int index) {
  DCHECK(IsBuiltinId(index));

  if (FLAG_embedded_builtins) {
    // We don't want to lazy-deserialize off-heap builtins.
    if (Builtins::IsIsolateIndependent(index)) return false;
  }

  // There are a couple of reasons that builtins can require eager-loading,
  // i.e. deserialization at isolate creation instead of on-demand. For
  // instance:
  // * DeserializeLazy implements lazy loading.
  // * Immovability requirement. This can only conveniently be guaranteed at
  //   isolate creation (at runtime, we'd have to allocate in LO space).
  // * To avoid conflicts in SharedFunctionInfo::function_data (Illegal,
  //   HandleApiCall, interpreter entry trampolines).
  // * Frequent use makes lazy loading unnecessary (CompileLazy).
  // TODO(wasm): Remove wasm builtins once immovability is no longer required.
  switch (index) {
    case kAbort:  // Required by wasm.
    case kArrayEveryLoopEagerDeoptContinuation:
    case kArrayEveryLoopLazyDeoptContinuation:
    case kArrayFilterLoopEagerDeoptContinuation:
    case kArrayFilterLoopLazyDeoptContinuation:
    case kArrayFindIndexLoopAfterCallbackLazyDeoptContinuation:
    case kArrayFindIndexLoopEagerDeoptContinuation:
    case kArrayFindIndexLoopLazyDeoptContinuation:
    case kArrayFindLoopAfterCallbackLazyDeoptContinuation:
    case kArrayFindLoopEagerDeoptContinuation:
    case kArrayFindLoopLazyDeoptContinuation:
    case kArrayForEachLoopEagerDeoptContinuation:
    case kArrayForEachLoopLazyDeoptContinuation:
    case kArrayMapLoopEagerDeoptContinuation:
    case kArrayMapLoopLazyDeoptContinuation:
    case kArrayReduceLoopEagerDeoptContinuation:
    case kArrayReduceLoopLazyDeoptContinuation:
    case kArrayReducePreLoopEagerDeoptContinuation:
    case kArrayReduceRightLoopEagerDeoptContinuation:
    case kArrayReduceRightLoopLazyDeoptContinuation:
    case kArrayReduceRightPreLoopEagerDeoptContinuation:
    case kArraySomeLoopEagerDeoptContinuation:
    case kArraySomeLoopLazyDeoptContinuation:
    case kAsyncFunctionAwaitResolveClosure:   // https://crbug.com/v8/7522
    case kAsyncGeneratorAwaitResolveClosure:  // https://crbug.com/v8/7522
    case kAsyncGeneratorYieldResolveClosure:  // https://crbug.com/v8/7522
    case kAsyncGeneratorAwaitCaught:          // https://crbug.com/v8/6786.
    case kAsyncGeneratorAwaitUncaught:        // https://crbug.com/v8/6786.
    // CEntry variants must be immovable, whereas lazy deserialization allocates
    // movable code.
    case kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit:
    case kCEntry_Return1_DontSaveFPRegs_ArgvOnStack_BuiltinExit:
    case kCEntry_Return1_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit:
    case kCEntry_Return1_SaveFPRegs_ArgvOnStack_NoBuiltinExit:
    case kCEntry_Return1_SaveFPRegs_ArgvOnStack_BuiltinExit:
    case kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_NoBuiltinExit:
    case kCEntry_Return2_DontSaveFPRegs_ArgvOnStack_BuiltinExit:
    case kCEntry_Return2_DontSaveFPRegs_ArgvInRegister_NoBuiltinExit:
    case kCEntry_Return2_SaveFPRegs_ArgvOnStack_NoBuiltinExit:
    case kCEntry_Return2_SaveFPRegs_ArgvOnStack_BuiltinExit:
    case kCompileLazy:
    case kDebugBreakTrampoline:
    case kDeserializeLazy:
    case kDeserializeLazyHandler:
    case kDeserializeLazyWideHandler:
    case kDeserializeLazyExtraWideHandler:
    case kFunctionPrototypeHasInstance:  // https://crbug.com/v8/6786.
    case kHandleApiCall:
    case kIllegal:
    case kIllegalHandler:
    case kInstantiateAsmJs:
    case kInterpreterEnterBytecodeAdvance:
    case kInterpreterEnterBytecodeDispatch:
    case kInterpreterEntryTrampoline:
    case kPromiseConstructorLazyDeoptContinuation:
    case kRecordWrite:  // https://crbug.com/chromium/765301.
    case kThrowWasmTrapDivByZero:             // Required by wasm.
    case kThrowWasmTrapDivUnrepresentable:    // Required by wasm.
    case kThrowWasmTrapFloatUnrepresentable:  // Required by wasm.
    case kThrowWasmTrapFuncInvalid:           // Required by wasm.
    case kThrowWasmTrapFuncSigMismatch:       // Required by wasm.
    case kThrowWasmTrapMemOutOfBounds:        // Required by wasm.
    case kThrowWasmTrapRemByZero:             // Required by wasm.
    case kThrowWasmTrapUnreachable:           // Required by wasm.
    case kToBooleanLazyDeoptContinuation:
    case kToNumber:                           // Required by wasm.
    case kGenericConstructorLazyDeoptContinuation:
    case kWasmCompileLazy:                    // Required by wasm.
    case kWasmStackGuard:                     // Required by wasm.
      return false;
    default:
      // TODO(6624): Extend to other kinds.
      return KindOf(index) == TFJ || KindOf(index) == BCH;
  }
  UNREACHABLE();
}

// static
bool Builtins::IsLazyDeserializer(Code* code) {
  return IsLazyDeserializer(code->builtin_index());
}

// static
bool Builtins::IsIsolateIndependent(int index) {
  DCHECK(IsBuiltinId(index));
#ifndef V8_TARGET_ARCH_IA32
  switch (index) {
    // TODO(jgruber): There's currently two blockers for moving
    // InterpreterEntryTrampoline into the binary:
    // 1. InterpreterEnterBytecode calculates a pointer into the middle of
    //    InterpreterEntryTrampoline (see interpreter_entry_return_pc_offset).
    //    When the builtin is embedded, the pointer would need to be calculated
    //    at an offset from the embedded instruction stream (instead of the
    //    trampoline code object).
    // 2. We create distinct copies of the trampoline to make it possible to
    //    attribute ticks in the interpreter to individual JS functions.
    //    See https://crrev.com/c/959081 and InstallBytecodeArray. When the
    //    trampoline is embedded, we need to ensure that CopyCode creates a copy
    //    of the builtin itself (and not just the trampoline).
    case kInterpreterEntryTrampoline:
      return false;
    default:
      return true;
  }
#else   // V8_TARGET_ARCH_IA32
  // TODO(jgruber, v8:6666): Implement support.
  // ia32 is a work-in-progress. This will let us make builtins
  // isolate-independent one-by-one.
  switch (index) {
#ifdef V8_INTL_SUPPORT
    case kCollatorConstructor:
    case kCollatorInternalCompare:
    case kCollatorPrototypeCompare:
    case kCollatorPrototypeResolvedOptions:
    case kCollatorSupportedLocalesOf:
    case kDatePrototypeToLocaleDateString:
    case kDatePrototypeToLocaleString:
    case kDatePrototypeToLocaleTimeString:
    case kDateTimeFormatConstructor:
    case kDateTimeFormatInternalFormat:
    case kDateTimeFormatPrototypeFormat:
    case kDateTimeFormatPrototypeFormatToParts:
    case kDateTimeFormatPrototypeResolvedOptions:
    case kDateTimeFormatSupportedLocalesOf:
    case kListFormatConstructor:
    case kListFormatPrototypeResolvedOptions:
    case kListFormatSupportedLocalesOf:
    case kLocaleConstructor:
    case kLocalePrototypeBaseName:
    case kLocalePrototypeCalendar:
    case kLocalePrototypeCaseFirst:
    case kLocalePrototypeCollation:
    case kLocalePrototypeHourCycle:
    case kLocalePrototypeLanguage:
    case kLocalePrototypeMaximize:
    case kLocalePrototypeMinimize:
    case kLocalePrototypeNumberingSystem:
    case kLocalePrototypeNumeric:
    case kLocalePrototypeRegion:
    case kLocalePrototypeScript:
    case kLocalePrototypeToString:
    case kNumberFormatConstructor:
    case kNumberFormatInternalFormatNumber:
    case kNumberFormatPrototypeFormatNumber:
    case kNumberFormatPrototypeFormatToParts:
    case kNumberFormatPrototypeResolvedOptions:
    case kNumberFormatSupportedLocalesOf:
    case kPluralRulesConstructor:
    case kPluralRulesPrototypeResolvedOptions:
    case kPluralRulesPrototypeSelect:
    case kPluralRulesSupportedLocalesOf:
    case kRelativeTimeFormatConstructor:
    case kRelativeTimeFormatPrototypeFormat:
    case kRelativeTimeFormatPrototypeFormatToParts:
    case kRelativeTimeFormatPrototypeResolvedOptions:
    case kRelativeTimeFormatSupportedLocalesOf:
    case kSegmenterConstructor:
    case kSegmenterPrototypeResolvedOptions:
    case kSegmenterSupportedLocalesOf:
    case kStringPrototypeNormalizeIntl:
    case kStringPrototypeToUpperCaseIntl:
    case kV8BreakIteratorConstructor:
    case kV8BreakIteratorInternalAdoptText:
    case kV8BreakIteratorInternalBreakType:
    case kV8BreakIteratorInternalCurrent:
    case kV8BreakIteratorInternalFirst:
    case kV8BreakIteratorInternalNext:
    case kV8BreakIteratorPrototypeAdoptText:
    case kV8BreakIteratorPrototypeBreakType:
    case kV8BreakIteratorPrototypeCurrent:
    case kV8BreakIteratorPrototypeFirst:
    case kV8BreakIteratorPrototypeNext:
    case kV8BreakIteratorPrototypeResolvedOptions:
    case kV8BreakIteratorSupportedLocalesOf:
#endif  // V8_INTL_SUPPORT
    case kArrayBufferConstructor:
    case kArrayBufferConstructor_DoNotInitialize:
    case kArrayBufferIsView:
    case kArrayBufferPrototypeGetByteLength:
    case kArrayBufferPrototypeSlice:
    case kArrayConcat:
    case kArrayIncludesHoleyDoubles:
    case kArrayIncludesPackedDoubles:
    case kArrayIndexOfHoleyDoubles:
    case kArrayIndexOfPackedDoubles:
    case kArrayPop:
    case kArrayPrototypeFill:
    case kArrayPush:
    case kArrayShift:
    case kArrayUnshift:
    case kAsyncFunctionConstructor:
    case kAsyncFunctionLazyDeoptContinuation:
    case kAsyncGeneratorFunctionConstructor:
    case kAtomicsIsLockFree:
    case kAtomicsNotify:
    case kAtomicsWait:
    case kAtomicsWake:
    case kBigIntAsIntN:
    case kBigIntAsUintN:
    case kBigIntConstructor:
    case kBigIntPrototypeToLocaleString:
    case kBigIntPrototypeToString:
    case kBigIntPrototypeValueOf:
    case kBooleanConstructor:
    case kCallBoundFunction:
    case kCallForwardVarargs:
    case kCallFunctionForwardVarargs:
    case kCallSitePrototypeGetColumnNumber:
    case kCallSitePrototypeGetEvalOrigin:
    case kCallSitePrototypeGetFileName:
    case kCallSitePrototypeGetFunction:
    case kCallSitePrototypeGetFunctionName:
    case kCallSitePrototypeGetLineNumber:
    case kCallSitePrototypeGetMethodName:
    case kCallSitePrototypeGetPosition:
    case kCallSitePrototypeGetScriptNameOrSourceURL:
    case kCallSitePrototypeGetThis:
    case kCallSitePrototypeGetTypeName:
    case kCallSitePrototypeIsAsync:
    case kCallSitePrototypeIsConstructor:
    case kCallSitePrototypeIsEval:
    case kCallSitePrototypeIsNative:
    case kCallSitePrototypeIsToplevel:
    case kCallSitePrototypeToString:
    case kCallVarargs:
    case kCanUseSameAccessor20ATDictionaryElements:
    case kCanUseSameAccessor25ATGenericElementsAccessor:
    case kConsoleAssert:
    case kConsoleClear:
    case kConsoleContext:
    case kConsoleCount:
    case kConsoleCountReset:
    case kConsoleDebug:
    case kConsoleDir:
    case kConsoleDirXml:
    case kConsoleError:
    case kConsoleGroup:
    case kConsoleGroupCollapsed:
    case kConsoleGroupEnd:
    case kConsoleInfo:
    case kConsoleLog:
    case kConsoleProfile:
    case kConsoleProfileEnd:
    case kConsoleTable:
    case kConsoleTime:
    case kConsoleTimeEnd:
    case kConsoleTimeLog:
    case kConsoleTimeStamp:
    case kConsoleTrace:
    case kConsoleWarn:
    case kConstructBoundFunction:
    case kConstructedNonConstructable:
    case kConstructForwardVarargs:
    case kConstructFunction:
    case kConstructFunctionForwardVarargs:
    case kConstructVarargs:
    case kContinueToCodeStubBuiltin:
    case kContinueToCodeStubBuiltinWithResult:
    case kContinueToJavaScriptBuiltin:
    case kContinueToJavaScriptBuiltinWithResult:
    case kDataViewConstructor:
    case kDateConstructor:
    case kDateNow:
    case kDateParse:
    case kDatePrototypeGetYear:
    case kDatePrototypeSetDate:
    case kDatePrototypeSetFullYear:
    case kDatePrototypeSetHours:
    case kDatePrototypeSetMilliseconds:
    case kDatePrototypeSetMinutes:
    case kDatePrototypeSetMonth:
    case kDatePrototypeSetSeconds:
    case kDatePrototypeSetTime:
    case kDatePrototypeSetUTCDate:
    case kDatePrototypeSetUTCFullYear:
    case kDatePrototypeSetUTCHours:
    case kDatePrototypeSetUTCMilliseconds:
    case kDatePrototypeSetUTCMinutes:
    case kDatePrototypeSetUTCMonth:
    case kDatePrototypeSetUTCSeconds:
    case kDatePrototypeSetYear:
    case kDatePrototypeToDateString:
    case kDatePrototypeToISOString:
    case kDatePrototypeToJson:
    case kDatePrototypeToString:
    case kDatePrototypeToTimeString:
    case kDatePrototypeToUTCString:
    case kDateUTC:
    case kDoubleToI:
    case kEmptyFunction:
    case kErrorCaptureStackTrace:
    case kErrorConstructor:
    case kErrorPrototypeToString:
    case kExtraWideHandler:
    case kForInContinueExtraWideHandler:
    case kForInContinueHandler:
    case kForInContinueWideHandler:
    case kForInPrepareExtraWideHandler:
    case kForInPrepareHandler:
    case kForInPrepareWideHandler:
    case kForInStepExtraWideHandler:
    case kForInStepHandler:
    case kForInStepWideHandler:
    case kFunctionConstructor:
    case kFunctionPrototypeApply:
    case kFunctionPrototypeBind:
    case kFunctionPrototypeCall:
    case kFunctionPrototypeToString:
    case kGeneratorFunctionConstructor:
    case kGenericBuiltinTest22UT12ATHeapObject5ATSmi:
    case kGenericBuiltinTest5ATSmi:
    case kGlobalDecodeURI:
    case kGlobalDecodeURIComponent:
    case kGlobalEncodeURI:
    case kGlobalEncodeURIComponent:
    case kGlobalEscape:
    case kGlobalEval:
    case kGlobalUnescape:
    case kHandleApiCall:
    case kHandleApiCallAsConstructor:
    case kHandleApiCallAsFunction:
    case kIllegal:
    case kInstantiateAsmJs:
    case kInternalArrayConstructor:
    case kInterpreterOnStackReplacement:
    case kInterpreterPushArgsThenCall:
    case kInterpreterPushArgsThenCallWithFinalSpread:
    case kInterpreterPushArgsThenConstruct:
    case kInterpreterPushArgsThenConstructWithFinalSpread:
    case kInterpreterPushUndefinedAndArgsThenCall:
    case kInterruptCheck:
    case kIsPromise:
    case kIsTraceCategoryEnabled:
    case kJSConstructEntryTrampoline:
    case kJSEntryTrampoline:
    case kJsonParse:
    case kJsonStringify:
    case kJumpConstantExtraWideHandler:
    case kJumpConstantHandler:
    case kJumpConstantWideHandler:
    case kJumpExtraWideHandler:
    case kJumpHandler:
    case kJumpIfFalseConstantExtraWideHandler:
    case kJumpIfFalseConstantHandler:
    case kJumpIfFalseConstantWideHandler:
    case kJumpIfFalseExtraWideHandler:
    case kJumpIfFalseHandler:
    case kJumpIfFalseWideHandler:
    case kJumpIfJSReceiverConstantExtraWideHandler:
    case kJumpIfJSReceiverConstantHandler:
    case kJumpIfJSReceiverConstantWideHandler:
    case kJumpIfJSReceiverExtraWideHandler:
    case kJumpIfJSReceiverHandler:
    case kJumpIfJSReceiverWideHandler:
    case kJumpIfNotNullConstantExtraWideHandler:
    case kJumpIfNotNullConstantHandler:
    case kJumpIfNotNullConstantWideHandler:
    case kJumpIfNotNullExtraWideHandler:
    case kJumpIfNotNullHandler:
    case kJumpIfNotNullWideHandler:
    case kJumpIfNotUndefinedConstantExtraWideHandler:
    case kJumpIfNotUndefinedConstantHandler:
    case kJumpIfNotUndefinedConstantWideHandler:
    case kJumpIfNotUndefinedExtraWideHandler:
    case kJumpIfNotUndefinedHandler:
    case kJumpIfNotUndefinedWideHandler:
    case kJumpIfNullConstantExtraWideHandler:
    case kJumpIfNullConstantHandler:
    case kJumpIfNullConstantWideHandler:
    case kJumpIfNullExtraWideHandler:
    case kJumpIfNullHandler:
    case kJumpIfNullWideHandler:
    case kJumpIfTrueConstantExtraWideHandler:
    case kJumpIfTrueConstantHandler:
    case kJumpIfTrueConstantWideHandler:
    case kJumpIfTrueExtraWideHandler:
    case kJumpIfTrueHandler:
    case kJumpIfTrueWideHandler:
    case kJumpIfUndefinedConstantExtraWideHandler:
    case kJumpIfUndefinedConstantHandler:
    case kJumpIfUndefinedConstantWideHandler:
    case kJumpIfUndefinedExtraWideHandler:
    case kJumpIfUndefinedHandler:
    case kJumpIfUndefinedWideHandler:
    case kJumpWideHandler:
    case kLdaConstantExtraWideHandler:
    case kLdaConstantHandler:
    case kLdaConstantWideHandler:
    case kLdaContextSlotExtraWideHandler:
    case kLdaContextSlotHandler:
    case kLdaContextSlotWideHandler:
    case kLdaCurrentContextSlotExtraWideHandler:
    case kLdaCurrentContextSlotHandler:
    case kLdaCurrentContextSlotWideHandler:
    case kLdaFalseHandler:
    case kLdaImmutableContextSlotExtraWideHandler:
    case kLdaImmutableContextSlotHandler:
    case kLdaImmutableContextSlotWideHandler:
    case kLdaImmutableCurrentContextSlotExtraWideHandler:
    case kLdaImmutableCurrentContextSlotHandler:
    case kLdaImmutableCurrentContextSlotWideHandler:
    case kLdaModuleVariableExtraWideHandler:
    case kLdaModuleVariableHandler:
    case kLdaModuleVariableWideHandler:
    case kLdaNullHandler:
    case kLdarExtraWideHandler:
    case kLdarHandler:
    case kLdarWideHandler:
    case kLdaSmiExtraWideHandler:
    case kLdaSmiHandler:
    case kLdaSmiWideHandler:
    case kLdaTheHoleHandler:
    case kLdaTrueHandler:
    case kLdaUndefinedHandler:
    case kLdaZeroHandler:
    case kLoad20ATDictionaryElements:
    case kLoad23ATFastPackedSmiElements:
    case kLoad25ATFastSmiOrObjectElements:
    case kLoadFixedElement16ATFixedInt8Array:
    case kLoadFixedElement17ATFixedInt16Array:
    case kLoadFixedElement17ATFixedUint8Array:
    case kLoadFixedElement18ATFixedUint16Array:
    case kLoadFixedElement24ATFixedUint8ClampedArray:
    case kLoadIC_StringLength:
    case kLoadIC_StringWrapperLength:
    case kLogicalNotHandler:
    case kMakeError:
    case kMakeRangeError:
    case kMakeSyntaxError:
    case kMakeTypeError:
    case kMakeURIError:
    case kMapPrototypeClear:
    case kMathHypot:
    case kMathPowInternal:
    case kMovExtraWideHandler:
    case kMovHandler:
    case kMovWideHandler:
    case kNotifyDeoptimized:
    case kNumberPrototypeToExponential:
    case kNumberPrototypeToFixed:
    case kNumberPrototypeToLocaleString:
    case kNumberPrototypeToPrecision:
    case kNumberPrototypeToString:
    case kObjectDefineGetter:
    case kObjectDefineProperties:
    case kObjectDefineProperty:
    case kObjectDefineSetter:
    case kObjectFreeze:
    case kObjectGetOwnPropertyDescriptors:
    case kObjectGetOwnPropertySymbols:
    case kObjectGetPrototypeOf:
    case kObjectIsExtensible:
    case kObjectIsFrozen:
    case kObjectIsSealed:
    case kObjectLookupGetter:
    case kObjectLookupSetter:
    case kObjectPreventExtensions:
    case kObjectPrototypeGetProto:
    case kObjectPrototypePropertyIsEnumerable:
    case kObjectPrototypeSetProto:
    case kObjectSeal:
    case kObjectSetPrototypeOf:
    case kOrderedHashTableHealIndex:
    case kPopContextExtraWideHandler:
    case kPopContextHandler:
    case kPopContextWideHandler:
    case kPushContextExtraWideHandler:
    case kPushContextHandler:
    case kPushContextWideHandler:
    case kRecordWrite:
    case kReflectApply:
    case kReflectConstruct:
    case kReflectDefineProperty:
    case kReflectDeleteProperty:
    case kReflectGet:
    case kReflectGetOwnPropertyDescriptor:
    case kReflectGetPrototypeOf:
    case kReflectIsExtensible:
    case kReflectOwnKeys:
    case kReflectPreventExtensions:
    case kReflectSet:
    case kReflectSetPrototypeOf:
    case kRegExpCapture1Getter:
    case kRegExpCapture2Getter:
    case kRegExpCapture3Getter:
    case kRegExpCapture4Getter:
    case kRegExpCapture5Getter:
    case kRegExpCapture6Getter:
    case kRegExpCapture7Getter:
    case kRegExpCapture8Getter:
    case kRegExpCapture9Getter:
    case kRegExpInputGetter:
    case kRegExpInputSetter:
    case kRegExpLastMatchGetter:
    case kRegExpLastParenGetter:
    case kRegExpLeftContextGetter:
    case kRegExpPrototypeToString:
    case kRegExpRightContextGetter:
    case kResumeGeneratorTrampoline:
    case kSetPendingMessageHandler:
    case kSetPrototypeClear:
    case kSharedArrayBufferPrototypeGetByteLength:
    case kSharedArrayBufferPrototypeSlice:
    case kStackCheck:
    case kStaContextSlotExtraWideHandler:
    case kStaContextSlotHandler:
    case kStaContextSlotWideHandler:
    case kStaCurrentContextSlotExtraWideHandler:
    case kStaCurrentContextSlotHandler:
    case kStaCurrentContextSlotWideHandler:
    case kStarExtraWideHandler:
    case kStarHandler:
    case kStarWideHandler:
    case kStore19ATTempArrayElements:
    case kStore20ATFastDoubleElements:
    case kStore23ATFastPackedSmiElements:
    case kStore25ATFastSmiOrObjectElements:
    case kStoreFixedElement16ATFixedInt8Array:
    case kStoreFixedElement17ATFixedInt16Array:
    case kStoreFixedElement17ATFixedUint8Array:
    case kStoreFixedElement18ATFixedUint16Array:
    case kStoreFixedElement19ATFixedFloat32Array:
    case kStoreFixedElement19ATFixedFloat64Array:
    case kStoreFixedElement20ATFixedBigInt64Array:
    case kStoreFixedElement21ATFixedBigUint64Array:
    case kStoreFixedElement24ATFixedUint8ClampedArray:
    case kStrictPoisonPillThrower:
    case kStringFromCodePoint:
    case kStringPrototypeEndsWith:
    case kStringPrototypeLastIndexOf:
    case kStringPrototypeLocaleCompare:
    case kStringPrototypeStartsWith:
    case kStringPrototypeToLocaleLowerCase:
    case kStringPrototypeToLocaleUpperCase:
    case kStringRaw:
    case kSwitchOnGeneratorStateExtraWideHandler:
    case kSwitchOnGeneratorStateHandler:
    case kSwitchOnGeneratorStateWideHandler:
    case kSwitchOnSmiNoFeedbackExtraWideHandler:
    case kSwitchOnSmiNoFeedbackHandler:
    case kSwitchOnSmiNoFeedbackWideHandler:
    case kSymbolConstructor:
    case kSymbolFor:
    case kSymbolKeyFor:
    case kTestHelperPlus1:
    case kTestHelperPlus2:
    case kTestNullHandler:
    case kTestReferenceEqualExtraWideHandler:
    case kTestReferenceEqualHandler:
    case kTestReferenceEqualWideHandler:
    case kTestTypeOfHandler:
    case kTestUndefinedHandler:
    case kTestUndetectableHandler:
    case kThrowWasmTrapDivByZero:
    case kThrowWasmTrapDivUnrepresentable:
    case kThrowWasmTrapFloatUnrepresentable:
    case kThrowWasmTrapFuncInvalid:
    case kThrowWasmTrapFuncSigMismatch:
    case kThrowWasmTrapMemOutOfBounds:
    case kThrowWasmTrapRemByZero:
    case kThrowWasmTrapUnalignedAccess:
    case kThrowWasmTrapUnreachable:
    case kTrace:
    case kTypedArrayPrototypeBuffer:
    case kTypedArrayPrototypeCopyWithin:
    case kTypedArrayPrototypeFill:
    case kTypedArrayPrototypeIncludes:
    case kTypedArrayPrototypeIndexOf:
    case kTypedArrayPrototypeLastIndexOf:
    case kTypedArrayPrototypeReverse:
    case kTypeof:
    case kTypeOfHandler:
    case kUnsupportedThrower:
    case kWasmAllocateHeapNumber:
    case kWasmCallJavaScript:
    case kWasmCompileLazy:
    case kWasmGrowMemory:
    case kWasmStackGuard:
    case kWasmThrow:
    case kWasmToNumber:
    case kWeakFactoryCleanupIteratorNext:
    case kWeakFactoryConstructor:
    case kWeakFactoryMakeCell:
    case kWeakMapLookupHashIndex:
    case kWideHandler:
      return true;
    default:
      return false;
  }
#endif  // V8_TARGET_ARCH_IA32
  UNREACHABLE();
}

// static
bool Builtins::IsWasmRuntimeStub(int index) {
  DCHECK(IsBuiltinId(index));
  switch (index) {
#define CASE_TRAP(Name) case kThrowWasm##Name:
#define CASE(Name) case k##Name:
    WASM_RUNTIME_STUB_LIST(CASE, CASE_TRAP)
#undef CASE_TRAP
#undef CASE
    return true;
    default:
      return false;
  }
  UNREACHABLE();
}

// static
Handle<Code> Builtins::GenerateOffHeapTrampolineFor(Isolate* isolate,
                                                    Address off_heap_entry) {
  DCHECK(isolate->serializer_enabled());
  DCHECK_NOT_NULL(isolate->embedded_blob());
  DCHECK_NE(0, isolate->embedded_blob_size());

  constexpr size_t buffer_size = 256;  // Enough to fit the single jmp.
  byte buffer[buffer_size];            // NOLINT(runtime/arrays)

  // Generate replacement code that simply tail-calls the off-heap code.
  MacroAssembler masm(isolate, buffer, buffer_size, CodeObjectRequired::kYes);
  DCHECK(!masm.has_frame());
  {
    FrameScope scope(&masm, StackFrame::NONE);
    masm.JumpToInstructionStream(off_heap_entry);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);

  return isolate->factory()->NewCode(desc, Code::BUILTIN, masm.CodeObject());
}

// static
Builtins::Kind Builtins::KindOf(int index) {
  DCHECK(IsBuiltinId(index));
  return builtin_metadata[index].kind;
}

// static
const char* Builtins::KindNameOf(int index) {
  Kind kind = Builtins::KindOf(index);
  // clang-format off
  switch (kind) {
    case CPP: return "CPP";
    case API: return "API";
    case TFJ: return "TFJ";
    case TFC: return "TFC";
    case TFS: return "TFS";
    case TFH: return "TFH";
    case BCH: return "BCH";
    case DLH: return "DLH";
    case ASM: return "ASM";
  }
  // clang-format on
  UNREACHABLE();
}

// static
bool Builtins::IsCpp(int index) { return Builtins::KindOf(index) == CPP; }

// static
bool Builtins::HasCppImplementation(int index) {
  Kind kind = Builtins::KindOf(index);
  return (kind == CPP || kind == API);
}

// static
bool Builtins::AllowDynamicFunction(Isolate* isolate, Handle<JSFunction> target,
                                    Handle<JSObject> target_global_proxy) {
  if (FLAG_allow_unsafe_function_constructor) return true;
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  Handle<Context> responsible_context =
      impl->MicrotaskContextIsLastEnteredContext() ? impl->MicrotaskContext()
                                                   : impl->LastEnteredContext();
  // TODO(jochen): Remove this.
  if (responsible_context.is_null()) {
    return true;
  }
  if (*responsible_context == target->context()) return true;
  return isolate->MayAccess(responsible_context, target_global_proxy);
}

}  // namespace internal
}  // namespace v8
