
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
#define BUILTIN_LIST_C(V)                 \
  V(Illegal)                              \
                                          \
  V(EmptyFunction)                        \
                                          \
  V(ArrayConcat)                          \
  V(ArrayPop)                             \
  V(ArrayPush)                            \
  V(ArrayShift)                           \
  V(ArraySlice)                           \
  V(ArraySplice)                          \
  V(ArrayUnshift)                         \
                                          \
  V(ArrayBufferConstructor)               \
  V(ArrayBufferConstructor_ConstructStub) \
  V(ArrayBufferIsView)                    \
                                          \
  V(BooleanConstructor)                   \
  V(BooleanConstructor_ConstructStub)     \
  V(BooleanPrototypeToString)             \
  V(BooleanPrototypeValueOf)              \
                                          \
  V(DataViewConstructor)                  \
  V(DataViewConstructor_ConstructStub)    \
  V(DataViewPrototypeGetBuffer)           \
  V(DataViewPrototypeGetByteLength)       \
  V(DataViewPrototypeGetByteOffset)       \
                                          \
  V(DateConstructor)                      \
  V(DateConstructor_ConstructStub)        \
  V(DateNow)                              \
  V(DateParse)                            \
  V(DateUTC)                              \
  V(DatePrototypeSetDate)                 \
  V(DatePrototypeSetFullYear)             \
  V(DatePrototypeSetHours)                \
  V(DatePrototypeSetMilliseconds)         \
  V(DatePrototypeSetMinutes)              \
  V(DatePrototypeSetMonth)                \
  V(DatePrototypeSetSeconds)              \
  V(DatePrototypeSetTime)                 \
  V(DatePrototypeSetUTCDate)              \
  V(DatePrototypeSetUTCFullYear)          \
  V(DatePrototypeSetUTCHours)             \
  V(DatePrototypeSetUTCMilliseconds)      \
  V(DatePrototypeSetUTCMinutes)           \
  V(DatePrototypeSetUTCMonth)             \
  V(DatePrototypeSetUTCSeconds)           \
  V(DatePrototypeToDateString)            \
  V(DatePrototypeToISOString)             \
  V(DatePrototypeToPrimitive)             \
  V(DatePrototypeToUTCString)             \
  V(DatePrototypeToString)                \
  V(DatePrototypeToTimeString)            \
  V(DatePrototypeValueOf)                 \
  V(DatePrototypeGetYear)                 \
  V(DatePrototypeSetYear)                 \
  V(DatePrototypeToJson)                  \
                                          \
  V(FunctionConstructor)                  \
  V(FunctionPrototypeBind)                \
  V(FunctionPrototypeToString)            \
                                          \
  V(GeneratorFunctionConstructor)         \
  V(AsyncFunctionConstructor)             \
                                          \
  V(GlobalDecodeURI)                      \
  V(GlobalDecodeURIComponent)             \
  V(GlobalEncodeURI)                      \
  V(GlobalEncodeURIComponent)             \
  V(GlobalEscape)                         \
  V(GlobalUnescape)                       \
                                          \
  V(GlobalEval)                           \
                                          \
  V(JsonParse)                            \
  V(JsonStringify)                        \
                                          \
  V(MathAcos)                             \
  V(MathAsin)                             \
  V(MathFround)                           \
  V(MathImul)                             \
                                          \
  V(ObjectAssign)                         \
  V(ObjectCreate)                         \
  V(ObjectDefineGetter)                   \
  V(ObjectDefineProperties)               \
  V(ObjectDefineProperty)                 \
  V(ObjectDefineSetter)                   \
  V(ObjectEntries)                        \
  V(ObjectFreeze)                         \
  V(ObjectGetOwnPropertyDescriptor)       \
  V(ObjectGetOwnPropertyDescriptors)      \
  V(ObjectGetOwnPropertyNames)            \
  V(ObjectGetOwnPropertySymbols)          \
  V(ObjectGetPrototypeOf)                 \
  V(ObjectIs)                             \
  V(ObjectIsExtensible)                   \
  V(ObjectIsFrozen)                       \
  V(ObjectIsSealed)                       \
  V(ObjectKeys)                           \
  V(ObjectLookupGetter)                   \
  V(ObjectLookupSetter)                   \
  V(ObjectPreventExtensions)              \
  V(ObjectProtoToString)                  \
  V(ObjectSeal)                           \
  V(ObjectValues)                         \
                                          \
  V(ProxyConstructor)                     \
  V(ProxyConstructor_ConstructStub)       \
                                          \
  V(ReflectDefineProperty)                \
  V(ReflectDeleteProperty)                \
  V(ReflectGet)                           \
  V(ReflectGetOwnPropertyDescriptor)      \
  V(ReflectGetPrototypeOf)                \
  V(ReflectHas)                           \
  V(ReflectIsExtensible)                  \
  V(ReflectOwnKeys)                       \
  V(ReflectPreventExtensions)             \
  V(ReflectSet)                           \
  V(ReflectSetPrototypeOf)                \
                                          \
  V(StringFromCodePoint)                  \
                                          \
  V(StringPrototypeTrim)                  \
  V(StringPrototypeTrimLeft)              \
  V(StringPrototypeTrimRight)             \
                                          \
  V(SymbolConstructor)                    \
  V(SymbolConstructor_ConstructStub)      \
                                          \
  V(TypedArrayPrototypeBuffer)            \
                                          \
  V(HandleApiCall)                        \
  V(HandleApiCallAsFunction)              \
  V(HandleApiCallAsConstructor)           \
                                          \
  V(RestrictedFunctionPropertiesThrower)  \
  V(RestrictedStrictArgumentsPropertiesThrower)

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
  V(FunctionPrototypeHasInstance, 2)  \
  V(GeneratorPrototypeNext, 2)        \
  V(GeneratorPrototypeReturn, 2)      \
  V(GeneratorPrototypeThrow, 2)       \
  V(MathAtan, 2)                      \
  V(MathAtan2, 3)                     \
  V(MathAtanh, 2)                     \
  V(MathCeil, 2)                      \
  V(MathCbrt, 2)                      \
  V(MathExpm1, 2)                     \
  V(MathClz32, 2)                     \
  V(MathCos, 2)                       \
  V(MathExp, 2)                       \
  V(MathFloor, 2)                     \
  V(MathLog, 2)                       \
  V(MathLog1p, 2)                     \
  V(MathLog2, 2)                      \
  V(MathLog10, 2)                     \
  V(MathRound, 2)                     \
  V(MathSin, 2)                       \
  V(MathTan, 2)                       \
  V(MathSqrt, 2)                      \
  V(MathTrunc, 2)                     \
  V(ObjectHasOwnProperty, 2)          \
  V(ArrayIsArray, 2)                  \
  V(StringFromCharCode, 2)            \
  V(StringPrototypeCharAt, 2)         \
  V(StringPrototypeCharCodeAt, 2)     \
  V(TypedArrayPrototypeByteLength, 1) \
  V(TypedArrayPrototypeByteOffset, 1) \
  V(TypedArrayPrototypeLength, 1)     \
  V(AtomicsLoad, 3)                   \
  V(AtomicsStore, 4)

