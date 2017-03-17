// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_H_
#define V8_BUILTINS_BUILTINS_H_

#include "src/base/flags.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

class Callable;
template <typename T>
class Handle;
class Isolate;

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

#define DECLARE_CODE_AGE_BUILTIN(C, V) V(Make##C##CodeYoungAgain)

// CPP: Builtin in C++. Entered via BUILTIN_EXIT frame.
//      Args: name
// API: Builtin in C++ for API callbacks. Entered via EXIT frame.
//      Args: name
// TFJ: Builtin in Turbofan, with JS linkage (callable as Javascript function).
//      Args: name, arguments count, explicit argument names...
// TFS: Builtin in Turbofan, with CodeStub linkage.
//      Args: name, code kind, extra IC state, interface descriptor, return_size
// ASM: Builtin in platform-dependent assembly.
//      Args: name
// ASH: Handlers implemented in platform-dependent assembly.
//      Args: name, code kind, extra IC state
// DBG: Builtin in platform-dependent assembly, used by the debugger.
//      Args: name

#define BUILTIN_LIST(CPP, API, TFJ, TFS, ASM, ASH, DBG)                        \
  ASM(Abort)                                                                   \
  /* Code aging */                                                             \
  CODE_AGE_LIST_WITH_ARG(DECLARE_CODE_AGE_BUILTIN, ASM)                        \
                                                                               \
  /* Declared first for dependency reasons */                                  \
  ASM(CompileLazy)                                                             \
  TFS(ToObject, BUILTIN, kNoExtraICState, TypeConversion, 1)                   \
  TFS(FastNewObject, BUILTIN, kNoExtraICState, FastNewObject, 1)               \
  TFS(HasProperty, BUILTIN, kNoExtraICState, HasProperty, 1)                   \
                                                                               \
  /* Calls */                                                                  \
  ASM(ArgumentsAdaptorTrampoline)                                              \
  /* ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList) */              \
  ASM(CallFunction_ReceiverIsNullOrUndefined)                                  \
  ASM(CallFunction_ReceiverIsNotNullOrUndefined)                               \
  ASM(CallFunction_ReceiverIsAny)                                              \
  ASM(TailCallFunction_ReceiverIsNullOrUndefined)                              \
  ASM(TailCallFunction_ReceiverIsNotNullOrUndefined)                           \
  ASM(TailCallFunction_ReceiverIsAny)                                          \
  /* ES6 section 9.4.1.1 [[Call]] ( thisArgument, argumentsList) */            \
  ASM(CallBoundFunction)                                                       \
  ASM(TailCallBoundFunction)                                                   \
  /* ES6 section 7.3.12 Call(F, V, [argumentsList]) */                         \
  ASM(Call_ReceiverIsNullOrUndefined)                                          \
  ASM(Call_ReceiverIsNotNullOrUndefined)                                       \
  ASM(Call_ReceiverIsAny)                                                      \
  ASM(TailCall_ReceiverIsNullOrUndefined)                                      \
  ASM(TailCall_ReceiverIsNotNullOrUndefined)                                   \
  ASM(TailCall_ReceiverIsAny)                                                  \
  ASM(CallWithSpread)                                                          \
  ASM(CallForwardVarargs)                                                      \
  ASM(CallFunctionForwardVarargs)                                              \
                                                                               \
  /* Construct */                                                              \
  /* ES6 section 9.2.2 [[Construct]] ( argumentsList, newTarget) */            \
  ASM(ConstructFunction)                                                       \
  /* ES6 section 9.4.1.2 [[Construct]] (argumentsList, newTarget) */           \
  ASM(ConstructBoundFunction)                                                  \
  ASM(ConstructedNonConstructable)                                             \
  /* ES6 section 9.5.14 [[Construct]] ( argumentsList, newTarget) */           \
  ASM(ConstructProxy)                                                          \
  /* ES6 section 7.3.13 Construct (F, [argumentsList], [newTarget]) */         \
  ASM(Construct)                                                               \
  ASM(ConstructWithSpread)                                                     \
  ASM(JSConstructStubApi)                                                      \
  ASM(JSConstructStubGeneric)                                                  \
  ASM(JSBuiltinsConstructStub)                                                 \
  ASM(JSBuiltinsConstructStubForDerived)                                       \
  TFS(FastNewClosure, BUILTIN, kNoExtraICState, FastNewClosure, 1)             \
  TFS(FastNewFunctionContextEval, BUILTIN, kNoExtraICState,                    \
      FastNewFunctionContext, 1)                                               \
  TFS(FastNewFunctionContextFunction, BUILTIN, kNoExtraICState,                \
      FastNewFunctionContext, 1)                                               \
  TFS(FastNewStrictArguments, BUILTIN, kNoExtraICState, FastNewArguments, 1)   \
  TFS(FastNewSloppyArguments, BUILTIN, kNoExtraICState, FastNewArguments, 1)   \
  TFS(FastNewRestParameter, BUILTIN, kNoExtraICState, FastNewArguments, 1)     \
  TFS(FastCloneRegExp, BUILTIN, kNoExtraICState, FastCloneRegExp, 1)           \
  TFS(FastCloneShallowArrayTrack, BUILTIN, kNoExtraICState,                    \
      FastCloneShallowArray, 1)                                                \
  TFS(FastCloneShallowArrayDontTrack, BUILTIN, kNoExtraICState,                \
      FastCloneShallowArray, 1)                                                \
  TFS(FastCloneShallowObject0, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject1, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject2, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject3, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject4, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject5, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
  TFS(FastCloneShallowObject6, BUILTIN, kNoExtraICState,                       \
      FastCloneShallowObject, 1)                                               \
                                                                               \
  /* Apply and entries */                                                      \
  ASM(Apply)                                                                   \
  ASM(JSEntryTrampoline)                                                       \
  ASM(JSConstructEntryTrampoline)                                              \
  ASM(ResumeGeneratorTrampoline)                                               \
                                                                               \
  /* Stack and interrupt check */                                              \
  ASM(InterruptCheck)                                                          \
  ASM(StackCheck)                                                              \
                                                                               \
  /* String helpers */                                                         \
  TFS(StringCharAt, BUILTIN, kNoExtraICState, StringCharAt, 1)                 \
  TFS(StringCharCodeAt, BUILTIN, kNoExtraICState, StringCharCodeAt, 1)         \
  TFS(StringEqual, BUILTIN, kNoExtraICState, Compare, 1)                       \
  TFS(StringGreaterThan, BUILTIN, kNoExtraICState, Compare, 1)                 \
  TFS(StringGreaterThanOrEqual, BUILTIN, kNoExtraICState, Compare, 1)          \
  TFS(StringIndexOf, BUILTIN, kNoExtraICState, StringIndexOf, 1)               \
  TFS(StringLessThan, BUILTIN, kNoExtraICState, Compare, 1)                    \
  TFS(StringLessThanOrEqual, BUILTIN, kNoExtraICState, Compare, 1)             \
                                                                               \
  /* Interpreter */                                                            \
  ASM(InterpreterEntryTrampoline)                                              \
  ASM(InterpreterPushArgsAndCall)                                              \
  ASM(InterpreterPushArgsAndCallFunction)                                      \
  ASM(InterpreterPushArgsAndCallWithFinalSpread)                               \
  ASM(InterpreterPushArgsAndTailCall)                                          \
  ASM(InterpreterPushArgsAndTailCallFunction)                                  \
  ASM(InterpreterPushArgsAndConstruct)                                         \
  ASM(InterpreterPushArgsAndConstructFunction)                                 \
  ASM(InterpreterPushArgsAndConstructArray)                                    \
  ASM(InterpreterPushArgsAndConstructWithFinalSpread)                          \
  ASM(InterpreterEnterBytecodeAdvance)                                         \
  ASM(InterpreterEnterBytecodeDispatch)                                        \
  ASM(InterpreterOnStackReplacement)                                           \
                                                                               \
  /* Code life-cycle */                                                        \
  ASM(CompileBaseline)                                                         \
  ASM(CompileOptimized)                                                        \
  ASM(CompileOptimizedConcurrent)                                              \
  ASM(InOptimizationQueue)                                                     \
  ASM(InstantiateAsmJs)                                                        \
  ASM(MarkCodeAsToBeExecutedOnce)                                              \
  ASM(MarkCodeAsExecutedOnce)                                                  \
  ASM(MarkCodeAsExecutedTwice)                                                 \
  ASM(NotifyDeoptimized)                                                       \
  ASM(NotifySoftDeoptimized)                                                   \
  ASM(NotifyLazyDeoptimized)                                                   \
  ASM(NotifyStubFailure)                                                       \
  ASM(NotifyStubFailureSaveDoubles)                                            \
  ASM(OnStackReplacement)                                                      \
                                                                               \
  /* API callback handling */                                                  \
  API(HandleApiCall)                                                           \
  API(HandleApiCallAsFunction)                                                 \
  API(HandleApiCallAsConstructor)                                              \
                                                                               \
  /* Adapters for Turbofan into runtime */                                     \
  ASM(AllocateInNewSpace)                                                      \
  ASM(AllocateInOldSpace)                                                      \
                                                                               \
  /* TurboFan support builtins */                                              \
  TFS(CopyFastSmiOrObjectElements, BUILTIN, kNoExtraICState,                   \
      CopyFastSmiOrObjectElements, 1)                                          \
  TFS(GrowFastDoubleElements, BUILTIN, kNoExtraICState, GrowArrayElements, 1)  \
  TFS(GrowFastSmiOrObjectElements, BUILTIN, kNoExtraICState,                   \
      GrowArrayElements, 1)                                                    \
  TFS(NewUnmappedArgumentsElements, BUILTIN, kNoExtraICState,                  \
      NewArgumentsElements, 1)                                                 \
                                                                               \
  /* Debugger */                                                               \
  DBG(FrameDropperTrampoline)                                                  \
  DBG(HandleDebuggerStatement)                                                 \
  DBG(Return_DebugBreak)                                                       \
  DBG(Slot_DebugBreak)                                                         \
                                                                               \
  /* Type conversions */                                                       \
  TFS(ToBoolean, BUILTIN, kNoExtraICState, TypeConversion, 1)                  \
  TFS(OrdinaryToPrimitive_Number, BUILTIN, kNoExtraICState, TypeConversion, 1) \
  TFS(OrdinaryToPrimitive_String, BUILTIN, kNoExtraICState, TypeConversion, 1) \
  TFS(NonPrimitiveToPrimitive_Default, BUILTIN, kNoExtraICState,               \
      TypeConversion, 1)                                                       \
  TFS(NonPrimitiveToPrimitive_Number, BUILTIN, kNoExtraICState,                \
      TypeConversion, 1)                                                       \
  TFS(NonPrimitiveToPrimitive_String, BUILTIN, kNoExtraICState,                \
      TypeConversion, 1)                                                       \
  TFS(StringToNumber, BUILTIN, kNoExtraICState, TypeConversion, 1)             \
  TFS(ToName, BUILTIN, kNoExtraICState, TypeConversion, 1)                     \
  TFS(NonNumberToNumber, BUILTIN, kNoExtraICState, TypeConversion, 1)          \
  TFS(ToNumber, BUILTIN, kNoExtraICState, TypeConversion, 1)                   \
  TFS(ToString, BUILTIN, kNoExtraICState, TypeConversion, 1)                   \
  TFS(ToInteger, BUILTIN, kNoExtraICState, TypeConversion, 1)                  \
  TFS(ToLength, BUILTIN, kNoExtraICState, TypeConversion, 1)                   \
  TFS(ClassOf, BUILTIN, kNoExtraICState, Typeof, 1)                            \
  TFS(Typeof, BUILTIN, kNoExtraICState, Typeof, 1)                             \
  TFS(GetSuperConstructor, BUILTIN, kNoExtraICState, TypeConversion, 1)        \
                                                                               \
  /* Handlers */                                                               \
  TFS(LoadICProtoArray, BUILTIN, kNoExtraICState, LoadICProtoArray, 1)         \
  TFS(LoadICProtoArrayThrowIfNonexistent, BUILTIN, kNoExtraICState,            \
      LoadICProtoArray, 1)                                                     \
  TFS(KeyedLoadIC_Megamorphic, BUILTIN, kNoExtraICState, LoadWithVector, 1)    \
  TFS(KeyedLoadIC_Miss, BUILTIN, kNoExtraICState, LoadWithVector, 1)           \
  TFS(KeyedLoadIC_Slow, HANDLER, Code::LOAD_IC, LoadWithVector, 1)             \
  TFS(KeyedLoadIC_IndexedString, HANDLER, Code::LOAD_IC, LoadWithVector, 1)    \
  TFS(KeyedStoreIC_Megamorphic, BUILTIN, kNoExtraICState, StoreWithVector, 1)  \
  TFS(KeyedStoreIC_Megamorphic_Strict, BUILTIN, kNoExtraICState,               \
      StoreWithVector, 1)                                                      \
  TFS(KeyedStoreIC_Miss, BUILTIN, kNoExtraICState, StoreWithVector, 1)         \
  TFS(KeyedStoreIC_Slow, HANDLER, Code::STORE_IC, StoreWithVector, 1)          \
  TFS(LoadGlobalIC_Miss, BUILTIN, kNoExtraICState, LoadGlobalWithVector, 1)    \
  TFS(LoadGlobalIC_Slow, HANDLER, Code::LOAD_GLOBAL_IC, LoadGlobalWithVector,  \
      1)                                                                       \
  TFS(LoadField, BUILTIN, kNoExtraICState, LoadField, 1)                       \
  TFS(LoadIC_FunctionPrototype, HANDLER, Code::LOAD_IC, LoadWithVector, 1)     \
  ASH(LoadIC_Getter_ForDeopt, BUILTIN, kNoExtraICState)                        \
  TFS(LoadIC_Miss, BUILTIN, kNoExtraICState, LoadWithVector, 1)                \
  TFS(LoadIC_Slow, HANDLER, Code::LOAD_IC, LoadWithVector, 1)                  \
  TFS(LoadIC_Uninitialized, BUILTIN, kNoExtraICState, LoadWithVector, 1)       \
  TFS(StoreIC_Miss, BUILTIN, kNoExtraICState, StoreWithVector, 1)              \
  ASH(StoreIC_Setter_ForDeopt, BUILTIN, kNoExtraICState)                       \
  TFS(StoreIC_Uninitialized, BUILTIN, kNoExtraICState, StoreWithVector, 1)     \
  TFS(StoreICStrict_Uninitialized, BUILTIN, kNoExtraICState, StoreWithVector,  \
      1)                                                                       \
                                                                               \
  /* Built-in functions for Javascript */                                      \
  /* Special internal builtins */                                              \
  CPP(EmptyFunction)                                                           \
  CPP(Illegal)                                                                 \
  CPP(RestrictedFunctionPropertiesThrower)                                     \
  CPP(RestrictedStrictArgumentsPropertiesThrower)                              \
  CPP(UnsupportedThrower)                                                      \
  TFJ(ReturnReceiver, 0)                                                       \
                                                                               \
  /* Array */                                                                  \
  ASM(ArrayCode)                                                               \
  ASM(InternalArrayCode)                                                       \
  CPP(ArrayConcat)                                                             \
  /* ES6 #sec-array.isarray */                                                 \
  TFJ(ArrayIsArray, 1, kArg)                                                   \
  /* ES7 #sec-array.prototype.includes */                                      \
  TFJ(ArrayIncludes, 2, kSearchElement, kFromIndex)                            \
  /* ES6 #sec-array.prototype.indexof */                                       \
  TFJ(ArrayIndexOf, 2, kSearchElement, kFromIndex)                             \
  /* ES6 #sec-array.prototype.pop */                                           \
  CPP(ArrayPop)                                                                \
  /* ES6 #sec-array.prototype.push */                                          \
  CPP(ArrayPush)                                                               \
  TFJ(FastArrayPush, SharedFunctionInfo::kDontAdaptArgumentsSentinel)          \
  /* ES6 #sec-array.prototype.shift */                                         \
  CPP(ArrayShift)                                                              \
  /* ES6 #sec-array.prototype.slice */                                         \
  CPP(ArraySlice)                                                              \
  /* ES6 #sec-array.prototype.splice */                                        \
  CPP(ArraySplice)                                                             \
  /* ES6 #sec-array.prototype.unshift */                                       \
  CPP(ArrayUnshift)                                                            \
  /* ES6 #sec-array.prototype.foreach */                                       \
  TFJ(ArrayForEachLoopContinuation, 6)                                         \
  TFJ(ArrayForEach, 2, kCallbackFn, kThisArg)                                  \
  /* ES6 #sec-array.prototype.every */                                         \
  TFJ(ArrayEveryLoopContinuation, 6)                                           \
  TFJ(ArrayEvery, 2, kCallbackFn, kThisArg)                                    \
  /* ES6 #sec-array.prototype.some */                                          \
  TFJ(ArraySomeLoopContinuation, 6)                                            \
  TFJ(ArraySome, 2, kCallbackFn, kThisArg)                                     \
  /* ES6 #sec-array.prototype.entries */                                       \
  TFJ(ArrayPrototypeEntries, 0)                                                \
  /* ES6 #sec-array.prototype.keys */                                          \
  TFJ(ArrayPrototypeKeys, 0)                                                   \
  /* ES6 #sec-array.prototype.values */                                        \
  TFJ(ArrayPrototypeValues, 0)                                                 \
  /* ES6 #sec-%arrayiteratorprototype%.next */                                 \
  TFJ(ArrayIteratorPrototypeNext, 0)                                           \
                                                                               \
  /* ArrayBuffer */                                                            \
  CPP(ArrayBufferConstructor)                                                  \
  CPP(ArrayBufferConstructor_ConstructStub)                                    \
  CPP(ArrayBufferPrototypeGetByteLength)                                       \
  CPP(ArrayBufferIsView)                                                       \
  CPP(ArrayBufferPrototypeSlice)                                               \
                                                                               \
  /* AsyncFunction */                                                          \
  TFJ(AsyncFunctionAwaitCaught, 3, kGenerator, kAwaited, kOuterPromise)        \
  TFJ(AsyncFunctionAwaitUncaught, 3, kGenerator, kAwaited, kOuterPromise)      \
  TFJ(AsyncFunctionAwaitRejectClosure, 1, kSentError)                          \
  TFJ(AsyncFunctionAwaitResolveClosure, 1, kSentValue)                         \
  TFJ(AsyncFunctionPromiseCreate, 0)                                           \
  TFJ(AsyncFunctionPromiseRelease, 1, kPromise)                                \
                                                                               \
  /* Boolean */                                                                \
  CPP(BooleanConstructor)                                                      \
  CPP(BooleanConstructor_ConstructStub)                                        \
  /* ES6 #sec-boolean.prototype.tostring */                                    \
  TFJ(BooleanPrototypeToString, 0)                                             \
  /* ES6 #sec-boolean.prototype.valueof */                                     \
  TFJ(BooleanPrototypeValueOf, 0)                                              \
                                                                               \
  /* CallSite */                                                               \
  CPP(CallSitePrototypeGetColumnNumber)                                        \
  CPP(CallSitePrototypeGetEvalOrigin)                                          \
  CPP(CallSitePrototypeGetFileName)                                            \
  CPP(CallSitePrototypeGetFunction)                                            \
  CPP(CallSitePrototypeGetFunctionName)                                        \
  CPP(CallSitePrototypeGetLineNumber)                                          \
  CPP(CallSitePrototypeGetMethodName)                                          \
  CPP(CallSitePrototypeGetPosition)                                            \
  CPP(CallSitePrototypeGetScriptNameOrSourceURL)                               \
  CPP(CallSitePrototypeGetThis)                                                \
  CPP(CallSitePrototypeGetTypeName)                                            \
  CPP(CallSitePrototypeIsConstructor)                                          \
  CPP(CallSitePrototypeIsEval)                                                 \
  CPP(CallSitePrototypeIsNative)                                               \
  CPP(CallSitePrototypeIsToplevel)                                             \
  CPP(CallSitePrototypeToString)                                               \
                                                                               \
  /* DataView */                                                               \
  CPP(DataViewConstructor)                                                     \
  CPP(DataViewConstructor_ConstructStub)                                       \
  CPP(DataViewPrototypeGetBuffer)                                              \
  CPP(DataViewPrototypeGetByteLength)                                          \
  CPP(DataViewPrototypeGetByteOffset)                                          \
  CPP(DataViewPrototypeGetInt8)                                                \
  CPP(DataViewPrototypeSetInt8)                                                \
  CPP(DataViewPrototypeGetUint8)                                               \
  CPP(DataViewPrototypeSetUint8)                                               \
  CPP(DataViewPrototypeGetInt16)                                               \
  CPP(DataViewPrototypeSetInt16)                                               \
  CPP(DataViewPrototypeGetUint16)                                              \
  CPP(DataViewPrototypeSetUint16)                                              \
  CPP(DataViewPrototypeGetInt32)                                               \
  CPP(DataViewPrototypeSetInt32)                                               \
  CPP(DataViewPrototypeGetUint32)                                              \
  CPP(DataViewPrototypeSetUint32)                                              \
  CPP(DataViewPrototypeGetFloat32)                                             \
  CPP(DataViewPrototypeSetFloat32)                                             \
  CPP(DataViewPrototypeGetFloat64)                                             \
  CPP(DataViewPrototypeSetFloat64)                                             \
                                                                               \
  /* Date */                                                                   \
  CPP(DateConstructor)                                                         \
  CPP(DateConstructor_ConstructStub)                                           \
  /* ES6 #sec-date.prototype.getdate */                                        \
  TFJ(DatePrototypeGetDate, 0)                                                 \
  /* ES6 #sec-date.prototype.getday */                                         \
  TFJ(DatePrototypeGetDay, 0)                                                  \
  /* ES6 #sec-date.prototype.getfullyear */                                    \
  TFJ(DatePrototypeGetFullYear, 0)                                             \
  /* ES6 #sec-date.prototype.gethours */                                       \
  TFJ(DatePrototypeGetHours, 0)                                                \
  /* ES6 #sec-date.prototype.getmilliseconds */                                \
  TFJ(DatePrototypeGetMilliseconds, 0)                                         \
  /* ES6 #sec-date.prototype.getminutes */                                     \
  TFJ(DatePrototypeGetMinutes, 0)                                              \
  /* ES6 #sec-date.prototype.getmonth */                                       \
  TFJ(DatePrototypeGetMonth, 0)                                                \
  /* ES6 #sec-date.prototype.getseconds */                                     \
  TFJ(DatePrototypeGetSeconds, 0)                                              \
  /* ES6 #sec-date.prototype.gettime */                                        \
  TFJ(DatePrototypeGetTime, 0)                                                 \
  /* ES6 #sec-date.prototype.gettimezoneoffset */                              \
  TFJ(DatePrototypeGetTimezoneOffset, 0)                                       \
  /* ES6 #sec-date.prototype.getutcdate */                                     \
  TFJ(DatePrototypeGetUTCDate, 0)                                              \
  /* ES6 #sec-date.prototype.getutcday */                                      \
  TFJ(DatePrototypeGetUTCDay, 0)                                               \
  /* ES6 #sec-date.prototype.getutcfullyear */                                 \
  TFJ(DatePrototypeGetUTCFullYear, 0)                                          \
  /* ES6 #sec-date.prototype.getutchours */                                    \
  TFJ(DatePrototypeGetUTCHours, 0)                                             \
  /* ES6 #sec-date.prototype.getutcmilliseconds */                             \
  TFJ(DatePrototypeGetUTCMilliseconds, 0)                                      \
  /* ES6 #sec-date.prototype.getutcminutes */                                  \
  TFJ(DatePrototypeGetUTCMinutes, 0)                                           \
  /* ES6 #sec-date.prototype.getutcmonth */                                    \
  TFJ(DatePrototypeGetUTCMonth, 0)                                             \
  /* ES6 #sec-date.prototype.getutcseconds */                                  \
  TFJ(DatePrototypeGetUTCSeconds, 0)                                           \
  /* ES6 #sec-date.prototype.valueof */                                        \
  TFJ(DatePrototypeValueOf, 0)                                                 \
  /* ES6 #sec-date.prototype-@@toprimitive */                                  \
  TFJ(DatePrototypeToPrimitive, 1, kHint)                                      \
  CPP(DatePrototypeGetYear)                                                    \
  CPP(DatePrototypeSetYear)                                                    \
  CPP(DateNow)                                                                 \
  CPP(DateParse)                                                               \
  CPP(DatePrototypeSetDate)                                                    \
  CPP(DatePrototypeSetFullYear)                                                \
  CPP(DatePrototypeSetHours)                                                   \
  CPP(DatePrototypeSetMilliseconds)                                            \
  CPP(DatePrototypeSetMinutes)                                                 \
  CPP(DatePrototypeSetMonth)                                                   \
  CPP(DatePrototypeSetSeconds)                                                 \
  CPP(DatePrototypeSetTime)                                                    \
  CPP(DatePrototypeSetUTCDate)                                                 \
  CPP(DatePrototypeSetUTCFullYear)                                             \
  CPP(DatePrototypeSetUTCHours)                                                \
  CPP(DatePrototypeSetUTCMilliseconds)                                         \
  CPP(DatePrototypeSetUTCMinutes)                                              \
  CPP(DatePrototypeSetUTCMonth)                                                \
  CPP(DatePrototypeSetUTCSeconds)                                              \
  CPP(DatePrototypeToDateString)                                               \
  CPP(DatePrototypeToISOString)                                                \
  CPP(DatePrototypeToUTCString)                                                \
  CPP(DatePrototypeToString)                                                   \
  CPP(DatePrototypeToTimeString)                                               \
  CPP(DatePrototypeToJson)                                                     \
  CPP(DateUTC)                                                                 \
                                                                               \
  /* Error */                                                                  \
  CPP(ErrorConstructor)                                                        \
  CPP(ErrorCaptureStackTrace)                                                  \
  CPP(ErrorPrototypeToString)                                                  \
  CPP(MakeError)                                                               \
  CPP(MakeRangeError)                                                          \
  CPP(MakeSyntaxError)                                                         \
  CPP(MakeTypeError)                                                           \
  CPP(MakeURIError)                                                            \
                                                                               \
  /* Function */                                                               \
  CPP(FunctionConstructor)                                                     \
  ASM(FunctionPrototypeApply)                                                  \
  CPP(FunctionPrototypeBind)                                                   \
  /* ES6 #sec-function.prototype.bind */                                       \
  TFJ(FastFunctionPrototypeBind,                                               \
      SharedFunctionInfo::kDontAdaptArgumentsSentinel)                         \
  ASM(FunctionPrototypeCall)                                                   \
  /* ES6 #sec-function.prototype-@@hasinstance */                              \
  TFJ(FunctionPrototypeHasInstance, 1, kV)                                     \
  /* ES6 #sec-function.prototype.tostring */                                   \
  CPP(FunctionPrototypeToString)                                               \
                                                                               \
  /* Belongs to Objects but is a dependency of GeneratorPrototypeResume */     \
  TFS(CreateIterResultObject, BUILTIN, kNoExtraICState,                        \
      CreateIterResultObject, 1)                                               \
                                                                               \
  /* Generator and Async */                                                    \
  CPP(GeneratorFunctionConstructor)                                            \
  /* ES6 #sec-generator.prototype.next */                                      \
  TFJ(GeneratorPrototypeNext, 1, kValue)                                       \
  /* ES6 #sec-generator.prototype.return */                                    \
  TFJ(GeneratorPrototypeReturn, 1, kValue)                                     \
  /* ES6 #sec-generator.prototype.throw */                                     \
  TFJ(GeneratorPrototypeThrow, 1, kException)                                  \
  CPP(AsyncFunctionConstructor)                                                \
                                                                               \
  /* Global object */                                                          \
  CPP(GlobalDecodeURI)                                                         \
  CPP(GlobalDecodeURIComponent)                                                \
  CPP(GlobalEncodeURI)                                                         \
  CPP(GlobalEncodeURIComponent)                                                \
  CPP(GlobalEscape)                                                            \
  CPP(GlobalUnescape)                                                          \
  CPP(GlobalEval)                                                              \
  /* ES6 #sec-isfinite-number */                                               \
  TFJ(GlobalIsFinite, 1, kNumber)                                              \
  /* ES6 #sec-isnan-number */                                                  \
  TFJ(GlobalIsNaN, 1, kNumber)                                                 \
                                                                               \
  /* JSON */                                                                   \
  CPP(JsonParse)                                                               \
  CPP(JsonStringify)                                                           \
                                                                               \
  /* ICs */                                                                    \
  TFS(LoadIC, LOAD_IC, kNoExtraICState, LoadWithVector, 1)                     \
  TFS(LoadIC_Noninlined, BUILTIN, kNoExtraICState, LoadWithVector, 1)          \
  TFS(LoadICTrampoline, LOAD_IC, kNoExtraICState, Load, 1)                     \
  TFS(KeyedLoadIC, KEYED_LOAD_IC, kNoExtraICState, LoadWithVector, 1)          \
  TFS(KeyedLoadICTrampoline, KEYED_LOAD_IC, kNoExtraICState, Load, 1)          \
  TFS(StoreIC, STORE_IC, kNoExtraICState, StoreWithVector, 1)                  \
  TFS(StoreICTrampoline, STORE_IC, kNoExtraICState, Store, 1)                  \
  TFS(StoreICStrict, STORE_IC, kNoExtraICState, StoreWithVector, 1)            \
  TFS(StoreICStrictTrampoline, STORE_IC, kNoExtraICState, Store, 1)            \
  TFS(KeyedStoreIC, KEYED_STORE_IC, kNoExtraICState, StoreWithVector, 1)       \
  TFS(KeyedStoreICTrampoline, KEYED_STORE_IC, kNoExtraICState, Store, 1)       \
  TFS(KeyedStoreICStrict, KEYED_STORE_IC, kNoExtraICState, StoreWithVector, 1) \
  TFS(KeyedStoreICStrictTrampoline, KEYED_STORE_IC, kNoExtraICState, Store, 1) \
  TFS(LoadGlobalIC, LOAD_GLOBAL_IC, kNoExtraICState, LoadGlobalWithVector, 1)  \
  TFS(LoadGlobalICInsideTypeof, LOAD_GLOBAL_IC, kNoExtraICState,               \
      LoadGlobalWithVector, 1)                                                 \
  TFS(LoadGlobalICTrampoline, LOAD_GLOBAL_IC, kNoExtraICState, LoadGlobal, 1)  \
  TFS(LoadGlobalICInsideTypeofTrampoline, LOAD_GLOBAL_IC, kNoExtraICState,     \
      LoadGlobal, 1)                                                           \
                                                                               \
  /* Math */                                                                   \
  /* ES6 #sec-math.abs */                                                      \
  TFJ(MathAbs, 1, kX)                                                          \
  /* ES6 #sec-math.acos */                                                     \
  TFJ(MathAcos, 1, kX)                                                         \
  /* ES6 #sec-math.acosh */                                                    \
  TFJ(MathAcosh, 1, kX)                                                        \
  /* ES6 #sec-math.asin */                                                     \
  TFJ(MathAsin, 1, kX)                                                         \
  /* ES6 #sec-math.asinh */                                                    \
  TFJ(MathAsinh, 1, kX)                                                        \
  /* ES6 #sec-math.atan */                                                     \
  TFJ(MathAtan, 1, kX)                                                         \
  /* ES6 #sec-math.atanh */                                                    \
  TFJ(MathAtanh, 1, kX)                                                        \
  /* ES6 #sec-math.atan2 */                                                    \
  TFJ(MathAtan2, 2, kY, kX)                                                    \
  /* ES6 #sec-math.cbrt */                                                     \
  TFJ(MathCbrt, 1, kX)                                                         \
  /* ES6 #sec-math.ceil */                                                     \
  TFJ(MathCeil, 1, kX)                                                         \
  /* ES6 #sec-math.clz32 */                                                    \
  TFJ(MathClz32, 1, kX)                                                        \
  /* ES6 #sec-math.cos */                                                      \
  TFJ(MathCos, 1, kX)                                                          \
  /* ES6 #sec-math.cosh */                                                     \
  TFJ(MathCosh, 1, kX)                                                         \
  /* ES6 #sec-math.exp */                                                      \
  TFJ(MathExp, 1, kX)                                                          \
  /* ES6 #sec-math.expm1 */                                                    \
  TFJ(MathExpm1, 1, kX)                                                        \
  /* ES6 #sec-math.floor */                                                    \
  TFJ(MathFloor, 1, kX)                                                        \
  /* ES6 #sec-math.fround */                                                   \
  TFJ(MathFround, 1, kX)                                                       \
  /* ES6 #sec-math.hypot */                                                    \
  CPP(MathHypot)                                                               \
  /* ES6 #sec-math.imul */                                                     \
  TFJ(MathImul, 2, kX, kY)                                                     \
  /* ES6 #sec-math.log */                                                      \
  TFJ(MathLog, 1, kX)                                                          \
  /* ES6 #sec-math.log1p */                                                    \
  TFJ(MathLog1p, 1, kX)                                                        \
  /* ES6 #sec-math.log10 */                                                    \
  TFJ(MathLog10, 1, kX)                                                        \
  /* ES6 #sec-math.log2 */                                                     \
  TFJ(MathLog2, 1, kX)                                                         \
  /* ES6 #sec-math.max */                                                      \
  TFJ(MathMax, SharedFunctionInfo::kDontAdaptArgumentsSentinel)                \
  /* ES6 #sec-math.min */                                                      \
  TFJ(MathMin, SharedFunctionInfo::kDontAdaptArgumentsSentinel)                \
  /* ES6 #sec-math.pow */                                                      \
  TFJ(MathPow, 2, kBase, kExponent)                                            \
  /* ES6 #sec-math.random */                                                   \
  TFJ(MathRandom, 0)                                                           \
  /* ES6 #sec-math.round */                                                    \
  TFJ(MathRound, 1, kX)                                                        \
  /* ES6 #sec-math.sign */                                                     \
  TFJ(MathSign, 1, kX)                                                         \
  /* ES6 #sec-math.sin */                                                      \
  TFJ(MathSin, 1, kX)                                                          \
  /* ES6 #sec-math.sinh */                                                     \
  TFJ(MathSinh, 1, kX)                                                         \
  /* ES6 #sec-math.sqrt */                                                     \
  TFJ(MathTan, 1, kX)                                                          \
  /* ES6 #sec-math.tan */                                                      \
  TFJ(MathTanh, 1, kX)                                                         \
  /* ES6 #sec-math.tanh */                                                     \
  TFJ(MathSqrt, 1, kX)                                                         \
  /* ES6 #sec-math.trunc */                                                    \
  TFJ(MathTrunc, 1, kX)                                                        \
                                                                               \
  /* Number */                                                                 \
  /* ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Call]] case */        \
  ASM(NumberConstructor)                                                       \
  /* ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Construct]] case */   \
  ASM(NumberConstructor_ConstructStub)                                         \
  /* ES6 #sec-number.isfinite */                                               \
  TFJ(NumberIsFinite, 1, kNumber)                                              \
  /* ES6 #sec-number.isinteger */                                              \
  TFJ(NumberIsInteger, 1, kNumber)                                             \
  /* ES6 #sec-number.isnan */                                                  \
  TFJ(NumberIsNaN, 1, kNumber)                                                 \
  /* ES6 #sec-number.issafeinteger */                                          \
  TFJ(NumberIsSafeInteger, 1, kNumber)                                         \
  /* ES6 #sec-number.parsefloat */                                             \
  TFJ(NumberParseFloat, 1, kString)                                            \
  /* ES6 #sec-number.parseint */                                               \
  TFJ(NumberParseInt, 2, kString, kRadix)                                      \
  CPP(NumberPrototypeToExponential)                                            \
  CPP(NumberPrototypeToFixed)                                                  \
  CPP(NumberPrototypeToLocaleString)                                           \
  CPP(NumberPrototypeToPrecision)                                              \
  CPP(NumberPrototypeToString)                                                 \
  /* ES6 #sec-number.prototype.valueof */                                      \
  TFJ(NumberPrototypeValueOf, 0)                                               \
  TFS(Add, BUILTIN, kNoExtraICState, BinaryOp, 1)                              \
  TFS(Subtract, BUILTIN, kNoExtraICState, BinaryOp, 1)                         \
  TFS(Multiply, BUILTIN, kNoExtraICState, BinaryOp, 1)                         \
  TFS(Divide, BUILTIN, kNoExtraICState, BinaryOp, 1)                           \
  TFS(Modulus, BUILTIN, kNoExtraICState, BinaryOp, 1)                          \
  TFS(BitwiseAnd, BUILTIN, kNoExtraICState, BinaryOp, 1)                       \
  TFS(BitwiseOr, BUILTIN, kNoExtraICState, BinaryOp, 1)                        \
  TFS(BitwiseXor, BUILTIN, kNoExtraICState, BinaryOp, 1)                       \
  TFS(ShiftLeft, BUILTIN, kNoExtraICState, BinaryOp, 1)                        \
  TFS(ShiftRight, BUILTIN, kNoExtraICState, BinaryOp, 1)                       \
  TFS(ShiftRightLogical, BUILTIN, kNoExtraICState, BinaryOp, 1)                \
  TFS(LessThan, BUILTIN, kNoExtraICState, Compare, 1)                          \
  TFS(LessThanOrEqual, BUILTIN, kNoExtraICState, Compare, 1)                   \
  TFS(GreaterThan, BUILTIN, kNoExtraICState, Compare, 1)                       \
  TFS(GreaterThanOrEqual, BUILTIN, kNoExtraICState, Compare, 1)                \
  TFS(Equal, BUILTIN, kNoExtraICState, Compare, 1)                             \
  TFS(StrictEqual, BUILTIN, kNoExtraICState, Compare, 1)                       \
                                                                               \
  /* Object */                                                                 \
  CPP(ObjectAssign)                                                            \
  TFJ(ObjectCreate, 2)                                                         \
  CPP(ObjectDefineGetter)                                                      \
  CPP(ObjectDefineProperties)                                                  \
  CPP(ObjectDefineProperty)                                                    \
  CPP(ObjectDefineSetter)                                                      \
  CPP(ObjectEntries)                                                           \
  CPP(ObjectFreeze)                                                            \
  CPP(ObjectGetOwnPropertyDescriptor)                                          \
  CPP(ObjectGetOwnPropertyDescriptors)                                         \
  CPP(ObjectGetOwnPropertyNames)                                               \
  CPP(ObjectGetOwnPropertySymbols)                                             \
  CPP(ObjectGetPrototypeOf)                                                    \
  CPP(ObjectSetPrototypeOf)                                                    \
  /* ES6 section 19.1.3.2 Object.prototype.hasOwnProperty */                   \
  TFJ(ObjectHasOwnProperty, 1)                                                 \
  CPP(ObjectIs)                                                                \
  CPP(ObjectIsExtensible)                                                      \
  CPP(ObjectIsFrozen)                                                          \
  CPP(ObjectIsSealed)                                                          \
  CPP(ObjectKeys)                                                              \
  CPP(ObjectLookupGetter)                                                      \
  CPP(ObjectLookupSetter)                                                      \
  CPP(ObjectPreventExtensions)                                                 \
  /* ES6 section 19.1.3.6 Object.prototype.toString () */                      \
  TFJ(ObjectProtoToString, 0)                                                  \
  TFJ(ObjectPrototypeValueOf, 0)                                               \
  CPP(ObjectPrototypePropertyIsEnumerable)                                     \
  CPP(ObjectPrototypeGetProto)                                                 \
  CPP(ObjectPrototypeSetProto)                                                 \
  CPP(ObjectSeal)                                                              \
  CPP(ObjectValues)                                                            \
                                                                               \
  /* instanceof */                                                             \
  TFS(OrdinaryHasInstance, BUILTIN, kNoExtraICState, Compare, 1)               \
  TFS(InstanceOf, BUILTIN, kNoExtraICState, Compare, 1)                        \
                                                                               \
  /* for-in */                                                                 \
  TFS(ForInFilter, BUILTIN, kNoExtraICState, ForInFilter, 1)                   \
  TFS(ForInNext, BUILTIN, kNoExtraICState, ForInNext, 1)                       \
  TFS(ForInPrepare, BUILTIN, kNoExtraICState, ForInPrepare, 3)                 \
                                                                               \
  /* Promise */                                                                \
  TFJ(PromiseGetCapabilitiesExecutor, 2)                                       \
  TFJ(NewPromiseCapability, 2)                                                 \
  TFJ(PromiseConstructor, 1)                                                   \
  TFJ(PromiseInternalConstructor, 1)                                           \
  TFJ(IsPromise, 1)                                                            \
  TFJ(PromiseResolveClosure, 1)                                                \
  TFJ(PromiseRejectClosure, 1)                                                 \
  TFJ(PromiseThen, 2)                                                          \
  TFJ(PromiseCatch, 1)                                                         \
  TFJ(ResolvePromise, 2)                                                       \
  TFS(PromiseHandleReject, BUILTIN, kNoExtraICState, PromiseHandleReject, 1)   \
  TFJ(PromiseHandle, 5)                                                        \
  TFJ(PromiseResolve, 1)                                                       \
  TFJ(PromiseReject, 1)                                                        \
  TFJ(InternalPromiseReject, 3)                                                \
  TFJ(PromiseFinally, 1)                                                       \
  TFJ(PromiseThenFinally, 1)                                                   \
  TFJ(PromiseCatchFinally, 1)                                                  \
  TFJ(PromiseValueThunkFinally, 0)                                             \
  TFJ(PromiseThrowerFinally, 0)                                                \
                                                                               \
  /* Proxy */                                                                  \
  CPP(ProxyConstructor)                                                        \
  CPP(ProxyConstructor_ConstructStub)                                          \
                                                                               \
  /* Reflect */                                                                \
  ASM(ReflectApply)                                                            \
  ASM(ReflectConstruct)                                                        \
  CPP(ReflectDefineProperty)                                                   \
  CPP(ReflectDeleteProperty)                                                   \
  CPP(ReflectGet)                                                              \
  CPP(ReflectGetOwnPropertyDescriptor)                                         \
  CPP(ReflectGetPrototypeOf)                                                   \
  CPP(ReflectHas)                                                              \
  CPP(ReflectIsExtensible)                                                     \
  CPP(ReflectOwnKeys)                                                          \
  CPP(ReflectPreventExtensions)                                                \
  CPP(ReflectSet)                                                              \
  CPP(ReflectSetPrototypeOf)                                                   \
                                                                               \
  /* RegExp */                                                                 \
  TFS(RegExpPrototypeExecSlow, BUILTIN, kNoExtraICState,                       \
      RegExpPrototypeExecSlow, 1)                                              \
  CPP(RegExpCapture1Getter)                                                    \
  CPP(RegExpCapture2Getter)                                                    \
  CPP(RegExpCapture3Getter)                                                    \
  CPP(RegExpCapture4Getter)                                                    \
  CPP(RegExpCapture5Getter)                                                    \
  CPP(RegExpCapture6Getter)                                                    \
  CPP(RegExpCapture7Getter)                                                    \
  CPP(RegExpCapture8Getter)                                                    \
  CPP(RegExpCapture9Getter)                                                    \
  TFJ(RegExpConstructor, 2)                                                    \
  TFJ(RegExpInternalMatch, 2)                                                  \
  CPP(RegExpInputGetter)                                                       \
  CPP(RegExpInputSetter)                                                       \
  CPP(RegExpLastMatchGetter)                                                   \
  CPP(RegExpLastParenGetter)                                                   \
  CPP(RegExpLeftContextGetter)                                                 \
  TFJ(RegExpPrototypeCompile, 2)                                               \
  TFJ(RegExpPrototypeExec, 1)                                                  \
  TFJ(RegExpPrototypeFlagsGetter, 0)                                           \
  TFJ(RegExpPrototypeGlobalGetter, 0)                                          \
  TFJ(RegExpPrototypeIgnoreCaseGetter, 0)                                      \
  TFJ(RegExpPrototypeMatch, 1)                                                 \
  TFJ(RegExpPrototypeMultilineGetter, 0)                                       \
  TFJ(RegExpPrototypeSearch, 1)                                                \
  TFJ(RegExpPrototypeSourceGetter, 0)                                          \
  TFJ(RegExpPrototypeStickyGetter, 0)                                          \
  TFJ(RegExpPrototypeTest, 1)                                                  \
  CPP(RegExpPrototypeToString)                                                 \
  TFJ(RegExpPrototypeUnicodeGetter, 0)                                         \
  CPP(RegExpRightContextGetter)                                                \
                                                                               \
  TFS(RegExpReplace, BUILTIN, kNoExtraICState, RegExpReplace, 1)               \
  TFJ(RegExpPrototypeReplace, 2)                                               \
                                                                               \
  TFS(RegExpSplit, BUILTIN, kNoExtraICState, RegExpSplit, 1)                   \
  TFJ(RegExpPrototypeSplit, 2)                                                 \
                                                                               \
  /* SharedArrayBuffer */                                                      \
  CPP(SharedArrayBufferPrototypeGetByteLength)                                 \
  TFJ(AtomicsLoad, 2)                                                          \
  TFJ(AtomicsStore, 3)                                                         \
  TFJ(AtomicsExchange, 3)                                                      \
  TFJ(AtomicsCompareExchange, 4)                                               \
  CPP(AtomicsAdd)                                                              \
  CPP(AtomicsSub)                                                              \
  CPP(AtomicsAnd)                                                              \
  CPP(AtomicsOr)                                                               \
  CPP(AtomicsXor)                                                              \
  CPP(AtomicsIsLockFree)                                                       \
  CPP(AtomicsWait)                                                             \
  CPP(AtomicsWake)                                                             \
                                                                               \
  /* String */                                                                 \
  ASM(StringConstructor)                                                       \
  ASM(StringConstructor_ConstructStub)                                         \
  CPP(StringFromCodePoint)                                                     \
  /* ES6 section 21.1.2.1 String.fromCharCode ( ...codeUnits ) */              \
  TFJ(StringFromCharCode, SharedFunctionInfo::kDontAdaptArgumentsSentinel)     \
  /* ES6 section 21.1.3.1 String.prototype.charAt ( pos ) */                   \
  TFJ(StringPrototypeCharAt, 1)                                                \
  /* ES6 section 21.1.3.2 String.prototype.charCodeAt ( pos ) */               \
  TFJ(StringPrototypeCharCodeAt, 1)                                            \
  /* ES6 section 21.1.3.6 */                                                   \
  /* String.prototype.endsWith ( searchString [ , endPosition ] ) */           \
  CPP(StringPrototypeEndsWith)                                                 \
  /* ES6 section 21.1.3.7 */                                                   \
  /* String.prototype.includes ( searchString [ , position ] ) */              \
  CPP(StringPrototypeIncludes)                                                 \
  /* ES6 section #sec-string.prototype.indexof */                              \
  /* String.prototype.indexOf ( searchString [ , position ] ) */               \
  TFJ(StringPrototypeIndexOf, SharedFunctionInfo::kDontAdaptArgumentsSentinel) \
  /* ES6 section 21.1.3.9 */                                                   \
  /* String.prototype.lastIndexOf ( searchString [ , position ] ) */           \
  CPP(StringPrototypeLastIndexOf)                                              \
  /* ES6 section 21.1.3.10 String.prototype.localeCompare ( that ) */          \
  CPP(StringPrototypeLocaleCompare)                                            \
  /* ES6 section 21.1.3.12 String.prototype.normalize ( [form] ) */            \
  CPP(StringPrototypeNormalize)                                                \
  /* ES6 section 21.1.3.16 String.prototype.replace ( search, replace ) */     \
  TFJ(StringPrototypeReplace, 2)                                               \
  /* ES6 section 21.1.3.19 String.prototype.split ( separator, limit )  */     \
  TFJ(StringPrototypeSplit, 2)                                                 \
  /* ES6 section B.2.3.1 String.prototype.substr ( start, length ) */          \
  TFJ(StringPrototypeSubstr, 2)                                                \
  /* ES6 section 21.1.3.19 String.prototype.substring ( start, end ) */        \
  TFJ(StringPrototypeSubstring, 2)                                             \
  /* ES6 section 21.1.3.20 */                                                  \
  /* String.prototype.startsWith ( searchString [ , position ] ) */            \
  CPP(StringPrototypeStartsWith)                                               \
  /* ES6 section 21.1.3.25 String.prototype.toString () */                     \
  TFJ(StringPrototypeToString, 0)                                              \
  /* ES #sec-string.prototype.tolocalelowercase */                             \
  CPP(StringPrototypeToLocaleLowerCase)                                        \
  /* ES #sec-string.prototype.tolocaleuppercase */                             \
  CPP(StringPrototypeToLocaleUpperCase)                                        \
  /* ES #sec-string.prototype.tolowercase */                                   \
  CPP(StringPrototypeToLowerCase)                                              \
  /* ES #sec-string.prototype.touppercase */                                   \
  CPP(StringPrototypeToUpperCase)                                              \
  CPP(StringPrototypeTrim)                                                     \
  CPP(StringPrototypeTrimLeft)                                                 \
  CPP(StringPrototypeTrimRight)                                                \
  /* ES6 section 21.1.3.28 String.prototype.valueOf () */                      \
  TFJ(StringPrototypeValueOf, 0)                                               \
  /* ES6 #sec-string.prototype-@@iterator */                                   \
  TFJ(StringPrototypeIterator, 0)                                              \
                                                                               \
  /* StringIterator */                                                         \
  TFJ(StringIteratorPrototypeNext, 0)                                          \
                                                                               \
  /* Symbol */                                                                 \
  CPP(SymbolConstructor)                                                       \
  CPP(SymbolConstructor_ConstructStub)                                         \
  /* ES6 section 19.4.2.1 Symbol.for */                                        \
  CPP(SymbolFor)                                                               \
  /* ES6 section 19.4.2.5 Symbol.keyFor */                                     \
  CPP(SymbolKeyFor)                                                            \
  /* ES6 section 19.4.3.4 Symbol.prototype [ @@toPrimitive ] ( hint ) */       \
  TFJ(SymbolPrototypeToPrimitive, 1)                                           \
  /* ES6 section 19.4.3.2 Symbol.prototype.toString ( ) */                     \
  TFJ(SymbolPrototypeToString, 0)                                              \
  /* ES6 section 19.4.3.3 Symbol.prototype.valueOf ( ) */                      \
  TFJ(SymbolPrototypeValueOf, 0)                                               \
                                                                               \
  /* TypedArray */                                                             \
  TFJ(TypedArrayConstructByArrayBuffer, 5)                                     \
  TFJ(TypedArrayConstructByLength, 3)                                          \
  TFJ(TypedArrayInitialize, 6)                                                 \
  CPP(TypedArrayPrototypeBuffer)                                               \
  /* ES6 section 22.2.3.2 get %TypedArray%.prototype.byteLength */             \
  TFJ(TypedArrayPrototypeByteLength, 0)                                        \
  /* ES6 section 22.2.3.3 get %TypedArray%.prototype.byteOffset */             \
  TFJ(TypedArrayPrototypeByteOffset, 0)                                        \
  /* ES6 section 22.2.3.18 get %TypedArray%.prototype.length */                \
  TFJ(TypedArrayPrototypeLength, 0)                                            \
  /* ES6 #sec-%typedarray%.prototype.entries */                                \
  TFJ(TypedArrayPrototypeEntries, 0)                                           \
  /* ES6 #sec-%typedarray%.prototype.keys */                                   \
  TFJ(TypedArrayPrototypeKeys, 0)                                              \
  /* ES6 #sec-%typedarray%.prototype.values */                                 \
  TFJ(TypedArrayPrototypeValues, 0)                                            \
  /* ES6 #sec-%typedarray%.prototype.copywithin */                             \
  CPP(TypedArrayPrototypeCopyWithin)                                           \
  /* ES6 #sec-%typedarray%.prototype.fill */                                   \
  CPP(TypedArrayPrototypeFill)                                                 \
  /* ES7 #sec-%typedarray%.prototype.includes */                               \
  CPP(TypedArrayPrototypeIncludes)                                             \
  /* ES6 #sec-%typedarray%.prototype.indexof */                                \
  CPP(TypedArrayPrototypeIndexOf)                                              \
  /* ES6 #sec-%typedarray%.prototype.indexof */                                \
  CPP(TypedArrayPrototypeLastIndexOf)                                          \
                                                                               \
  /* Wasm */                                                                   \
  ASM(WasmCompileLazy)                                                         \
  TFS(WasmStackGuard, BUILTIN, kNoExtraICState, WasmRuntimeCall, 1)            \
  TFS(ThrowWasmTrapUnreachable, BUILTIN, kNoExtraICState, WasmRuntimeCall, 1)  \
  TFS(ThrowWasmTrapMemOutOfBounds, BUILTIN, kNoExtraICState, WasmRuntimeCall,  \
      1)                                                                       \
  TFS(ThrowWasmTrapDivByZero, BUILTIN, kNoExtraICState, WasmRuntimeCall, 1)    \
  TFS(ThrowWasmTrapDivUnrepresentable, BUILTIN, kNoExtraICState,               \
      WasmRuntimeCall, 1)                                                      \
  TFS(ThrowWasmTrapRemByZero, BUILTIN, kNoExtraICState, WasmRuntimeCall, 1)    \
  TFS(ThrowWasmTrapFloatUnrepresentable, BUILTIN, kNoExtraICState,             \
      WasmRuntimeCall, 1)                                                      \
  TFS(ThrowWasmTrapFuncInvalid, BUILTIN, kNoExtraICState, WasmRuntimeCall, 1)  \
  TFS(ThrowWasmTrapFuncSigMismatch, BUILTIN, kNoExtraICState, WasmRuntimeCall, \
      1)                                                                       \
                                                                               \
  /* Async-from-Sync Iterator */                                               \
                                                                               \
  /* %AsyncFromSyncIteratorPrototype% */                                       \
  /* (proposal-async-iteration/#sec-%asyncfromsynciteratorprototype%-object)*/ \
  TFJ(AsyncFromSyncIteratorPrototypeNext, 1)                                   \
  TFJ(AsyncFromSyncIteratorPrototypeThrow, 1)                                  \
  TFJ(AsyncFromSyncIteratorPrototypeReturn, 1)                                 \
                                                                               \
  /* proposal-async-iteration/#sec-async-iterator-value-unwrap-functions */    \
  TFJ(AsyncIteratorValueUnwrap, 1)

#define BUILTIN_PROMISE_REJECTION_PREDICTION_LIST(V) \
  V(AsyncFromSyncIteratorPrototypeNext)              \
  V(AsyncFromSyncIteratorPrototypeReturn)            \
  V(AsyncFromSyncIteratorPrototypeThrow)             \
  V(AsyncFunctionAwaitCaught)                        \
  V(AsyncFunctionAwaitUncaught)                      \
  V(PromiseConstructor)                              \
  V(PromiseHandle)                                   \
  V(PromiseResolve)                                  \
  V(PromiseResolveClosure)                           \
  V(ResolvePromise)

#define BUILTIN_EXCEPTION_CAUGHT_PREDICTION_LIST(V) V(PromiseHandleReject)

#define IGNORE_BUILTIN(...)

#define BUILTIN_LIST_ALL(V) BUILTIN_LIST(V, V, V, V, V, V, V)

#define BUILTIN_LIST_TFS(V)                                       \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, V, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN)

#define BUILTIN_LIST_C(V)                                            \
  BUILTIN_LIST(V, V, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN)

#define BUILTIN_LIST_A(V)                                                      \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               V, V, V)

#define BUILTIN_LIST_DBG(V)                                                    \
  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, IGNORE_BUILTIN, \
               IGNORE_BUILTIN, IGNORE_BUILTIN, V)

