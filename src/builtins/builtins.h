// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_H_
#define V8_BUILTINS_BUILTINS_H_

#include "src/base/flags.h"
#include "src/handles.h"

namespace v8 {
namespace internal {

#define CODE_AGE_LIST_WITH_ARG(V, A) \
  V(Quadragenarian, A)               \
  V(Quinquagenarian, A)              \
  V(Sexagenarian, A)                 \
  V(Septuagenarian, A)               \
  V(Octogenarian, A)

#define CODE_AGE_LIST_IGNORE_ARG(X, V) V(X)

#define CODE_AGE_LIST(V) CODE_AGE_LIST_WITH_ARG(CODE_AGE_LIST_IGNORE_ARG, V)

#define CODE_AGE_LIST_COMPLETE(V) \
  V(ToBeExecutedOnce)             \
  V(NotExecuted)                  \
  V(ExecutedOnce)                 \
  V(NoAge)                        \
  CODE_AGE_LIST_WITH_ARG(CODE_AGE_LIST_IGNORE_ARG, V)

#define DECLARE_CODE_AGE_BUILTIN(C, V) \
  V(Make##C##CodeYoungAgainOddMarking) \
  V(Make##C##CodeYoungAgainEvenMarking)

// CPP: Builtin in C++. Entered via BUILTIN_EXIT frame.
//      Args: name
// API: Builtin in C++ for API callbacks. Entered via EXIT frame.
//      Args: name
// TFJ: Builtin in Turbofan, with JS linkage (callable as Javascript function).
//      Args: name, arguments count
// TFS: Builtin in Turbofan, with CodeStub linkage.
//      Args: name, code kind, extra IC state, interface descriptor
// ASM: Builtin in platform-dependent assembly.
//      Args: name
// ASH: Handlers implemented in platform-dependent assembly.
//      Args: name, code kind, extra IC state
// DBG: Builtin in platform-dependent assembly, used by the debugger.
//      Args: name
#define BUILTIN_LIST(CPP, API, TFJ, TFS, ASM, ASH, DBG)                     \
  /* Handlers */                                                            \
  ASM(KeyedLoadIC_Miss)                                                     \
  ASM(KeyedStoreIC_Miss)                                                    \
  ASH(LoadIC_Getter_ForDeopt, LOAD_IC, kNoExtraICState)                     \
  ASH(KeyedLoadIC_Megamorphic, KEYED_LOAD_IC, kNoExtraICState)              \
  ASH(StoreIC_Setter_ForDeopt, STORE_IC, StoreICState::kStrictModeState)    \
  ASH(KeyedStoreIC_Megamorphic, KEYED_STORE_IC, kNoExtraICState)            \
  ASH(KeyedStoreIC_Megamorphic_Strict, KEYED_STORE_IC,                      \
      StoreICState::kStrictModeState)                                       \
  ASH(KeyedLoadIC_Slow, HANDLER, Code::KEYED_LOAD_IC)                       \
  ASH(KeyedStoreIC_Slow, HANDLER, Code::KEYED_STORE_IC)                     \
  ASH(LoadIC_Normal, HANDLER, Code::LOAD_IC)                                \
  ASH(StoreIC_Normal, HANDLER, Code::STORE_IC)                              \
  TFS(LoadGlobalIC_Miss, BUILTIN, kNoExtraICState, LoadGlobalWithVector)    \
  TFS(LoadGlobalIC_SlowNotInsideTypeof, HANDLER, Code::LOAD_GLOBAL_IC,      \
      LoadGlobalWithVector)                                                 \
  TFS(LoadGlobalIC_SlowInsideTypeof, HANDLER, Code::LOAD_GLOBAL_IC,         \
      LoadGlobalWithVector)                                                 \
  TFS(LoadIC_Miss, BUILTIN, kNoExtraICState, LoadWithVector)                \
  TFS(LoadIC_Slow, HANDLER, Code::LOAD_IC, LoadWithVector)                  \
  TFS(StoreIC_Miss, BUILTIN, kNoExtraICState, StoreWithVector)              \
  TFS(StoreIC_SlowSloppy, HANDLER, Code::STORE_IC, StoreWithVector)         \
  TFS(StoreIC_SlowStrict, HANDLER, Code::STORE_IC, StoreWithVector)         \
  /* Code aging */                                                          \
  CODE_AGE_LIST_WITH_ARG(DECLARE_CODE_AGE_BUILTIN, ASM)                     \
  /* Calls */                                                               \
  ASM(ArgumentsAdaptorTrampoline)                                           \
  ASM(CallFunction_ReceiverIsNullOrUndefined)                               \
  ASM(CallFunction_ReceiverIsNotNullOrUndefined)                            \
  ASM(CallFunction_ReceiverIsAny)                                           \
  ASM(TailCallFunction_ReceiverIsNullOrUndefined)                           \
  ASM(TailCallFunction_ReceiverIsNotNullOrUndefined)                        \
  ASM(TailCallFunction_ReceiverIsAny)                                       \
  ASM(CallBoundFunction)                                                    \
  ASM(TailCallBoundFunction)                                                \
  ASM(Call_ReceiverIsNullOrUndefined)                                       \
  ASM(Call_ReceiverIsNotNullOrUndefined)                                    \
  ASM(Call_ReceiverIsAny)                                                   \
  ASM(TailCall_ReceiverIsNullOrUndefined)                                   \
  ASM(TailCall_ReceiverIsNotNullOrUndefined)                                \
  ASM(TailCall_ReceiverIsAny)                                               \
  /* Construct */                                                           \
  ASM(ConstructFunction)                                                    \
  ASM(ConstructBoundFunction)                                               \
  ASM(ConstructedNonConstructable)                                          \
  ASM(ConstructProxy)                                                       \
  ASM(Construct)                                                            \
  ASM(JSConstructStubApi)                                                   \
  ASM(JSConstructStubGeneric)                                               \
  ASM(JSBuiltinsConstructStub)                                              \
  ASM(JSBuiltinsConstructStubForDerived)                                    \
  /* Apply and entries */                                                   \
  ASM(Apply)                                                                \
  ASM(JSEntryTrampoline)                                                    \
  ASM(JSConstructEntryTrampoline)                                           \
  ASM(ResumeGeneratorTrampoline)                                            \
  /* Stack and interrupt check */                                           \
  ASM(InterruptCheck)                                                       \
  ASM(StackCheck)                                                           \
  /* Interpreter */                                                         \
  ASM(InterpreterEntryTrampoline)                                           \
  ASM(InterpreterMarkBaselineOnReturn)                                      \
  ASM(InterpreterPushArgsAndCallFunction)                                   \
  ASM(InterpreterPushArgsAndTailCallFunction)                               \
  ASM(InterpreterPushArgsAndCall)                                           \
  ASM(InterpreterPushArgsAndTailCall)                                       \
  ASM(InterpreterPushArgsAndConstruct)                                      \
  ASM(InterpreterEnterBytecodeDispatch)                                     \
  /* Code life-cycle */                                                     \
  ASM(CompileLazy)                                                          \
  ASM(CompileBaseline)                                                      \
  ASM(CompileOptimized)                                                     \
  ASM(CompileOptimizedConcurrent)                                           \
  ASM(InOptimizationQueue)                                                  \
  ASM(InstantiateAsmJs)                                                     \
  ASM(MarkCodeAsToBeExecutedOnce)                                           \
  ASM(MarkCodeAsExecutedOnce)                                               \
  ASM(MarkCodeAsExecutedTwice)                                              \
  ASM(NotifyDeoptimized)                                                    \
  ASM(NotifySoftDeoptimized)                                                \
  ASM(NotifyLazyDeoptimized)                                                \
  ASM(NotifyStubFailure)                                                    \
  ASM(NotifyStubFailureSaveDoubles)                                         \
  ASM(OnStackReplacement)                                                   \
  /* API callback handling */                                               \
  API(HandleApiCall)                                                        \
  API(HandleApiCallAsFunction)                                              \
  API(HandleApiCallAsConstructor)                                           \
  ASM(HandleFastApiCall)                                                    \
  /* Adapters for Turbofan into runtime */                                  \
  ASM(AllocateInNewSpace)                                                   \
  ASM(AllocateInOldSpace)                                                   \
  /* Debugger */                                                            \
  DBG(Return_DebugBreak)                                                    \
  DBG(Slot_DebugBreak)                                                      \
  DBG(FrameDropper_LiveEdit)                                                \
  /* Type conversions */                                                    \
  TFS(OrdinaryToPrimitive_Number, BUILTIN, kNoExtraICState, TypeConversion) \
  TFS(OrdinaryToPrimitive_String, BUILTIN, kNoExtraICState, TypeConversion) \
  TFS(NonPrimitiveToPrimitive_Default, BUILTIN, kNoExtraICState,            \
      TypeConversion)                                                       \
  TFS(NonPrimitiveToPrimitive_Number, BUILTIN, kNoExtraICState,             \
      TypeConversion)                                                       \
  TFS(NonPrimitiveToPrimitive_String, BUILTIN, kNoExtraICState,             \
      TypeConversion)                                                       \
  ASM(StringToNumber)                                                       \
  TFS(NonNumberToNumber, BUILTIN, kNoExtraICState, TypeConversion)          \
  ASM(ToNumber)                                                             \
                                                                            \
  /* Built-in functions for Javascript */                                   \
  /* Special internal builtins */                                           \
  CPP(EmptyFunction)                                                        \
  CPP(Illegal)                                                              \
  CPP(RestrictedFunctionPropertiesThrower)                                  \
  CPP(RestrictedStrictArgumentsPropertiesThrower)                           \
  /* Array */                                                               \
  ASM(ArrayCode)                                                            \
  ASM(InternalArrayCode)                                                    \
  CPP(ArrayConcat)                                                          \
  TFJ(ArrayIsArray, 2)                                                      \
  CPP(ArrayPop)                                                             \
  CPP(ArrayPush)                                                            \
  CPP(ArrayShift)                                                           \
  CPP(ArraySlice)                                                           \
  CPP(ArraySplice)                                                          \
  CPP(ArrayUnshift)                                                         \
  /* ArrayBuffer */                                                         \
  CPP(ArrayBufferConstructor)                                               \
  CPP(ArrayBufferConstructor_ConstructStub)                                 \
  CPP(ArrayBufferPrototypeGetByteLength)                                    \
  CPP(ArrayBufferIsView)                                                    \
  /* Boolean */                                                             \
  CPP(BooleanConstructor)                                                   \
  CPP(BooleanConstructor_ConstructStub)                                     \
  TFJ(BooleanPrototypeToString, 1)                                          \
  TFJ(BooleanPrototypeValueOf, 1)                                           \
  /* DataView */                                                            \
  CPP(DataViewConstructor)                                                  \
  CPP(DataViewConstructor_ConstructStub)                                    \
  CPP(DataViewPrototypeGetBuffer)                                           \
  CPP(DataViewPrototypeGetByteLength)                                       \
  CPP(DataViewPrototypeGetByteOffset)                                       \
  /* Date */                                                                \
  CPP(DateConstructor)                                                      \
  CPP(DateConstructor_ConstructStub)                                        \
  CPP(DateNow)                                                              \
  CPP(DateParse)                                                            \
  CPP(DateUTC)                                                              \
  CPP(DatePrototypeSetDate)                                                 \
  CPP(DatePrototypeSetFullYear)                                             \
  CPP(DatePrototypeSetHours)                                                \
  CPP(DatePrototypeSetMilliseconds)                                         \
  CPP(DatePrototypeSetMinutes)                                              \
  CPP(DatePrototypeSetMonth)                                                \
  CPP(DatePrototypeSetSeconds)                                              \
  CPP(DatePrototypeSetTime)                                                 \
  CPP(DatePrototypeSetUTCDate)                                              \
  CPP(DatePrototypeSetUTCFullYear)                                          \
  CPP(DatePrototypeSetUTCHours)                                             \
  CPP(DatePrototypeSetUTCMilliseconds)                                      \
  CPP(DatePrototypeSetUTCMinutes)                                           \
  CPP(DatePrototypeSetUTCMonth)                                             \
  CPP(DatePrototypeSetUTCSeconds)                                           \
  CPP(DatePrototypeToDateString)                                            \
  CPP(DatePrototypeToISOString)                                             \
  CPP(DatePrototypeToPrimitive)                                             \
  CPP(DatePrototypeToUTCString)                                             \
  CPP(DatePrototypeToString)                                                \
  CPP(DatePrototypeToTimeString)                                            \
  CPP(DatePrototypeValueOf)                                                 \
  ASM(DatePrototypeGetDate)                                                 \
  ASM(DatePrototypeGetDay)                                                  \
  ASM(DatePrototypeGetFullYear)                                             \
  ASM(DatePrototypeGetHours)                                                \
  ASM(DatePrototypeGetMilliseconds)                                         \
  ASM(DatePrototypeGetMinutes)                                              \
  ASM(DatePrototypeGetMonth)                                                \
  ASM(DatePrototypeGetSeconds)                                              \
  ASM(DatePrototypeGetTime)                                                 \
  ASM(DatePrototypeGetTimezoneOffset)                                       \
  ASM(DatePrototypeGetUTCDate)                                              \
  ASM(DatePrototypeGetUTCDay)                                               \
  ASM(DatePrototypeGetUTCFullYear)                                          \
  ASM(DatePrototypeGetUTCHours)                                             \
  ASM(DatePrototypeGetUTCMilliseconds)                                      \
  ASM(DatePrototypeGetUTCMinutes)                                           \
  ASM(DatePrototypeGetUTCMonth)                                             \
  ASM(DatePrototypeGetUTCSeconds)                                           \
  CPP(DatePrototypeGetYear)                                                 \
  CPP(DatePrototypeSetYear)                                                 \
  CPP(DatePrototypeToJson)                                                  \
  /* Function */                                                            \
  CPP(FunctionConstructor)                                                  \
  ASM(FunctionPrototypeApply)                                               \
  CPP(FunctionPrototypeBind)                                                \
  ASM(FunctionPrototypeCall)                                                \
  TFJ(FunctionPrototypeHasInstance, 2)                                      \
  CPP(FunctionPrototypeToString)                                            \
  /* Generator and Async */                                                 \
  CPP(GeneratorFunctionConstructor)                                         \
  TFJ(GeneratorPrototypeNext, 2)                                            \
  TFJ(GeneratorPrototypeReturn, 2)                                          \
  TFJ(GeneratorPrototypeThrow, 2)                                           \
  CPP(AsyncFunctionConstructor)                                             \
  /* Encode and decode */                                                   \
  CPP(GlobalDecodeURI)                                                      \
  CPP(GlobalDecodeURIComponent)                                             \
  CPP(GlobalEncodeURI)                                                      \
  CPP(GlobalEncodeURIComponent)                                             \
  CPP(GlobalEscape)                                                         \
  CPP(GlobalUnescape)                                                       \
  /* Eval */                                                                \
  CPP(GlobalEval)                                                           \
  /* JSON */                                                                \
  CPP(JsonParse)                                                            \
  CPP(JsonStringify)                                                        \
  /* Math */                                                                \
  TFJ(MathAcos, 2)                                                          \
  TFJ(MathAcosh, 2)                                                         \
  TFJ(MathAsin, 2)                                                          \
  TFJ(MathAsinh, 2)                                                         \
  TFJ(MathAtan, 2)                                                          \
  TFJ(MathAtanh, 2)                                                         \
  TFJ(MathAtan2, 3)                                                         \
  TFJ(MathCeil, 2)                                                          \
  TFJ(MathCbrt, 2)                                                          \
  TFJ(MathAbs, 2)                                                           \
  TFJ(MathExpm1, 2)                                                         \
  TFJ(MathClz32, 2)                                                         \
  TFJ(MathCos, 2)                                                           \
  TFJ(MathCosh, 2)                                                          \
  TFJ(MathExp, 2)                                                           \
  TFJ(MathFloor, 2)                                                         \
  TFJ(MathFround, 2)                                                        \
  CPP(MathHypot)                                                            \
  TFJ(MathImul, 3)                                                          \
  TFJ(MathLog, 2)                                                           \
  TFJ(MathLog1p, 2)                                                         \
  TFJ(MathLog10, 2)                                                         \
  TFJ(MathLog2, 2)                                                          \
  ASM(MathMax)                                                              \
  ASM(MathMin)                                                              \
  TFJ(MathRound, 2)                                                         \
  TFJ(MathPow, 3)                                                           \
  TFJ(MathSign, 2)                                                          \
  TFJ(MathSin, 2)                                                           \
  TFJ(MathSinh, 2)                                                          \
  TFJ(MathTan, 2)                                                           \
  TFJ(MathTanh, 2)                                                          \
  TFJ(MathSqrt, 2)                                                          \
  TFJ(MathTrunc, 2)                                                         \
  /* Number */                                                              \
  ASM(NumberConstructor)                                                    \
  ASM(NumberConstructor_ConstructStub)                                      \
  CPP(NumberPrototypeToExponential)                                         \
  CPP(NumberPrototypeToFixed)                                               \
  CPP(NumberPrototypeToLocaleString)                                        \
  CPP(NumberPrototypeToPrecision)                                           \
  CPP(NumberPrototypeToString)                                              \
  TFJ(NumberPrototypeValueOf, 1)                                            \
  /* Object */                                                              \
  CPP(ObjectAssign)                                                         \
  CPP(ObjectCreate)                                                         \
  CPP(ObjectDefineGetter)                                                   \
  CPP(ObjectDefineProperties)                                               \
  CPP(ObjectDefineProperty)                                                 \
  CPP(ObjectDefineSetter)                                                   \
  CPP(ObjectEntries)                                                        \
  CPP(ObjectFreeze)                                                         \
  CPP(ObjectGetOwnPropertyDescriptor)                                       \
  CPP(ObjectGetOwnPropertyDescriptors)                                      \
  CPP(ObjectGetOwnPropertyNames)                                            \
  CPP(ObjectGetOwnPropertySymbols)                                          \
  CPP(ObjectGetPrototypeOf)                                                 \
  TFJ(ObjectHasOwnProperty, 2)                                              \
  CPP(ObjectIs)                                                             \
  CPP(ObjectIsExtensible)                                                   \
  CPP(ObjectIsFrozen)                                                       \
  CPP(ObjectIsSealed)                                                       \
  CPP(ObjectKeys)                                                           \
  CPP(ObjectLookupGetter)                                                   \
  CPP(ObjectLookupSetter)                                                   \
  CPP(ObjectPreventExtensions)                                              \
  TFJ(ObjectProtoToString, 1)                                               \
  CPP(ObjectPrototypePropertyIsEnumerable)                                  \
  CPP(ObjectSeal)                                                           \
  CPP(ObjectValues)                                                         \
  /* Proxy */                                                               \
  CPP(ProxyConstructor)                                                     \
  CPP(ProxyConstructor_ConstructStub)                                       \
  /* Reflect */                                                             \
  ASM(ReflectApply)                                                         \
  ASM(ReflectConstruct)                                                     \
  CPP(ReflectDefineProperty)                                                \
  CPP(ReflectDeleteProperty)                                                \
  CPP(ReflectGet)                                                           \
  CPP(ReflectGetOwnPropertyDescriptor)                                      \
  CPP(ReflectGetPrototypeOf)                                                \
  CPP(ReflectHas)                                                           \
  CPP(ReflectIsExtensible)                                                  \
  CPP(ReflectOwnKeys)                                                       \
  CPP(ReflectPreventExtensions)                                             \
  CPP(ReflectSet)                                                           \
  CPP(ReflectSetPrototypeOf)                                                \
  /* SharedArrayBuffer */                                                   \
  CPP(SharedArrayBufferPrototypeGetByteLength)                              \
  TFJ(AtomicsLoad, 3)                                                       \
  TFJ(AtomicsStore, 4)                                                      \
  /* String */                                                              \
  ASM(StringConstructor)                                                    \
  ASM(StringConstructor_ConstructStub)                                      \
  CPP(StringFromCodePoint)                                                  \
  TFJ(StringFromCharCode, 2)                                                \
  TFJ(StringPrototypeCharAt, 2)                                             \
  TFJ(StringPrototypeCharCodeAt, 2)                                         \
  TFJ(StringPrototypeToString, 1)                                           \
  CPP(StringPrototypeTrim)                                                  \
  CPP(StringPrototypeTrimLeft)                                              \
  CPP(StringPrototypeTrimRight)                                             \
  TFJ(StringPrototypeValueOf, 1)                                            \
  /* Symbol */                                                              \
  CPP(SymbolConstructor)                                                    \
  CPP(SymbolConstructor_ConstructStub)                                      \
  TFJ(SymbolPrototypeToPrimitive, 2)                                        \
  TFJ(SymbolPrototypeToString, 1)                                           \
  TFJ(SymbolPrototypeValueOf, 1)                                            \
  /* TypedArray */                                                          \
  CPP(TypedArrayPrototypeBuffer)                                            \
  TFJ(TypedArrayPrototypeByteLength, 1)                                     \
  TFJ(TypedArrayPrototypeByteOffset, 1)                                     \
  TFJ(TypedArrayPrototypeLength, 1)

#define IGNORE_BUILTIN(...)

#define BUILTIN_LIST_ALL(V) BUILTIN_LIST(V, V, V, V, V, V, V)

#define BUILTIN_LIST_C(V)                                            \
  BUILTIN_LIST(V, V, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN)

#define BUILTIN_LIST_A(V)                                                      \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               V, V, V)

#define BUILTIN_LIST_DBG(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, V)

// Forward declarations.
class CodeStubAssembler;
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
#define DEF_ENUM(Name, ...) k##Name,
    BUILTIN_LIST_ALL(DEF_ENUM)
#undef DEF_ENUM
        builtin_count
  };

#define DECLARE_BUILTIN_ACCESSOR(Name, ...) Handle<Code> Name();
  BUILTIN_LIST_ALL(DECLARE_BUILTIN_ACCESSOR)
#undef DECLARE_BUILTIN_ACCESSOR

  // Convenience wrappers.
  Handle<Code> CallFunction(
      ConvertReceiverMode = ConvertReceiverMode::kAny,
      TailCallMode tail_call_mode = TailCallMode::kDisallow);
  Handle<Code> Call(ConvertReceiverMode = ConvertReceiverMode::kAny,
                    TailCallMode tail_call_mode = TailCallMode::kDisallow);
  Handle<Code> CallBoundFunction(TailCallMode tail_call_mode);
  Handle<Code> NonPrimitiveToPrimitive(
      ToPrimitiveHint hint = ToPrimitiveHint::kDefault);
  Handle<Code> OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint);
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