// Define list of builtins implemented in TurboFan (with CallStub linkage).
#define BUILTIN_LIST_S(V)                                                   \
  V(LoadGlobalIC_Miss, BUILTIN, kNoExtraICState, LoadGlobalWithVector)      \
  V(LoadGlobalIC_Slow, HANDLER, Code::LOAD_GLOBAL_IC, LoadGlobalWithVector) \
  V(LoadIC_Miss, BUILTIN, kNoExtraICState, LoadWithVector)                  \
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
#define DEF_ENUM_C(name) k##name,
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
#define DEF_ENUM_C(name) c_##name,
    BUILTIN_LIST_C(DEF_ENUM_C)
#undef DEF_ENUM_C
    cfunction_count
  };

#define DECLARE_BUILTIN_ACCESSOR_C(name) Handle<Code> name();
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
  Handle<Code> InterpreterPushArgsAndCall(TailCallMode tail_call_mode);

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

 private:
  Builtins();

  // The external C++ functions called from the code.
  static Address const c_functions_[cfunction_count];

  // Note: These are always Code objects, but to conform with
  // IterateBuiltins() above which assumes Object**'s for the callback
  // function f, we use an Object* array here.
  Object* builtins_[builtin_count];
  const char* names_[builtin_count];

  static void Generate_Adaptor(MacroAssembler* masm, CFunctionId id);
  static void Generate_AllocateInNewSpace(MacroAssembler* masm);
  static void Generate_AllocateInOldSpace(MacroAssembler* masm);
  static void Generate_ConstructedNonConstructable(MacroAssembler* masm);
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

  // ES6 section 20.2.2.6 Math.atan ( x )
  static void Generate_MathAtan(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.8 Math.atan2 ( y, x )
  static void Generate_MathAtan2(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.7 Math.atanh ( x )
  static void Generate_MathAtanh(CodeStubAssembler* assembler);
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
  // ES6 section 20.2.2.14 Math.exp ( x )
  static void Generate_MathExp(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.16 Math.floor ( x )
  static void Generate_MathFloor(CodeStubAssembler* assembler);
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
  // ES6 section 20.2.2.28 Math.round ( x )
  static void Generate_MathRound(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.20 Math.sin ( x )
  static void Generate_MathSin(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.32 Math.sqrt ( x )
  static void Generate_MathSqrt(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.33 Math.sin ( x )
  static void Generate_MathTan(CodeStubAssembler* assembler);
  // ES6 section 20.2.2.35 Math.trunc ( x )
  static void Generate_MathTrunc(CodeStubAssembler* assembler);

  // ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Call]] case.
  static void Generate_NumberConstructor(MacroAssembler* masm);
  // ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Construct]] case.
  static void Generate_NumberConstructor_ConstructStub(MacroAssembler* masm);

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

  static void Generate_StringConstructor(MacroAssembler* masm);
  static void Generate_StringConstructor_ConstructStub(MacroAssembler* masm);

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
    return Generate_InterpreterPushArgsAndCallImpl(masm,
                                                   TailCallMode::kDisallow);
  }
  static void Generate_InterpreterPushArgsAndTailCall(MacroAssembler* masm) {
    return Generate_InterpreterPushArgsAndCallImpl(masm, TailCallMode::kAllow);
  }
  static void Generate_InterpreterPushArgsAndCallImpl(
      MacroAssembler* masm, TailCallMode tail_call_mode);
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