#define BUILTINS_WITH_UNTAGGED_PARAMS(V) V(WasmCompileLazy)

// Forward declarations.
class ObjectVisitor;
enum class InterpreterPushArgsMode : unsigned;
namespace compiler {
class CodeAssemblerState;
}

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

  enum Name : int32_t {
#define DEF_ENUM(Name, ...) k##Name,
    BUILTIN_LIST_ALL(DEF_ENUM)
#undef DEF_ENUM
        builtin_count
  };

#define DECLARE_BUILTIN_ACCESSOR(Name, ...) \
  V8_EXPORT_PRIVATE Handle<Code> Name();
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
  Handle<Code> InterpreterPushArgsAndCall(TailCallMode tail_call_mode,
                                          InterpreterPushArgsMode mode);
  Handle<Code> InterpreterPushArgsAndConstruct(InterpreterPushArgsMode mode);
  Handle<Code> NewFunctionContext(ScopeType scope_type);
  Handle<Code> NewCloneShallowArray(AllocationSiteMode allocation_mode);
  Handle<Code> NewCloneShallowObject(int length);

  Code* builtin(Name name) {
    // Code::cast cannot be used here since we access builtins
    // during the marking phase of mark sweep. See IC::Clear.
    return reinterpret_cast<Code*>(builtins_[name]);
  }

  Address builtin_address(Name name) {
    return reinterpret_cast<Address>(&builtins_[name]);
  }

  static Callable CallableFor(Isolate* isolate, Name name);

  static const char* name(int index);

  // Returns the C++ entry point for builtins implemented in C++, and the null
  // Address otherwise.
  static Address CppEntryOf(int index);

  static bool IsCpp(int index);
  static bool IsApi(int index);
  static bool HasCppImplementation(int index);

  bool is_initialized() const { return initialized_; }

  MUST_USE_RESULT static MaybeHandle<Object> InvokeApiFunction(
      Isolate* isolate, bool is_construct, Handle<HeapObject> function,
      Handle<Object> receiver, int argc, Handle<Object> args[],
      Handle<HeapObject> new_target);

  enum ExitFrameType { EXIT, BUILTIN_EXIT };

  static void Generate_Adaptor(MacroAssembler* masm, Address builtin_address,
                               ExitFrameType exit_frame_type);

  static bool AllowDynamicFunction(Isolate* isolate, Handle<JSFunction> target,
                                   Handle<JSObject> target_global_proxy);

 private:
  Builtins();

  static void Generate_CallFunction(MacroAssembler* masm,
                                    ConvertReceiverMode mode,
                                    TailCallMode tail_call_mode);

  static void Generate_CallBoundFunctionImpl(MacroAssembler* masm,
                                             TailCallMode tail_call_mode);

  static void Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode,
                            TailCallMode tail_call_mode);
  static void Generate_CallForwardVarargs(MacroAssembler* masm,
                                          Handle<Code> code);

  static void Generate_InterpreterPushArgsAndCallImpl(
      MacroAssembler* masm, TailCallMode tail_call_mode,
      InterpreterPushArgsMode mode);

  static void Generate_InterpreterPushArgsAndConstructImpl(
      MacroAssembler* masm, InterpreterPushArgsMode mode);

#define DECLARE_ASM(Name, ...) \
  static void Generate_##Name(MacroAssembler* masm);
#define DECLARE_TF(Name, ...) \
  static void Generate_##Name(compiler::CodeAssemblerState* state);

  BUILTIN_LIST(IGNORE_BUILTIN, IGNORE_BUILTIN, DECLARE_TF, DECLARE_TF,
               DECLARE_ASM, DECLARE_ASM, DECLARE_ASM)

#undef DECLARE_ASM
#undef DECLARE_TF

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
