// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/api.h"
#include "src/assembler-inl.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/callable.h"
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
#define DECL_ASM(Name, ...) { #Name, Builtins::ASM, {} },
const BuiltinMetadata builtin_metadata[] = {
  BUILTIN_LIST(DECL_CPP, DECL_API, DECL_TFJ, DECL_TFC, DECL_TFS, DECL_TFH,
               DECL_ASM)
};
#undef DECL_CPP
#undef DECL_API
#undef DECL_TFJ
#undef DECL_TFC
#undef DECL_TFS
#undef DECL_TFH
#undef DECL_ASM
// clang-format on

}  // namespace

Builtins::Builtins() : initialized_(false) {
  memset(builtins_, 0, sizeof(builtins_[0]) * builtin_count);
}

Builtins::~Builtins() {}

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

void Builtins::IterateBuiltins(RootVisitor* v) {
  for (int i = 0; i < builtin_count; i++) {
    v->VisitRootPointer(Root::kBuiltins, name(i), &builtins_[i]);
  }
}

const char* Builtins::Lookup(Address pc) {
  // may be called during initialization (disassembler!)
  if (initialized_) {
    for (int i = 0; i < builtin_count; i++) {
      Code* entry = Code::cast(builtins_[i]);
      if (entry->contains(pc)) return name(i);
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
  DCHECK(Builtins::IsBuiltinId(index));
  DCHECK(Internals::HasHeapObjectTag(builtin));
  // The given builtin may be completely uninitialized thus we cannot check its
  // type here.
  builtins_[index] = builtin;
}

Handle<Code> Builtins::builtin_handle(int index) {
  DCHECK(IsBuiltinId(index));
  return Handle<Code>(reinterpret_cast<Code**>(builtin_address(index)));
}

// static
int Builtins::GetStackParameterCount(Name name) {
  DCHECK(Builtins::KindOf(name) == TFJ);
  return builtin_metadata[name].kind_specific_data.parameter_count;
}

// static
Callable Builtins::CallableFor(Isolate* isolate, Name name) {
  Handle<Code> code(
      reinterpret_cast<Code**>(isolate->builtins()->builtin_address(name)));
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
                 CASE_OTHER, CASE_OTHER, IGNORE_BUILTIN)
#undef CASE_OTHER
    default:
      Builtins::Kind kind = Builtins::KindOf(name);
      if (kind == TFJ || kind == CPP) {
        return Callable(code, BuiltinDescriptor(isolate));
      }
      UNREACHABLE();
  }
  CallInterfaceDescriptor descriptor(isolate, key);
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

// static
bool Builtins::IsEmbeddedBuiltin(const Code* code) {
#ifdef V8_EMBEDDED_BUILTINS
  return Builtins::IsBuiltinId(code->builtin_index()) &&
         Builtins::IsIsolateIndependent(code->builtin_index());
#else
  return false;
#endif
}

// static
bool Builtins::IsLazy(int index) {
  DCHECK(IsBuiltinId(index));

#ifdef V8_EMBEDDED_BUILTINS
  // We don't want to lazy-deserialize off-heap builtins.
  if (Builtins::IsIsolateIndependent(index)) return false;
#endif

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
    case kAsyncGeneratorAwaitCaught:            // https://crbug.com/v8/6786.
    case kAsyncGeneratorAwaitUncaught:          // https://crbug.com/v8/6786.
    case kCompileLazy:
    case kDebugBreakTrampoline:
    case kDeserializeLazy:
    case kFunctionPrototypeHasInstance:  // https://crbug.com/v8/6786.
    case kHandleApiCall:
    case kIllegal:
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
    case kTypedArrayConstructorLazyDeoptContinuation:
    case kWasmCompileLazy:                    // Required by wasm.
    case kWasmStackGuard:                     // Required by wasm.
      return false;
    default:
      // TODO(6624): Extend to other kinds.
      return KindOf(index) == TFJ;
  }
  UNREACHABLE();
}