  const char* name(int index);

  bool is_initialized() const { return initialized_; }

  MUST_USE_RESULT static MaybeHandle<Object> InvokeApiFunction(
      Isolate* isolate, Handle<HeapObject> function, Handle<Object> receiver,
      int argc, Handle<Object> args[]);

  enum ExitFrameType { EXIT, BUILTIN_EXIT };

  static void Generate_Adaptor(MacroAssembler* masm, Address builtin_address,
                               ExitFrameType exit_frame_type);

 private:
  Builtins();

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

  // ES6 section 19.1.3.6 Object.prototype.toString ()
  static void Generate_ObjectProtoToString(CodeStubAssembler* assembler);

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

#define DECLARE_CODE_AGE_BUILTIN_GENERATOR(C)              \
  static void Generate_Make##C##CodeYoungAgainEvenMarking( \
      MacroAssembler* masm);                               \
  static void Generate_Make##C##CodeYoungAgainOddMarking(MacroAssembler* masm);
  CODE_AGE_LIST(DECLARE_CODE_AGE_BUILTIN_GENERATOR)
#undef DECLARE_CODE_AGE_BUILTIN_GENERATOR

  static void Generate_MarkCodeAsToBeExecutedOnce(MacroAssembler* masm);
  static void Generate_MarkCodeAsExecutedOnce(MacroAssembler* masm);
  static void Generate_MarkCodeAsExecutedTwice(MacroAssembler* masm);

  static void Generate_AtomicsLoad(CodeStubAssembler* assembler);
  static void Generate_AtomicsStore(CodeStubAssembler* assembler);

  // Note: These are always Code objects, but to conform with
  // IterateBuiltins() above which assumes Object**'s for the callback
  // function f, we use an Object* array here.
  Object* builtins_[builtin_count];
  bool initialized_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Builtins);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_H_
