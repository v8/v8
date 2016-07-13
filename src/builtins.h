
// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_H_
#define V8_BUILTINS_H_

#include "src/base/flags.h"
#include "src/handles.h"

namespace v8 {
namespace internal {

// Forward declarations.
class CodeStubAssembler;

#define CODE_AGE_LIST_WITH_ARG(V, A)     \
  V(Quadragenarian, A)                   \
  V(Quinquagenarian, A)                  \
  V(Sexagenarian, A)                     \
  V(Septuagenarian, A)                   \
  V(Octogenarian, A)

#define CODE_AGE_LIST_IGNORE_ARG(X, V) V(X)

#define CODE_AGE_LIST(V) \
  CODE_AGE_LIST_WITH_ARG(CODE_AGE_LIST_IGNORE_ARG, V)

#define CODE_AGE_LIST_COMPLETE(V)                  \
  V(ToBeExecutedOnce)                              \
  V(NotExecuted)                                   \
  V(ExecutedOnce)                                  \
  V(NoAge)                                         \
  CODE_AGE_LIST_WITH_ARG(CODE_AGE_LIST_IGNORE_ARG, V)

#define DECLARE_CODE_AGE_BUILTIN(C, V)                           \
  V(Make##C##CodeYoungAgainOddMarking, BUILTIN, kNoExtraICState) \
  V(Make##C##CodeYoungAgainEvenMarking, BUILTIN, kNoExtraICState)

// Define list of builtins implemented in C++.
#define BUILTIN_LIST_C(V)                                  \
  V(Illegal, BUILTIN_EXIT)                                 \
                                                           \
  V(EmptyFunction, BUILTIN_EXIT)                           \
                                                           \
  V(ArrayConcat, BUILTIN_EXIT)                             \
  V(ArrayPop, BUILTIN_EXIT)                                \
  V(ArrayPush, BUILTIN_EXIT)                               \
  V(ArrayShift, BUILTIN_EXIT)                              \
  V(ArraySlice, BUILTIN_EXIT)                              \
  V(ArraySplice, BUILTIN_EXIT)                             \
  V(ArrayUnshift, BUILTIN_EXIT)                            \
                                                           \
  V(ArrayBufferConstructor, BUILTIN_EXIT)                  \
  V(ArrayBufferConstructor_ConstructStub, BUILTIN_EXIT)    \
  V(ArrayBufferPrototypeGetByteLength, BUILTIN_EXIT)       \
  V(ArrayBufferIsView, BUILTIN_EXIT)                       \
                                                           \
  V(BooleanConstructor, BUILTIN_EXIT)                      \
  V(BooleanConstructor_ConstructStub, BUILTIN_EXIT)        \
                                                           \
  V(DataViewConstructor, BUILTIN_EXIT)                     \
  V(DataViewConstructor_ConstructStub, BUILTIN_EXIT)       \
  V(DataViewPrototypeGetBuffer, BUILTIN_EXIT)              \
  V(DataViewPrototypeGetByteLength, BUILTIN_EXIT)          \
  V(DataViewPrototypeGetByteOffset, BUILTIN_EXIT)          \
                                                           \
  V(DateConstructor, BUILTIN_EXIT)                         \
  V(DateConstructor_ConstructStub, BUILTIN_EXIT)           \
  V(DateNow, BUILTIN_EXIT)                                 \
  V(DateParse, BUILTIN_EXIT)                               \
  V(DateUTC, BUILTIN_EXIT)                                 \
  V(DatePrototypeSetDate, BUILTIN_EXIT)                    \
  V(DatePrototypeSetFullYear, BUILTIN_EXIT)                \
  V(DatePrototypeSetHours, BUILTIN_EXIT)                   \
  V(DatePrototypeSetMilliseconds, BUILTIN_EXIT)            \
  V(DatePrototypeSetMinutes, BUILTIN_EXIT)                 \
  V(DatePrototypeSetMonth, BUILTIN_EXIT)                   \
  V(DatePrototypeSetSeconds, BUILTIN_EXIT)                 \
  V(DatePrototypeSetTime, BUILTIN_EXIT)                    \
  V(DatePrototypeSetUTCDate, BUILTIN_EXIT)                 \
  V(DatePrototypeSetUTCFullYear, BUILTIN_EXIT)             \
  V(DatePrototypeSetUTCHours, BUILTIN_EXIT)                \
  V(DatePrototypeSetUTCMilliseconds, BUILTIN_EXIT)         \
  V(DatePrototypeSetUTCMinutes, BUILTIN_EXIT)              \
  V(DatePrototypeSetUTCMonth, BUILTIN_EXIT)                \
  V(DatePrototypeSetUTCSeconds, BUILTIN_EXIT)              \
  V(DatePrototypeToDateString, BUILTIN_EXIT)               \
  V(DatePrototypeToISOString, BUILTIN_EXIT)                \
  V(DatePrototypeToPrimitive, BUILTIN_EXIT)                \
  V(DatePrototypeToUTCString, BUILTIN_EXIT)                \
  V(DatePrototypeToString, BUILTIN_EXIT)                   \
  V(DatePrototypeToTimeString, BUILTIN_EXIT)               \
  V(DatePrototypeValueOf, BUILTIN_EXIT)                    \
  V(DatePrototypeGetYear, BUILTIN_EXIT)                    \
  V(DatePrototypeSetYear, BUILTIN_EXIT)                    \
  V(DatePrototypeToJson, BUILTIN_EXIT)                     \
                                                           \
  V(FunctionConstructor, BUILTIN_EXIT)                     \
  V(FunctionPrototypeBind, BUILTIN_EXIT)                   \
  V(FunctionPrototypeToString, BUILTIN_EXIT)               \
                                                           \
  V(GeneratorFunctionConstructor, BUILTIN_EXIT)            \
  V(AsyncFunctionConstructor, BUILTIN_EXIT)                \
                                                           \
  V(GlobalDecodeURI, BUILTIN_EXIT)                         \
  V(GlobalDecodeURIComponent, BUILTIN_EXIT)                \
  V(GlobalEncodeURI, BUILTIN_EXIT)                         \
  V(GlobalEncodeURIComponent, BUILTIN_EXIT)                \
  V(GlobalEscape, BUILTIN_EXIT)                            \
  V(GlobalUnescape, BUILTIN_EXIT)                          \
                                                           \
  V(GlobalEval, BUILTIN_EXIT)                              \
                                                           \
  V(JsonParse, BUILTIN_EXIT)                               \
  V(JsonStringify, BUILTIN_EXIT)                           \
                                                           \
  V(MathHypot, BUILTIN_EXIT)                               \
                                                           \
  V(NumberPrototypeToExponential, BUILTIN_EXIT)            \
  V(NumberPrototypeToFixed, BUILTIN_EXIT)                  \
  V(NumberPrototypeToLocaleString, BUILTIN_EXIT)           \
  V(NumberPrototypeToPrecision, BUILTIN_EXIT)              \
  V(NumberPrototypeToString, BUILTIN_EXIT)                 \
                                                           \
  V(ObjectAssign, BUILTIN_EXIT)                            \
  V(ObjectCreate, BUILTIN_EXIT)                            \
  V(ObjectDefineGetter, BUILTIN_EXIT)                      \
  V(ObjectDefineProperties, BUILTIN_EXIT)                  \
  V(ObjectDefineProperty, BUILTIN_EXIT)                    \
  V(ObjectDefineSetter, BUILTIN_EXIT)                      \
  V(ObjectEntries, BUILTIN_EXIT)                           \
  V(ObjectFreeze, BUILTIN_EXIT)                            \
  V(ObjectGetOwnPropertyDescriptor, BUILTIN_EXIT)          \
  V(ObjectGetOwnPropertyDescriptors, BUILTIN_EXIT)         \
  V(ObjectGetOwnPropertyNames, BUILTIN_EXIT)               \
  V(ObjectGetOwnPropertySymbols, BUILTIN_EXIT)             \
  V(ObjectGetPrototypeOf, BUILTIN_EXIT)                    \
  V(ObjectIs, BUILTIN_EXIT)                                \
  V(ObjectIsExtensible, BUILTIN_EXIT)                      \
  V(ObjectIsFrozen, BUILTIN_EXIT)                          \
  V(ObjectIsSealed, BUILTIN_EXIT)                          \
  V(ObjectKeys, BUILTIN_EXIT)                              \
  V(ObjectLookupGetter, BUILTIN_EXIT)                      \
  V(ObjectLookupSetter, BUILTIN_EXIT)                      \
  V(ObjectPreventExtensions, BUILTIN_EXIT)                 \
  V(ObjectPrototypePropertyIsEnumerable, BUILTIN_EXIT)     \
  V(ObjectProtoToString, BUILTIN_EXIT)                     \
  V(ObjectSeal, BUILTIN_EXIT)                              \
  V(ObjectValues, BUILTIN_EXIT)                            \
                                                           \
  V(ProxyConstructor, BUILTIN_EXIT)                        \
  V(ProxyConstructor_ConstructStub, BUILTIN_EXIT)          \
                                                           \
  V(ReflectDefineProperty, BUILTIN_EXIT)                   \
  V(ReflectDeleteProperty, BUILTIN_EXIT)                   \
  V(ReflectGet, BUILTIN_EXIT)                              \
  V(ReflectGetOwnPropertyDescriptor, BUILTIN_EXIT)         \
  V(ReflectGetPrototypeOf, BUILTIN_EXIT)                   \
  V(ReflectHas, BUILTIN_EXIT)                              \
  V(ReflectIsExtensible, BUILTIN_EXIT)                     \
  V(ReflectOwnKeys, BUILTIN_EXIT)                          \
  V(ReflectPreventExtensions, BUILTIN_EXIT)                \
  V(ReflectSet, BUILTIN_EXIT)                              \
  V(ReflectSetPrototypeOf, BUILTIN_EXIT)                   \
                                                           \
  V(SharedArrayBufferPrototypeGetByteLength, BUILTIN_EXIT) \
                                                           \
  V(StringFromCodePoint, BUILTIN_EXIT)                     \
                                                           \
  V(StringPrototypeTrim, BUILTIN_EXIT)                     \
  V(StringPrototypeTrimLeft, BUILTIN_EXIT)                 \
  V(StringPrototypeTrimRight, BUILTIN_EXIT)                \
                                                           \
  V(SymbolConstructor, BUILTIN_EXIT)                       \
  V(SymbolConstructor_ConstructStub, BUILTIN_EXIT)         \
                                                           \
  V(TypedArrayPrototypeBuffer, BUILTIN_EXIT)               \
                                                           \
  V(HandleApiCall, EXIT)                                   \
  V(HandleApiCallAsFunction, EXIT)                         \
  V(HandleApiCallAsConstructor, EXIT)                      \
                                                           \
  V(RestrictedFunctionPropertiesThrower, BUILTIN_EXIT)     \
  V(RestrictedStrictArgumentsPropertiesThrower, BUILTIN_EXIT)

// Define list of builtins implemented in assembly.
#define BUILTIN_LIST_A(V)                                                    \
  V(AllocateInNewSpace, BUILTIN, kNoExtraICState)                            \
  V(AllocateInOldSpace, BUILTIN, kNoExtraICState)                            \
                                                                             \
  V(ArgumentsAdaptorTrampoline, BUILTIN, kNoExtraICState)                    \
                                                                             \
  V(ConstructedNonConstructable, BUILTIN, kNoExtraICState)                   \
                                                                             \
  V(CallFunction_ReceiverIsNullOrUndefined, BUILTIN, kNoExtraICState)        \
  V(CallFunction_ReceiverIsNotNullOrUndefined, BUILTIN, kNoExtraICState)     \
  V(CallFunction_ReceiverIsAny, BUILTIN, kNoExtraICState)                    \
  V(TailCallFunction_ReceiverIsNullOrUndefined, BUILTIN, kNoExtraICState)    \
  V(TailCallFunction_ReceiverIsNotNullOrUndefined, BUILTIN, kNoExtraICState) \
  V(TailCallFunction_ReceiverIsAny, BUILTIN, kNoExtraICState)                \
  V(CallBoundFunction, BUILTIN, kNoExtraICState)                             \
  V(TailCallBoundFunction, BUILTIN, kNoExtraICState)                         \
  V(Call_ReceiverIsNullOrUndefined, BUILTIN, kNoExtraICState)                \
  V(Call_ReceiverIsNotNullOrUndefined, BUILTIN, kNoExtraICState)             \
  V(Call_ReceiverIsAny, BUILTIN, kNoExtraICState)                            \
  V(TailCall_ReceiverIsNullOrUndefined, BUILTIN, kNoExtraICState)            \
  V(TailCall_ReceiverIsNotNullOrUndefined, BUILTIN, kNoExtraICState)         \
  V(TailCall_ReceiverIsAny, BUILTIN, kNoExtraICState)                        \
                                                                             \
  V(ConstructFunction, BUILTIN, kNoExtraICState)                             \
  V(ConstructBoundFunction, BUILTIN, kNoExtraICState)                        \
  V(ConstructProxy, BUILTIN, kNoExtraICState)                                \
  V(Construct, BUILTIN, kNoExtraICState)                                     \
                                                                             \
  V(StringToNumber, BUILTIN, kNoExtraICState)                                \
  V(NonNumberToNumber, BUILTIN, kNoExtraICState)                             \
  V(ToNumber, BUILTIN, kNoExtraICState)                                      \
                                                                             \
  V(Apply, BUILTIN, kNoExtraICState)                                         \
                                                                             \
  V(HandleFastApiCall, BUILTIN, kNoExtraICState)                             \
                                                                             \
  V(InOptimizationQueue, BUILTIN, kNoExtraICState)                           \
  V(JSConstructStubGeneric, BUILTIN, kNoExtraICState)                        \
  V(JSBuiltinsConstructStub, BUILTIN, kNoExtraICState)                       \
  V(JSBuiltinsConstructStubForDerived, BUILTIN, kNoExtraICState)             \
  V(JSConstructStubApi, BUILTIN, kNoExtraICState)                            \
  V(JSEntryTrampoline, BUILTIN, kNoExtraICState)                             \
  V(JSConstructEntryTrampoline, BUILTIN, kNoExtraICState)                    \
  V(ResumeGeneratorTrampoline, BUILTIN, kNoExtraICState)                     \
  V(InstantiateAsmJs, BUILTIN, kNoExtraICState)                              \
  V(CompileLazy, BUILTIN, kNoExtraICState)                                   \
  V(CompileBaseline, BUILTIN, kNoExtraICState)                               \
  V(CompileOptimized, BUILTIN, kNoExtraICState)                              \
  V(CompileOptimizedConcurrent, BUILTIN, kNoExtraICState)                    \
  V(NotifyDeoptimized, BUILTIN, kNoExtraICState)                             \
  V(NotifySoftDeoptimized, BUILTIN, kNoExtraICState)                         \
  V(NotifyLazyDeoptimized, BUILTIN, kNoExtraICState)                         \
  V(NotifyStubFailure, BUILTIN, kNoExtraICState)                             \
  V(NotifyStubFailureSaveDoubles, BUILTIN, kNoExtraICState)                  \
                                                                             \
  V(InterpreterEntryTrampoline, BUILTIN, kNoExtraICState)                    \
  V(InterpreterMarkBaselineOnReturn, BUILTIN, kNoExtraICState)               \
  V(InterpreterPushArgsAndCallFunction, BUILTIN, kNoExtraICState)            \
  V(InterpreterPushArgsAndTailCallFunction, BUILTIN, kNoExtraICState)        \
  V(InterpreterPushArgsAndCall, BUILTIN, kNoExtraICState)                    \
  V(InterpreterPushArgsAndTailCall, BUILTIN, kNoExtraICState)                \
  V(InterpreterPushArgsAndConstruct, BUILTIN, kNoExtraICState)               \
  V(InterpreterEnterBytecodeDispatch, BUILTIN, kNoExtraICState)              \
                                                                             \
  V(KeyedLoadIC_Miss, BUILTIN, kNoExtraICState)                              \
  V(StoreIC_Miss, BUILTIN, kNoExtraICState)                                  \
  V(KeyedStoreIC_Miss, BUILTIN, kNoExtraICState)                             \
  V(LoadIC_Getter_ForDeopt, LOAD_IC, kNoExtraICState)                        \
  V(KeyedLoadIC_Megamorphic, KEYED_LOAD_IC, kNoExtraICState)                 \
                                                                             \
  V(StoreIC_Setter_ForDeopt, STORE_IC, StoreICState::kStrictModeState)       \
                                                                             \
  V(KeyedStoreIC_Megamorphic, KEYED_STORE_IC, kNoExtraICState)               \
  V(KeyedStoreIC_Megamorphic_Strict, KEYED_STORE_IC,                         \
    StoreICState::kStrictModeState)                                          \
                                                                             \
  V(DatePrototypeGetDate, BUILTIN, kNoExtraICState)                          \
  V(DatePrototypeGetDay, BUILTIN, kNoExtraICState)                           \
  V(DatePrototypeGetFullYear, BUILTIN, kNoExtraICState)                      \
  V(DatePrototypeGetHours, BUILTIN, kNoExtraICState)                         \
  V(DatePrototypeGetMilliseconds, BUILTIN, kNoExtraICState)                  \
  V(DatePrototypeGetMinutes, BUILTIN, kNoExtraICState)                       \
  V(DatePrototypeGetMonth, BUILTIN, kNoExtraICState)                         \
  V(DatePrototypeGetSeconds, BUILTIN, kNoExtraICState)                       \
  V(DatePrototypeGetTime, BUILTIN, kNoExtraICState)                          \
  V(DatePrototypeGetTimezoneOffset, BUILTIN, kNoExtraICState)                \
  V(DatePrototypeGetUTCDate, BUILTIN, kNoExtraICState)                       \
  V(DatePrototypeGetUTCDay, BUILTIN, kNoExtraICState)                        \
  V(DatePrototypeGetUTCFullYear, BUILTIN, kNoExtraICState)                   \
  V(DatePrototypeGetUTCHours, BUILTIN, kNoExtraICState)                      \
  V(DatePrototypeGetUTCMilliseconds, BUILTIN, kNoExtraICState)               \
  V(DatePrototypeGetUTCMinutes, BUILTIN, kNoExtraICState)                    \
  V(DatePrototypeGetUTCMonth, BUILTIN, kNoExtraICState)                      \
  V(DatePrototypeGetUTCSeconds, BUILTIN, kNoExtraICState)                    \
                                                                             \
  V(FunctionPrototypeApply, BUILTIN, kNoExtraICState)                        \
  V(FunctionPrototypeCall, BUILTIN, kNoExtraICState)                         \
                                                                             \
  V(ReflectApply, BUILTIN, kNoExtraICState)                                  \
  V(ReflectConstruct, BUILTIN, kNoExtraICState)                              \
                                                                             \
  V(InternalArrayCode, BUILTIN, kNoExtraICState)                             \
  V(ArrayCode, BUILTIN, kNoExtraICState)                                     \
                                                                             \
  V(MathMax, BUILTIN, kNoExtraICState)                                       \
  V(MathMin, BUILTIN, kNoExtraICState)                                       \
                                                                             \
  V(NumberConstructor, BUILTIN, kNoExtraICState)                             \
  V(NumberConstructor_ConstructStub, BUILTIN, kNoExtraICState)               \
                                                                             \
  V(StringConstructor, BUILTIN, kNoExtraICState)                             \
  V(StringConstructor_ConstructStub, BUILTIN, kNoExtraICState)               \
                                                                             \
  V(OnStackReplacement, BUILTIN, kNoExtraICState)                            \
  V(InterruptCheck, BUILTIN, kNoExtraICState)                                \
  V(StackCheck, BUILTIN, kNoExtraICState)                                    \
                                                                             \
  V(MarkCodeAsToBeExecutedOnce, BUILTIN, kNoExtraICState)                    \
  V(MarkCodeAsExecutedOnce, BUILTIN, kNoExtraICState)                        \
  V(MarkCodeAsExecutedTwice, BUILTIN, kNoExtraICState)                       \
  CODE_AGE_LIST_WITH_ARG(DECLARE_CODE_AGE_BUILTIN, V)

// Define list of builtins implemented in TurboFan (with JS linkage).
#define BUILTIN_LIST_T(V)             \
  V(BooleanPrototypeToString, 1)      \
  V(BooleanPrototypeValueOf, 1)       \
  V(FunctionPrototypeHasInstance, 2)  \
  V(GeneratorPrototypeNext, 2)        \
  V(GeneratorPrototypeReturn, 2)      \
  V(GeneratorPrototypeThrow, 2)       \
  V(MathAcos, 2)                      \
  V(MathAcosh, 2)                     \
  V(MathAsin, 2)                      \
  V(MathAsinh, 2)                     \
  V(MathAtan, 2)                      \
  V(MathAtanh, 2)                     \
  V(MathAtan2, 3)                     \
  V(MathCeil, 2)                      \
  V(MathCbrt, 2)                      \
  V(MathAbs, 2)                       \
  V(MathExpm1, 2)                     \
  V(MathClz32, 2)                     \
  V(MathCos, 2)                       \
  V(MathCosh, 2)                      \
  V(MathExp, 2)                       \
  V(MathFloor, 2)                     \
  V(MathFround, 2)                    \
  V(MathImul, 3)                      \
  V(MathLog, 2)                       \
  V(MathLog1p, 2)                     \
  V(MathLog10, 2)                     \
  V(MathLog2, 2)                      \
  V(MathRound, 2)                     \
  V(MathPow, 3)                       \
  V(MathSign, 2)                      \
  V(MathSin, 2)                       \
  V(MathSinh, 2)                      \
  V(MathTan, 2)                       \
  V(MathTanh, 2)                      \
  V(MathSqrt, 2)                      \
  V(MathTrunc, 2)                     \
  V(NumberPrototypeValueOf, 1)        \
  V(ObjectHasOwnProperty, 2)          \
  V(ArrayIsArray, 2)                  \
  V(StringFromCharCode, 2)            \
  V(StringPrototypeCharAt, 2)         \
  V(StringPrototypeCharCodeAt, 2)     \
  V(StringPrototypeToString, 1)       \
  V(StringPrototypeValueOf, 1)        \
  V(SymbolPrototypeToPrimitive, 2)    \
  V(SymbolPrototypeToString, 1)       \
  V(SymbolPrototypeValueOf, 1)        \
  V(TypedArrayPrototypeByteLength, 1) \
  V(TypedArrayPrototypeByteOffset, 1) \
  V(TypedArrayPrototypeLength, 1)     \
  V(AtomicsLoad, 3)                   \
  V(AtomicsStore, 4)

// Define list of builtins implemented in TurboFan (with CallStub linkage).
#define BUILTIN_LIST_S(V)                                              \
  V(LoadGlobalIC_Miss, BUILTIN, kNoExtraICState, LoadGlobalWithVector) \
  V(LoadGlobalIC_SlowNotInsideTypeof, HANDLER, Code::LOAD_GLOBAL_IC,   \
    LoadGlobalWithVector)                                              \
  V(LoadGlobalIC_SlowInsideTypeof, HANDLER, Code::LOAD_GLOBAL_IC,      \
    LoadGlobalWithVector)                                              \
  V(LoadIC_Miss, BUILTIN, kNoExtraICState, LoadWithVector)             \
  V(LoadIC_Slow, HANDLER, Code::LOAD_IC, LoadWithVector)

// Define list of builtin handlers implemented in assembly.
#define BUILTIN_LIST_H(V)                    \
  V(KeyedLoadIC_Slow,        KEYED_LOAD_IC)  \
  V(StoreIC_Slow,            STORE_IC)       \
  V(KeyedStoreIC_Slow,       KEYED_STORE_IC) \
  V(LoadIC_Normal,           LOAD_IC)        \
  V(StoreIC_Normal,          STORE_IC)

// Define list of builtins used by the debugger implemented in assembly.
#define BUILTIN_LIST_DEBUG_A(V)                  \
  V(Return_DebugBreak, BUILTIN, kNoExtraICState) \
  V(Slot_DebugBreak, BUILTIN, kNoExtraICState)   \
  V(FrameDropper_LiveEdit, BUILTIN, kNoExtraICState)

class BuiltinFunctionTable;
class ObjectVisitor;


class Builtins {
 public:
  ~Builtins();

  // Generate all builtin code objects. Should be called once during
  // isolate initialization.
  void SetUp(Isolate* isolate, bool create_heap_objects);
  void TearDown();

  // Garbage collection support.
  void IterateBuiltins(ObjectVisitor* v);

  // Disassembler support.
  const char* Lookup(byte* pc);

  enum Name {
#define DEF_ENUM_C(name, ignore) k##name,
#define DEF_ENUM_A(name, kind, extra) k##name,
#define DEF_ENUM_T(name, argc) k##name,
#define DEF_ENUM_S(name, kind, extra, interface_descriptor) k##name,
#define DEF_ENUM_H(name, kind) k##name,
    BUILTIN_LIST_C(DEF_ENUM_C) BUILTIN_LIST_A(DEF_ENUM_A)
        BUILTIN_LIST_T(DEF_ENUM_T) BUILTIN_LIST_S(DEF_ENUM_S)
            BUILTIN_LIST_H(DEF_ENUM_H) BUILTIN_LIST_DEBUG_A(DEF_ENUM_A)
#undef DEF_ENUM_C
#undef DEF_ENUM_A
#undef DEF_ENUM_T
#undef DEF_ENUM_S
#undef DEF_ENUM_H
                builtin_count
  };

  enum CFunctionId {
#define DEF_ENUM_C(name, ignore) c_##name,
    BUILTIN_LIST_C(DEF_ENUM_C)
#undef DEF_ENUM_C
        cfunction_count
  };

#define DECLARE_BUILTIN_ACCESSOR_C(name, ignore) Handle<Code> name();
#define DECLARE_BUILTIN_ACCESSOR_A(name, kind, extra) Handle<Code> name();
#define DECLARE_BUILTIN_ACCESSOR_T(name, argc) Handle<Code> name();
#define DECLARE_BUILTIN_ACCESSOR_S(name, kind, extra, interface_descriptor) \
  Handle<Code> name();
#define DECLARE_BUILTIN_ACCESSOR_H(name, kind) Handle<Code> name();
  BUILTIN_LIST_C(DECLARE_BUILTIN_ACCESSOR_C)
  BUILTIN_LIST_A(DECLARE_BUILTIN_ACCESSOR_A)
  BUILTIN_LIST_T(DECLARE_BUILTIN_ACCESSOR_T)
  BUILTIN_LIST_S(DECLARE_BUILTIN_ACCESSOR_S)
  BUILTIN_LIST_H(DECLARE_BUILTIN_ACCESSOR_H)
  BUILTIN_LIST_DEBUG_A(DECLARE_BUILTIN_ACCESSOR_A)
#undef DECLARE_BUILTIN_ACCESSOR_C
#undef DECLARE_BUILTIN_ACCESSOR_A
#undef DECLARE_BUILTIN_ACCESSOR_T
#undef DECLARE_BUILTIN_ACCESSOR_S
#undef DECLARE_BUILTIN_ACCESSOR_H

  // Convenience wrappers.
  Handle<Code> CallFunction(
      ConvertReceiverMode = ConvertReceiverMode::kAny,
      TailCallMode tail_call_mode = TailCallMode::kDisallow);
  Handle<Code> Call(ConvertReceiverMode = ConvertReceiverMode::kAny,
                    TailCallMode tail_call_mode = TailCallMode::kDisallow);
  Handle<Code> CallBoundFunction(TailCallMode tail_call_mode);
  Handle<Code> InterpreterPushArgsAndCall(
      TailCallMode tail_call_mode,
      CallableType function_type = CallableType::kAny);

  Code* builtin(Name name) {
    // Code::cast cannot be used here since we access builtins
    // during the marking phase of mark sweep. See IC::Clear.
    return reinterpret_cast<Code*>(builtins_[name]);
  }

  Address builtin_address(Name name) {
    return reinterpret_cast<Address>(&builtins_[name]);
  }

  static Address c_function_address(CFunctionId id) {
    return c_functions_[id];
  }

  const char* name(int index) {
    DCHECK(index >= 0);
    DCHECK(index < builtin_count);
    return names_[index];
  }

  bool is_initialized() const { return initialized_; }

  MUST_USE_RESULT static MaybeHandle<Object> InvokeApiFunction(
      Isolate* isolate, Handle<HeapObject> function, Handle<Object> receiver,
      int argc, Handle<Object> args[]);

  enum ExitFrameType { EXIT, BUILTIN_EXIT };

 private:
  Builtins();

  // The external C++ functions called from the code.
  static Address const c_functions_[cfunction_count];

  // Note: These are always Code objects, but to conform with
  // IterateBuiltins() above which assumes Object**'s for the callback
  // function f, we use an Object* array here.
  Object* builtins_[builtin_count];
  const char* names_[builtin_count];

  static void Generate_Adaptor(MacroAssembler* masm, CFunctionId id,
                               ExitFrameType exit_frame_type);
  static void Generate_AllocateInNewSpace(MacroAssembler* masm);
  static void Generate_AllocateInOldSpace(MacroAssembler* masm);
  static void Generate_ConstructedNonConstructable(MacroAssembler* masm);
  static void Generate_InstantiateAsmJs(MacroAssembler* masm);
  static void Generate_CompileLazy(MacroAssembler* masm);
  static void Generate_CompileBaseline(MacroAssembler* masm);
  static void Generate_InOptimizationQueue(MacroAssembler* masm);
  static void Generate_CompileOptimized(MacroAssembler* masm);
  static void Generate_CompileOptimizedConcurrent(MacroAssembler* masm);
  static void Generate_JSConstructStubGeneric(MacroAssembler* masm);
  static void Generate_JSBuiltinsConstructStub(MacroAssembler* masm);
  static void Generate_JSBuiltinsConstructStubForDerived(MacroAssembler* masm);
  static void Generate_JSConstructStubApi(MacroAssembler* masm);
  static void Generate_JSEntryTrampoline(MacroAssembler* masm);
  static void Generate_JSConstructEntryTrampoline(MacroAssembler* masm);
  static void Generate_ResumeGeneratorTrampoline(MacroAssembler* masm);
  static void Generate_NotifyDeoptimized(MacroAssembler* masm);
  static void Generate_NotifySoftDeoptimized(MacroAssembler* masm);
  static void Generate_NotifyLazyDeoptimized(MacroAssembler* masm);
  static void Generate_NotifyStubFailure(MacroAssembler* masm);
  static void Generate_NotifyStubFailureSaveDoubles(MacroAssembler* masm);
  static void Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm);
  static void Generate_StringToNumber(MacroAssembler* masm);
  static void Generate_NonNumberToNumber(MacroAssembler* masm);
  static void Generate_ToNumber(MacroAssembler* masm);

  static void Generate_Apply(MacroAssembler* masm);

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  static void Generate_CallFunction(MacroAssembler* masm,
                                    ConvertReceiverMode mode,
                                    TailCallMode tail_call_mode);
  static void Generate_CallFunction_ReceiverIsNullOrUndefined(
      MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                          TailCallMode::kDisallow);
  }
  static void Generate_CallFunction_ReceiverIsNotNullOrUndefined(
      MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                          TailCallMode::kDisallow);
  }
  static void Generate_CallFunction_ReceiverIsAny(MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kAny,
                          TailCallMode::kDisallow);
  }
  static void Generate_TailCallFunction_ReceiverIsNullOrUndefined(
      MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kNullOrUndefined,
                          TailCallMode::kAllow);
  }
  static void Generate_TailCallFunction_ReceiverIsNotNullOrUndefined(
      MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kNotNullOrUndefined,
                          TailCallMode::kAllow);
  }
  static void Generate_TailCallFunction_ReceiverIsAny(MacroAssembler* masm) {
    Generate_CallFunction(masm, ConvertReceiverMode::kAny,
                          TailCallMode::kAllow);
  }
  // ES6 section 9.4.1.1 [[Call]] ( thisArgument, argumentsList)
  static void Generate_CallBoundFunctionImpl(MacroAssembler* masm,
                                             TailCallMode tail_call_mode);
  static void Generate_CallBoundFunction(MacroAssembler* masm) {
    Generate_CallBoundFunctionImpl(masm, TailCallMode::kDisallow);
  }
  static void Generate_TailCallBoundFunction(MacroAssembler* masm) {
    Generate_CallBoundFunctionImpl(masm, TailCallMode::kAllow);
  }
  // ES6 section 7.3.12 Call(F, V, [argumentsList])
  static void Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode,
                            TailCallMode tail_call_mode);
  static void Generate_Call_ReceiverIsNullOrUndefined(MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                  TailCallMode::kDisallow);
  }
  static void Generate_Call_ReceiverIsNotNullOrUndefined(MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                  TailCallMode::kDisallow);
  }
  static void Generate_Call_ReceiverIsAny(MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kDisallow);
  }
  static void Generate_TailCall_ReceiverIsNullOrUndefined(
      MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kNullOrUndefined,
                  TailCallMode::kAllow);
  }
  static void Generate_TailCall_ReceiverIsNotNullOrUndefined(
      MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kNotNullOrUndefined,
                  TailCallMode::kAllow);
  }
  static void Generate_TailCall_ReceiverIsAny(MacroAssembler* masm) {
    Generate_Call(masm, ConvertReceiverMode::kAny, TailCallMode::kAllow);
  }

  // ES6 section 9.2.2 [[Construct]] ( argumentsList, newTarget)
  static void Generate_ConstructFunction(MacroAssembler* masm);
  // ES6 section 9.4.1.2 [[Construct]] (argumentsList, newTarget)
  static void Generate_ConstructBoundFunction(MacroAssembler* masm);
  // ES6 section 9.5.14 [[Construct]] ( argumentsList, newTarget)
  static void Generate_ConstructProxy(MacroAssembler* masm);
  // ES6 section 7.3.13 Construct (F, [argumentsList], [newTarget])
  static void Generate_Construct(MacroAssembler* masm);

  static void Generate_HandleFastApiCall(MacroAssembler* masm);

  // ES6 section 19.3.3.2 Boolean.prototype.toString ( )
  static void Generate_BooleanPrototypeToString(CodeStubAssembler* assembler);
  // ES6 section 19.3.3.3 Boolean.prototype.valueOf ( )
  static void Generate_BooleanPrototypeValueOf(CodeStubAssembler* assembler);

  static void Generate_DatePrototype_GetField(MacroAssembler* masm,
                                              int field_index);
  // ES6 section 20.3.4.2 Date.prototype.getDate ( )
  static void Generate_DatePrototypeGetDate(MacroAssembler* masm);
  // ES6 section 20.3.4.3 Date.prototype.getDay ( )
  static void Generate_DatePrototypeGetDay(MacroAssembler* masm);
  // ES6 section 20.3.4.4 Date.prototype.getFullYear ( )
  static void Generate_DatePrototypeGetFullYear(MacroAssembler* masm);
  // ES6 section 20.3.4.5 Date.prototype.getHours ( )
  static void Generate_DatePrototypeGetHours(MacroAssembler* masm);
  // ES6 section 20.3.4.6 Date.prototype.getMilliseconds ( )
  static void Generate_DatePrototypeGetMilliseconds(MacroAssembler* masm);
  // ES6 section 20.3.4.7 Date.prototype.getMinutes ( )
  static void Generate_DatePrototypeGetMinutes(MacroAssembler* masm);
  // ES6 section 20.3.4.8 Date.prototype.getMonth ( )
  static void Generate_DatePrototypeGetMonth(MacroAssembler* masm);
  // ES6 section 20.3.4.9 Date.prototype.getSeconds ( )
  static void Generate_DatePrototypeGetSeconds(MacroAssembler* masm);
  // ES6 section 20.3.4.10 Date.prototype.getTime ( )
  static void Generate_DatePrototypeGetTime(MacroAssembler* masm);
  // ES6 section 20.3.4.11 Date.prototype.getTimezoneOffset ( )
  static void Generate_DatePrototypeGetTimezoneOffset(MacroAssembler* masm);
  // ES6 section 20.3.4.12 Date.prototype.getUTCDate ( )
  static void Generate_DatePrototypeGetUTCDate(MacroAssembler* masm);
  // ES6 section 20.3.4.13 Date.prototype.getUTCDay ( )
  static void Generate_DatePrototypeGetUTCDay(MacroAssembler* masm);
  // ES6 section 20.3.4.14 Date.prototype.getUTCFullYear ( )
  static void Generate_DatePrototypeGetUTCFullYear(MacroAssembler* masm);
  // ES6 section 20.3.4.15 Date.prototype.getUTCHours ( )
  static void Generate_DatePrototypeGetUTCHours(MacroAssembler* masm);
  // ES6 section 20.3.4.16 Date.prototype.getUTCMilliseconds ( )
  static void Generate_DatePrototypeGetUTCMilliseconds(MacroAssembler* masm);
  // ES6 section 20.3.4.17 Date.prototype.getUTCMinutes ( )
  static void Generate_DatePrototypeGetUTCMinutes(MacroAssembler* masm);
  // ES6 section 20.3.4.18 Date.prototype.getUTCMonth ( )
  static void Generate_DatePrototypeGetUTCMonth(MacroAssembler* masm);
  // ES6 section 20.3.4.19 Date.prototype.getUTCSeconds ( )
  static void Generate_DatePrototypeGetUTCSeconds(MacroAssembler* masm);

  static void Generate_FunctionPrototypeApply(MacroAssembler* masm);
  static void Generate_FunctionPrototypeCall(MacroAssembler* masm);

  static void Generate_ReflectApply(MacroAssembler* masm);
  static void Generate_ReflectConstruct(MacroAssembler* masm);

  static void Generate_InternalArrayCode(MacroAssembler* masm);
  static void Generate_ArrayCode(MacroAssembler* masm);

  // ES6 section 20.2.2.1 Math.abs ( x )
  static void Generate_MathAbs(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.2 Math.acos ( x )
  static void Generate_MathAcos(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.3 Math.acosh ( x )
  static void Generate_MathAcosh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.4 Math.asin ( x )
  static void Generate_MathAsin(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.5 Math.asinh ( x )
  static void Generate_MathAsinh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.6 Math.atan ( x )
  static void Generate_MathAtan(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.7 Math.atanh ( x )
  static void Generate_MathAtanh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.8 Math.atan2 ( y, x )
  static void Generate_MathAtan2(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.10 Math.ceil ( x )
  static void Generate_MathCeil(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.9 Math.ceil ( x )
  static void Generate_MathCbrt(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.15 Math.expm1 ( x )
  static void Generate_MathExpm1(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.11 Math.clz32 ( x )
  static void Generate_MathClz32(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.12 Math.cos ( x )
  static void Generate_MathCos(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.13 Math.cosh ( x )
  static void Generate_MathCosh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.14 Math.exp ( x )
  static void Generate_MathExp(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.16 Math.floor ( x )
  static void Generate_MathFloor(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.17 Math.fround ( x )
  static void Generate_MathFround(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.20 Math.imul ( x, y )
  static void Generate_MathImul(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.20 Math.log ( x )
  static void Generate_MathLog(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.21 Math.log ( x )
  static void Generate_MathLog1p(CodeStubAssembler* assembler);

  static void Generate_MathLog2(CodeStubAssembler* assembler);
  static void Generate_MathLog10(CodeStubAssembler* assembler);

  enum class MathMaxMinKind { kMax, kMin };
  static void Generate_MathMaxMin(MacroAssembler* masm, MathMaxMinKind kind);
  // ES6 section 20.2.2.24 Math.max ( value1, value2 , ...values )
  static void Generate_MathMax(MacroAssembler* masm) {
    Generate_MathMaxMin(masm, MathMaxMinKind::kMax);
  }
  // ES6 section 20.2.2.25 Math.min ( value1, value2 , ...values )
  static void Generate_MathMin(MacroAssembler* masm) {
    Generate_MathMaxMin(masm, MathMaxMinKind::kMin);
  }
  // ES6 section 20.2.2.26 Math.pow ( x, y )
  static void Generate_MathPow(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.28 Math.round ( x )
  static void Generate_MathRound(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.29 Math.sign ( x )
  static void Generate_MathSign(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.30 Math.sin ( x )
  static void Generate_MathSin(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.31 Math.sinh ( x )
  static void Generate_MathSinh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.32 Math.sqrt ( x )
  static void Generate_MathSqrt(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.33 Math.tan ( x )
  static void Generate_MathTan(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.34 Math.tanh ( x )
  static void Generate_MathTanh(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.35 Math.trunc ( x )
  static void Generate_MathTrunc(CodeStubAssembler* assembler);

  // ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Call]] case.
  static void Generate_NumberConstructor(MacroAssembler* masm);
  // ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Construct]] case.
  static void Generate_NumberConstructor_ConstructStub(MacroAssembler* masm);
  // ES6 section 20.1.3.7 Number.prototype.valueOf ( )
  static void Generate_NumberPrototypeValueOf(CodeStubAssembler* assembler);

  // ES6 section 19.2.3.6 Function.prototype [ @@hasInstance ] ( V )
  static void Generate_FunctionPrototypeHasInstance(
      CodeStubAssembler* assembler);

  // ES6 section 25.3.1.2 Generator.prototype.next ( value )
  static void Generate_GeneratorPrototypeNext(CodeStubAssembler* assembler);
  // ES6 section 25.3.1.3 Generator.prototype.return ( value )
  static void Generate_GeneratorPrototypeReturn(CodeStubAssembler* assembler);
  // ES6 section 25.3.1.4 Generator.prototype.throw ( exception )
  static void Generate_GeneratorPrototypeThrow(CodeStubAssembler* assembler);

  // ES6 section 19.1.3.2 Object.prototype.hasOwnProperty
  static void Generate_ObjectHasOwnProperty(CodeStubAssembler* assembler);

  // ES6 section 22.1.2.2 Array.isArray
  static void Generate_ArrayIsArray(CodeStubAssembler* assembler);

  // ES6 section 21.1.2.1 String.fromCharCode ( ...codeUnits )
  static void Generate_StringFromCharCode(CodeStubAssembler* assembler);
  // ES6 section 21.1.3.1 String.prototype.charAt ( pos )
  static void Generate_StringPrototypeCharAt(CodeStubAssembler* assembler);
  // ES6 section 21.1.3.2 String.prototype.charCodeAt ( pos )
  static void Generate_StringPrototypeCharCodeAt(CodeStubAssembler* assembler);
  // ES6 section 21.1.3.25 String.prototype.toString ()
  static void Generate_StringPrototypeToString(CodeStubAssembler* assembler);
  // ES6 section 21.1.3.28 String.prototype.valueOf ()
  static void Generate_StringPrototypeValueOf(CodeStubAssembler* assembler);

  static void Generate_StringConstructor(MacroAssembler* masm);
  static void Generate_StringConstructor_ConstructStub(MacroAssembler* masm);

  // ES6 section 19.4.3.4 Symbol.prototype [ @@toPrimitive ] ( hint )
  static void Generate_SymbolPrototypeToPrimitive(CodeStubAssembler* assembler);
  // ES6 section 19.4.3.2 Symbol.prototype.toString ( )
  static void Generate_SymbolPrototypeToString(CodeStubAssembler* assembler);
  // ES6 section 19.4.3.3 Symbol.prototype.valueOf ( )
  static void Generate_SymbolPrototypeValueOf(CodeStubAssembler* assembler);

  // ES6 section 22.2.3.2 get %TypedArray%.prototype.byteLength
  static void Generate_TypedArrayPrototypeByteLength(
      CodeStubAssembler* assembler);
  // ES6 section 22.2.3.3 get %TypedArray%.prototype.byteOffset
  static void Generate_TypedArrayPrototypeByteOffset(
      CodeStubAssembler* assembler);
  // ES6 section 22.2.3.18 get %TypedArray%.prototype.length
  static void Generate_TypedArrayPrototypeLength(CodeStubAssembler* assembler);

  static void Generate_OnStackReplacement(MacroAssembler* masm);
  static void Generate_InterruptCheck(MacroAssembler* masm);
  static void Generate_StackCheck(MacroAssembler* masm);

  static void Generate_InterpreterEntryTrampoline(MacroAssembler* masm);
  static void Generate_InterpreterEnterBytecodeDispatch(MacroAssembler* masm);
  static void Generate_InterpreterMarkBaselineOnReturn(MacroAssembler* masm);
  static void Generate_InterpreterPushArgsAndCall(MacroAssembler* masm) {
    return Generate_InterpreterPushArgsAndCallImpl(
        masm, TailCallMode::kDisallow, CallableType::kAny);
  }
  static void Generate_InterpreterPushArgsAndTailCall(MacroAssembler* masm) {
    return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                   CallableType::kAny);
  }
  static void Generate_InterpreterPushArgsAndCallFunction(
      MacroAssembler* masm) {
    return Generate_InterpreterPushArgsAndCallImpl(
        masm, TailCallMode::kDisallow, CallableType::kJSFunction);
  }
  static void Generate_InterpreterPushArgsAndTailCallFunction(
      MacroAssembler* masm) {
    return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow,
                                                   CallableType::kJSFunction);
  }
  static void Generate_InterpreterPushArgsAndCallImpl(
      MacroAssembler* masm, TailCallMode tail_call_mode,
      CallableType function_type);
  static void Generate_InterpreterPushArgsAndConstruct(MacroAssembler* masm);

#define DECLARE_CODE_AGE_BUILTIN_GENERATOR(C)                \
  static void Generate_Make##C##CodeYoungAgainEvenMarking(   \
      MacroAssembler* masm);                                 \
  static void Generate_Make##C##CodeYoungAgainOddMarking(    \
      MacroAssembler* masm);
  CODE_AGE_LIST(DECLARE_CODE_AGE_BUILTIN_GENERATOR)
#undef DECLARE_CODE_AGE_BUILTIN_GENERATOR

  static void Generate_MarkCodeAsToBeExecutedOnce(MacroAssembler* masm);
  static void Generate_MarkCodeAsExecutedOnce(MacroAssembler* masm);
  static void Generate_MarkCodeAsExecutedTwice(MacroAssembler* masm);

  static void Generate_AtomicsLoad(CodeStubAssembler* assembler);
  static void Generate_AtomicsStore(CodeStubAssembler* assembler);

  static void InitBuiltinFunctionTable();

  bool initialized_;

  friend class BuiltinFunctionTable;
  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Builtins);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_H_