// static
bool Builtins::IsIsolateIndependent(int index) {
  DCHECK(IsBuiltinId(index));
  switch (index) {
    case kAbort:
    case kAbortJS:
    case kAdaptorWithBuiltinExitFrame:
    case kAdaptorWithExitFrame:
    case kAdd:
    case kAllocateHeapNumber:
    case kAllocateInNewSpace:
    case kAllocateInOldSpace:
    case kArgumentsAdaptorTrampoline:
    case kArrayBufferConstructor:
    case kArrayBufferConstructor_DoNotInitialize:
    case kArrayBufferIsView:
    case kArrayBufferPrototypeGetByteLength:
    case kArrayBufferPrototypeSlice:
    case kArrayConcat:
    case kArrayConstructor:
    case kArrayEvery:
    case kArrayEveryLoopContinuation:
    case kArrayEveryLoopEagerDeoptContinuation:
    case kArrayEveryLoopLazyDeoptContinuation:
    case kArrayFilter:
    case kArrayFilterLoopContinuation:
    case kArrayFilterLoopEagerDeoptContinuation:
    case kArrayFilterLoopLazyDeoptContinuation:
    case kArrayFindIndexLoopAfterCallbackLazyDeoptContinuation:
    case kArrayFindIndexLoopContinuation:
    case kArrayFindIndexLoopEagerDeoptContinuation:
    case kArrayFindIndexLoopLazyDeoptContinuation:
    case kArrayFindLoopAfterCallbackLazyDeoptContinuation:
    case kArrayFindLoopContinuation:
    case kArrayFindLoopEagerDeoptContinuation:
    case kArrayFindLoopLazyDeoptContinuation:
    case kArrayForEach:
    case kArrayForEachLoopContinuation:
    case kArrayForEachLoopEagerDeoptContinuation:
    case kArrayForEachLoopLazyDeoptContinuation:
    case kArrayFrom:
    case kArrayIncludes:
    case kArrayIncludesHoleyDoubles:
    case kArrayIncludesPackedDoubles:
    case kArrayIncludesSmiOrObject:
    case kArrayIndexOf:
    case kArrayIndexOfHoleyDoubles:
    case kArrayIndexOfPackedDoubles:
    case kArrayIndexOfSmiOrObject:
    case kArrayIsArray:
    case kArrayIteratorPrototypeNext:
    case kArrayMap:
    case kArrayMapLoopContinuation:
    case kArrayMapLoopEagerDeoptContinuation:
    case kArrayMapLoopLazyDeoptContinuation:
    case kArrayOf:
    case kArrayPop:
    case kArrayPrototypeEntries:
    case kArrayPrototypeFind:
    case kArrayPrototypeFindIndex:
    case kArrayPrototypeFlatMap:
    case kArrayPrototypeFlatten:
    case kArrayPrototypeKeys:
    case kArrayPrototypePop:
    case kArrayPrototypePush:
    case kArrayPrototypeShift:
    case kArrayPrototypeSlice:
    case kArrayPrototypeValues:
    case kArrayPush:
    case kArrayReduce:
    case kArrayReduceLoopContinuation:
    case kArrayReduceLoopEagerDeoptContinuation:
    case kArrayReduceLoopLazyDeoptContinuation:
    case kArrayReducePreLoopEagerDeoptContinuation:
    case kArrayReduceRight:
    case kArrayReduceRightLoopContinuation:
    case kArrayReduceRightLoopEagerDeoptContinuation:
    case kArrayReduceRightLoopLazyDeoptContinuation:
    case kArrayReduceRightPreLoopEagerDeoptContinuation:
    case kArrayShift:
    case kArraySome:
    case kArraySomeLoopContinuation:
    case kArraySomeLoopEagerDeoptContinuation:
    case kArraySomeLoopLazyDeoptContinuation:
    case kArraySplice:
    case kArraySpliceTorque:
    case kArrayUnshift:
    case kAsyncFromSyncIteratorPrototypeNext:
    case kAsyncFromSyncIteratorPrototypeReturn:
    case kAsyncFromSyncIteratorPrototypeThrow:
    case kAsyncFunctionAwaitCaught:
    case kAsyncFunctionAwaitFulfill:
    case kAsyncFunctionAwaitReject:
    case kAsyncFunctionAwaitUncaught:
    case kAsyncFunctionConstructor:
    case kAsyncFunctionPromiseCreate:
    case kAsyncFunctionPromiseRelease:
    case kAsyncGeneratorAwaitCaught:
    case kAsyncGeneratorAwaitFulfill:
    case kAsyncGeneratorAwaitReject:
    case kAsyncGeneratorAwaitUncaught:
    case kAsyncGeneratorFunctionConstructor:
    case kAsyncGeneratorPrototypeNext:
    case kAsyncGeneratorPrototypeReturn:
    case kAsyncGeneratorPrototypeThrow:
    case kAsyncGeneratorReject:
    case kAsyncGeneratorResolve:
    case kAsyncGeneratorResumeNext:
    case kAsyncGeneratorReturn:
    case kAsyncGeneratorReturnClosedFulfill:
    case kAsyncGeneratorReturnClosedReject:
    case kAsyncGeneratorReturnFulfill:
    case kAsyncGeneratorYield:
    case kAsyncGeneratorYieldFulfill:
    case kAsyncIteratorValueUnwrap:
    case kAtomicsAdd:
    case kAtomicsAnd:
    case kAtomicsCompareExchange:
    case kAtomicsExchange:
    case kAtomicsIsLockFree:
    case kAtomicsLoad:
    case kAtomicsOr:
    case kAtomicsStore:
    case kAtomicsSub:
    case kAtomicsWait:
    case kAtomicsWake:
    case kAtomicsXor:
    case kBigIntAsIntN:
    case kBigIntAsUintN:
    case kBigIntConstructor:
    case kBigIntPrototypeToLocaleString:
    case kBigIntPrototypeToString:
    case kBigIntPrototypeValueOf:
    case kBitwiseAnd:
    case kBitwiseNot:
    case kBitwiseOr:
    case kBitwiseXor:
    case kBooleanConstructor:
    case kBooleanPrototypeToString:
    case kBooleanPrototypeValueOf:
    case kCallForwardVarargs:
    case kCallFunctionForwardVarargs:
    case kCallProxy:
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
    case kCallSitePrototypeIsConstructor:
    case kCallSitePrototypeIsEval:
    case kCallSitePrototypeIsNative:
    case kCallSitePrototypeIsToplevel:
    case kCallSitePrototypeToString:
    case kCallVarargs:
    case kCallWithArrayLike:
    case kCallWithSpread:
    case kCloneFastJSArray:
    case kConsoleAssert:
    case kConsoleClear:
    case kConsoleContext:
    case kConsoleCount:
    case kConsoleDebug:
    case kConsoleDir:
    case kConsoleDirXml:
    case kConsoleError:
    case kConsoleGroup:
    case kConsoleGroupCollapsed:
    case kConsoleGroupEnd:
    case kConsoleInfo:
    case kConsoleLog:
    case kConsoleMarkTimeline:
    case kConsoleProfile:
    case kConsoleProfileEnd:
    case kConsoleTable:
    case kConsoleTime:
    case kConsoleTimeEnd:
    case kConsoleTimeline:
    case kConsoleTimelineEnd:
    case kConsoleTimeStamp:
    case kConsoleTrace:
    case kConsoleWarn:
    case kConstruct:
    case kConstructFunction:
    case kConstructProxy:
    case kConstructVarargs:
    case kConstructWithArrayLike:
    case kConstructWithSpread:
    case kContinueToCodeStubBuiltin:
    case kContinueToCodeStubBuiltinWithResult:
    case kContinueToJavaScriptBuiltin:
    case kContinueToJavaScriptBuiltinWithResult:
    case kCopyFastSmiOrObjectElements:
    case kCreateEmptyArrayLiteral:
    case kCreateGeneratorObject:
    case kCreateIterResultObject:
    case kCreateRegExpLiteral:
    case kCreateShallowArrayLiteral:
    case kCreateShallowObjectLiteral:
    case kCreateTypedArray:
    case kDataViewConstructor:
    case kDataViewPrototypeGetBigInt64:
    case kDataViewPrototypeGetBigUint64:
    case kDataViewPrototypeGetBuffer:
    case kDataViewPrototypeGetByteLength:
    case kDataViewPrototypeGetByteOffset:
    case kDataViewPrototypeGetFloat32:
    case kDataViewPrototypeGetFloat64:
    case kDataViewPrototypeGetInt16:
    case kDataViewPrototypeGetInt32:
    case kDataViewPrototypeGetInt8:
    case kDataViewPrototypeGetUint16:
    case kDataViewPrototypeGetUint32:
    case kDataViewPrototypeGetUint8:
    case kDataViewPrototypeSetBigInt64:
    case kDataViewPrototypeSetBigUint64:
    case kDataViewPrototypeSetFloat32:
    case kDataViewPrototypeSetFloat64:
    case kDataViewPrototypeSetInt16:
    case kDataViewPrototypeSetInt32:
    case kDataViewPrototypeSetInt8:
    case kDataViewPrototypeSetUint16:
    case kDataViewPrototypeSetUint32:
    case kDataViewPrototypeSetUint8:
    case kDateConstructor:
    case kDateNow:
    case kDateParse:
    case kDatePrototypeGetDate:
    case kDatePrototypeGetDay:
    case kDatePrototypeGetFullYear:
    case kDatePrototypeGetHours:
    case kDatePrototypeGetMilliseconds:
    case kDatePrototypeGetMinutes:
    case kDatePrototypeGetMonth:
    case kDatePrototypeGetSeconds:
    case kDatePrototypeGetTime:
    case kDatePrototypeGetTimezoneOffset:
    case kDatePrototypeGetUTCDate:
    case kDatePrototypeGetUTCDay:
    case kDatePrototypeGetUTCFullYear:
    case kDatePrototypeGetUTCHours:
    case kDatePrototypeGetUTCMilliseconds:
    case kDatePrototypeGetUTCMinutes:
    case kDatePrototypeGetUTCMonth:
    case kDatePrototypeGetUTCSeconds:
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
    case kDatePrototypeToPrimitive:
    case kDatePrototypeToString:
    case kDatePrototypeToTimeString:
    case kDatePrototypeToUTCString:
    case kDatePrototypeValueOf:
    case kDateUTC:
    case kDebugBreakTrampoline:
    case kDecrement:
    case kDeleteProperty:
    case kDivide:
    case kDoubleToI:
    case kEmptyFunction:
    case kEnqueueMicrotask:
    case kEqual:
    case kErrorCaptureStackTrace:
    case kErrorConstructor:
    case kErrorPrototypeToString:
    case kExponentiate:
    case kExtractFastJSArray:
    case kFastConsoleAssert:
    case kFastFunctionPrototypeBind:
    case kFastNewClosure:
    case kFastNewFunctionContextEval:
    case kFastNewFunctionContextFunction:
    case kFastNewObject:
    case kFindOrderedHashMapEntry:
    case kFlatMapIntoArray:
    case kFlattenIntoArray:
    case kForInEnumerate:
    case kForInFilter:
    case kFulfillPromise:
    case kFunctionConstructor:
    case kFunctionPrototypeApply:
    case kFunctionPrototypeBind:
    case kFunctionPrototypeCall:
    case kFunctionPrototypeHasInstance:
    case kFunctionPrototypeToString:
    case kGeneratorFunctionConstructor:
    case kGeneratorPrototypeNext:
    case kGeneratorPrototypeReturn:
    case kGeneratorPrototypeThrow:
    case kGetSuperConstructor:
    case kGlobalDecodeURI:
    case kGlobalDecodeURIComponent:
    case kGlobalEncodeURI:
    case kGlobalEncodeURIComponent:
    case kGlobalEscape:
    case kGlobalEval:
    case kGlobalIsFinite:
    case kGlobalIsNaN:
    case kGlobalUnescape:
    case kGreaterThan:
    case kGreaterThanOrEqual:
    case kGrowFastDoubleElements:
    case kGrowFastSmiOrObjectElements:
    case kHandleApiCall:
    case kHandleApiCallAsConstructor:
    case kHandleApiCallAsFunction:
    case kHasProperty:
    case kIllegal:
    case kIncrement:
    case kInstanceOf:
    case kInternalArrayConstructor:
    case kInterpreterEnterBytecodeAdvance:
    case kInterpreterEnterBytecodeDispatch:
    case kInterpreterPushArgsThenCall:
    case kInterpreterPushArgsThenCallWithFinalSpread:
    case kInterpreterPushArgsThenConstruct:
    case kInterpreterPushArgsThenConstructArrayFunction:
    case kInterpreterPushArgsThenConstructWithFinalSpread:
    case kInterpreterPushUndefinedAndArgsThenCall:
    case kInterruptCheck:
    case kIsPromise:
    case kIterableToList:
    case kJSBuiltinsConstructStub:
    case kJSConstructStubGenericRestrictedReturn:
    case kJSConstructStubGenericUnrestrictedReturn:
    case kJsonParse:
    case kJsonStringify:
    case kKeyedLoadIC:
    case kKeyedLoadIC_Megamorphic:
    case kKeyedLoadIC_PolymorphicName:
    case kKeyedLoadIC_Slow:
    case kKeyedLoadICTrampoline:
    case kKeyedStoreIC:
    case kKeyedStoreIC_Megamorphic:
    case kKeyedStoreIC_Slow:
    case kKeyedStoreICTrampoline:
    case kLessThan:
    case kLessThanOrEqual:
    case kLoadGlobalIC:
    case kLoadGlobalICInsideTypeof:
    case kLoadGlobalICInsideTypeofTrampoline:
    case kLoadGlobalIC_Slow:
    case kLoadGlobalICTrampoline:
    case kLoadIC:
    case kLoadIC_FunctionPrototype:
    case kLoadIC_Noninlined:
    case kLoadIC_Slow:
    case kLoadIC_StringLength:
    case kLoadIC_StringWrapperLength:
    case kLoadICTrampoline:
    case kLoadIC_Uninitialized:
    case kMakeError:
    case kMakeRangeError:
    case kMakeSyntaxError:
    case kMakeTypeError:
    case kMakeURIError:
    case kMapConstructor:
    case kMapIteratorPrototypeNext:
    case kMapPrototypeClear:
    case kMapPrototypeDelete:
    case kMapPrototypeEntries:
    case kMapPrototypeForEach:
    case kMapPrototypeGet:
    case kMapPrototypeGetSize:
    case kMapPrototypeHas:
    case kMapPrototypeKeys:
    case kMapPrototypeSet:
    case kMapPrototypeValues:
    case kMathAbs:
    case kMathAcos:
    case kMathAcosh:
    case kMathAsin:
    case kMathAsinh:
    case kMathAtan:
    case kMathAtan2:
    case kMathAtanh:
    case kMathCbrt:
    case kMathCeil:
    case kMathClz32:
    case kMathCos:
    case kMathCosh:
    case kMathExp:
    case kMathExpm1:
    case kMathFloor:
    case kMathFround:
    case kMathHypot:
    case kMathImul:
    case kMathLog:
    case kMathLog10:
    case kMathLog1p:
    case kMathLog2:
    case kMathMax:
    case kMathMin:
    case kMathPow:
    case kMathPowInternal:
    case kMathRandom:
    case kMathRound:
    case kMathSign:
    case kMathSin:
    case kMathSinh:
    case kMathSqrt:
    case kMathTan:
    case kMathTanh:
    case kMathTrunc:
    case kModulus:
    case kMultiply:
    case kNegate:
    case kNewArgumentsElements:
    case kNewPromiseCapability:
    case kNonNumberToNumber:
    case kNonNumberToNumeric:
    case kNonPrimitiveToPrimitive_Default:
    case kNonPrimitiveToPrimitive_Number:
    case kNonPrimitiveToPrimitive_String:
    case kNumberConstructor:
    case kNumberIsFinite:
    case kNumberIsInteger:
    case kNumberIsNaN:
    case kNumberIsSafeInteger:
    case kNumberParseFloat:
    case kNumberParseInt:
    case kNumberPrototypeToExponential:
    case kNumberPrototypeToFixed:
    case kNumberPrototypeToLocaleString:
    case kNumberPrototypeToPrecision:
    case kNumberPrototypeToString:
    case kNumberPrototypeValueOf:
    case kNumberToString:
    case kObjectAssign:
    case kObjectConstructor:
    case kObjectCreate:
    case kObjectDefineGetter:
    case kObjectDefineProperties:
    case kObjectDefineProperty:
    case kObjectDefineSetter:
    case kObjectEntries:
    case kObjectFreeze:
    case kObjectGetOwnPropertyDescriptor:
    case kObjectGetOwnPropertyDescriptors:
    case kObjectGetOwnPropertyNames:
    case kObjectGetOwnPropertySymbols:
    case kObjectGetPrototypeOf:
    case kObjectIs:
    case kObjectIsExtensible:
    case kObjectIsFrozen:
    case kObjectIsSealed:
    case kObjectKeys:
    case kObjectLookupGetter:
    case kObjectLookupSetter:
    case kObjectPreventExtensions:
    case kObjectPrototypeGetProto:
    case kObjectPrototypeHasOwnProperty:
    case kObjectPrototypeIsPrototypeOf:
    case kObjectPrototypePropertyIsEnumerable:
    case kObjectPrototypeSetProto:
    case kObjectPrototypeToLocaleString:
    case kObjectPrototypeToString:
    case kObjectPrototypeValueOf:
    case kObjectSeal:
    case kObjectSetPrototypeOf:
    case kObjectValues:
    case kOrderedHashTableHealIndex:
    case kOrdinaryHasInstance:
    case kOrdinaryToPrimitive_Number:
    case kOrdinaryToPrimitive_String:
    case kPerformPromiseThen:
    case kPromiseAll:
    case kPromiseAllResolveElementClosure:
    case kPromiseCapabilityDefaultReject:
    case kPromiseCapabilityDefaultResolve:
    case kPromiseCatchFinally:
    case kPromiseConstructor:
    case kPromiseConstructorLazyDeoptContinuation:
    case kPromiseFulfillReactionJob:
    case kPromiseGetCapabilitiesExecutor:
    case kPromiseInternalConstructor:
    case kPromiseInternalReject:
    case kPromiseInternalResolve:
    case kPromisePrototypeCatch:
    case kPromisePrototypeFinally:
    case kPromisePrototypeThen:
    case kPromiseRace:
    case kPromiseReject:
    case kPromiseRejectReactionJob:
    case kPromiseResolve:
    case kPromiseResolveThenableJob:
    case kPromiseResolveTrampoline:
    case kPromiseThenFinally:
    case kPromiseThrowerFinally:
    case kPromiseValueThunkFinally:
    case kProxyConstructor:
    case kProxyGetProperty:
    case kProxyHasProperty:
    case kProxyRevocable:
    case kProxyRevoke:
    case kProxySetProperty:
    case kRecordWrite:
    case kReflectApply:
    case kReflectConstruct:
    case kReflectDefineProperty:
    case kReflectDeleteProperty:
    case kReflectGet:
    case kReflectGetOwnPropertyDescriptor:
    case kReflectGetPrototypeOf:
    case kReflectHas:
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
    case kRegExpConstructor:
    case kRegExpExecAtom:
    case kRegExpInputGetter:
    case kRegExpInputSetter:
    case kRegExpInternalMatch:
    case kRegExpLastMatchGetter:
    case kRegExpLastParenGetter:
    case kRegExpLeftContextGetter:
    case kRegExpMatchFast:
    case kRegExpPrototypeCompile:
    case kRegExpPrototypeDotAllGetter:
    case kRegExpPrototypeExec:
    case kRegExpPrototypeExecSlow:
    case kRegExpPrototypeFlagsGetter:
    case kRegExpPrototypeGlobalGetter:
    case kRegExpPrototypeIgnoreCaseGetter:
    case kRegExpPrototypeMatch:
    case kRegExpPrototypeMatchAll:
    case kRegExpPrototypeMultilineGetter:
    case kRegExpPrototypeReplace:
    case kRegExpPrototypeSearch:
    case kRegExpPrototypeSourceGetter:
    case kRegExpPrototypeSplit:
    case kRegExpPrototypeStickyGetter:
    case kRegExpPrototypeTest:
    case kRegExpPrototypeToString:
    case kRegExpPrototypeUnicodeGetter:
    case kRegExpReplace:
    case kRegExpRightContextGetter:
    case kRegExpSearchFast:
    case kRegExpSplit:
    case kRegExpStringIteratorPrototypeNext:
    case kRejectPromise:
    case kResolvePromise:
    case kReturnReceiver:
    case kRunMicrotasks:
    case kSameValue:
    case kSetConstructor:
    case kSetIteratorPrototypeNext:
    case kSetPrototypeAdd:
    case kSetPrototypeClear:
    case kSetPrototypeDelete:
    case kSetPrototypeEntries:
    case kSetPrototypeForEach:
    case kSetPrototypeGetSize:
    case kSetPrototypeHas:
    case kSetPrototypeValues:
    case kSharedArrayBufferPrototypeGetByteLength:
    case kSharedArrayBufferPrototypeSlice:
    case kShiftLeft:
    case kShiftRight:
    case kShiftRightLogical:
    case kStackCheck:
    case kStoreGlobalIC:
    case kStoreGlobalIC_Slow:
    case kStoreGlobalICTrampoline:
    case kStoreIC:
    case kStoreICTrampoline:
    case kStoreIC_Uninitialized:
    case kStoreInArrayLiteralIC:
    case kStoreInArrayLiteralIC_Slow:
    case kStrictEqual:
    case kStrictPoisonPillThrower:
    case kStringCharAt:
    case kStringCodePointAtUTF16:
    case kStringCodePointAtUTF32:
    case kStringConstructor:
    case kStringEqual:
    case kStringFromCharCode:
    case kStringFromCodePoint:
    case kStringGreaterThan:
    case kStringGreaterThanOrEqual:
    case kStringIndexOf:
    case kStringIteratorPrototypeNext:
    case kStringLessThan:
    case kStringLessThanOrEqual:
    case kStringPrototypeAnchor:
    case kStringPrototypeBig:
    case kStringPrototypeBlink:
    case kStringPrototypeBold:
    case kStringPrototypeCharAt:
    case kStringPrototypeCharCodeAt:
    case kStringPrototypeCodePointAt:
    case kStringPrototypeConcat:
    case kStringPrototypeEndsWith:
    case kStringPrototypeFixed:
    case kStringPrototypeFontcolor:
    case kStringPrototypeFontsize:
    case kStringPrototypeIncludes:
    case kStringPrototypeIndexOf:
    case kStringPrototypeItalics:
    case kStringPrototypeIterator:
    case kStringPrototypeLastIndexOf:
    case kStringPrototypeLink:
    case kStringPrototypeLocaleCompare:
    case kStringPrototypeMatch:
    case kStringPrototypeMatchAll:
    case kStringPrototypePadEnd:
    case kStringPrototypePadStart:
    case kStringPrototypeRepeat:
    case kStringPrototypeReplace:
    case kStringPrototypeSearch:
    case kStringPrototypeSlice:
    case kStringPrototypeSmall:
    case kStringPrototypeSplit:
    case kStringPrototypeStartsWith:
    case kStringPrototypeStrike:
    case kStringPrototypeSub:
    case kStringPrototypeSubstr:
    case kStringPrototypeSubstring:
    case kStringPrototypeSup:
    case kStringPrototypeToString:
    case kStringPrototypeTrim:
    case kStringPrototypeTrimEnd:
    case kStringPrototypeTrimStart:
    case kStringPrototypeValueOf:
    case kStringRaw:
    case kStringRepeat:
    case kStringSubstring:
    case kStringToNumber:
    case kSubtract:
    case kSymbolConstructor:
    case kSymbolFor:
    case kSymbolKeyFor:
    case kSymbolPrototypeToPrimitive:
    case kSymbolPrototypeToString:
    case kSymbolPrototypeValueOf:
    case kThrowWasmTrapDivByZero:
    case kThrowWasmTrapDivUnrepresentable:
    case kThrowWasmTrapFloatUnrepresentable:
    case kThrowWasmTrapFuncInvalid:
    case kThrowWasmTrapFuncSigMismatch:
    case kThrowWasmTrapMemOutOfBounds:
    case kThrowWasmTrapRemByZero:
    case kThrowWasmTrapUnreachable:
    case kToBoolean:
    case kToBooleanLazyDeoptContinuation:
    case kToInteger:
    case kToInteger_TruncateMinusZero:
    case kToLength:
    case kToName:
    case kToNumber:
    case kToNumeric:
    case kToObject:
    case kToString:
    case kTypedArrayBaseConstructor:
    case kTypedArrayConstructor:
    case kTypedArrayConstructorLazyDeoptContinuation:
    case kTypedArrayFrom:
    case kTypedArrayInitialize:
    case kTypedArrayInitializeWithBuffer:
    case kTypedArrayOf:
    case kTypedArrayPrototypeBuffer:
    case kTypedArrayPrototypeByteLength:
    case kTypedArrayPrototypeByteOffset:
    case kTypedArrayPrototypeCopyWithin:
    case kTypedArrayPrototypeEntries:
    case kTypedArrayPrototypeEvery:
    case kTypedArrayPrototypeFill:
    case kTypedArrayPrototypeFilter:
    case kTypedArrayPrototypeFind:
    case kTypedArrayPrototypeFindIndex:
    case kTypedArrayPrototypeForEach:
    case kTypedArrayPrototypeIncludes:
    case kTypedArrayPrototypeIndexOf:
    case kTypedArrayPrototypeKeys:
    case kTypedArrayPrototypeLastIndexOf:
    case kTypedArrayPrototypeLength:
    case kTypedArrayPrototypeMap:
    case kTypedArrayPrototypeReduce:
    case kTypedArrayPrototypeReduceRight:
    case kTypedArrayPrototypeReverse:
    case kTypedArrayPrototypeSet:
    case kTypedArrayPrototypeSlice:
    case kTypedArrayPrototypeSome:
    case kTypedArrayPrototypeSubArray:
    case kTypedArrayPrototypeToStringTag:
    case kTypedArrayPrototypeValues:
    case kTypeof:
    case kUnsupportedThrower:
    case kWasmStackGuard:
    case kWeakCollectionDelete:
    case kWeakCollectionSet:
    case kWeakMapConstructor:
    case kWeakMapGet:
    case kWeakMapHas:
    case kWeakMapLookupHashIndex:
    case kWeakMapPrototypeDelete:
    case kWeakMapPrototypeSet:
    case kWeakSetConstructor:
    case kWeakSetHas:
    case kWeakSetPrototypeAdd:
    case kWeakSetPrototypeDelete:
#ifdef V8_INTL_SUPPORT
    case kNumberFormatPrototypeFormatToParts:
    case kStringPrototypeNormalizeIntl:
    case kStringPrototypeToLowerCaseIntl:
    case kStringPrototypeToUpperCaseIntl:
    case kStringToLowerCaseIntl:
#endif
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

#ifdef V8_EMBEDDED_BUILTINS
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
#endif  // V8_EMBEDDED_BUILTINS

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

Handle<Code> Builtins::JSConstructStubGeneric() {
  return FLAG_harmony_restrict_constructor_return
             ? builtin_handle(kJSConstructStubGenericRestrictedReturn)
             : builtin_handle(kJSConstructStubGenericUnrestrictedReturn);
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
