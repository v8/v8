// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-factory.h"

#include "src/bootstrapper.h"
#include "src/ic/ic.h"

namespace v8 {
namespace internal {


// static
Callable CodeFactory::LoadIC(Isolate* isolate) {
  if (FLAG_tf_load_ic_stub) {
    LoadICTrampolineTFStub stub(isolate);
    return Callable(stub.GetCode(), LoadDescriptor(isolate));
  }
  LoadICTrampolineStub stub(isolate);
  return Callable(stub.GetCode(), LoadDescriptor(isolate));
}

// static
Callable CodeFactory::ApiGetter(Isolate* isolate) {
  CallApiGetterStub stub(isolate);
  return Callable(stub.GetCode(), ApiGetterDescriptor(isolate));
}

// static
Callable CodeFactory::LoadICInOptimizedCode(Isolate* isolate) {
  auto code = LoadIC::initialize_stub_in_optimized_code(isolate);
  return Callable(code, LoadWithVectorDescriptor(isolate));
}

// static
Callable CodeFactory::LoadGlobalIC(Isolate* isolate, TypeofMode typeof_mode) {
  LoadGlobalICTrampolineStub stub(isolate, LoadGlobalICState(typeof_mode));
  return Callable(stub.GetCode(), LoadGlobalDescriptor(isolate));
}

// static
Callable CodeFactory::LoadGlobalICInOptimizedCode(Isolate* isolate,
                                                  TypeofMode typeof_mode) {
  auto code = LoadGlobalIC::initialize_stub_in_optimized_code(
      isolate, LoadGlobalICState(typeof_mode).GetExtraICState());
  return Callable(code, LoadGlobalWithVectorDescriptor(isolate));
}

// static
Callable CodeFactory::KeyedLoadIC(Isolate* isolate) {
  KeyedLoadICTrampolineStub stub(isolate);
  return Callable(stub.GetCode(), LoadDescriptor(isolate));
}


// static
Callable CodeFactory::KeyedLoadICInOptimizedCode(Isolate* isolate) {
  auto code =
      KeyedLoadIC::initialize_stub_in_optimized_code(isolate, kNoExtraICState);
  return Callable(code, LoadWithVectorDescriptor(isolate));
}


// static
Callable CodeFactory::CallIC(Isolate* isolate, int argc,
                             ConvertReceiverMode mode,
                             TailCallMode tail_call_mode) {
  CallICTrampolineStub stub(isolate, CallICState(argc, mode, tail_call_mode));
  return Callable(stub.GetCode(), CallFunctionWithFeedbackDescriptor(isolate));
}


// static
Callable CodeFactory::CallICInOptimizedCode(Isolate* isolate, int argc,
                                            ConvertReceiverMode mode,
                                            TailCallMode tail_call_mode) {
  return Callable(CallIC::initialize_stub_in_optimized_code(isolate, argc, mode,
                                                            tail_call_mode),
                  CallFunctionWithFeedbackAndVectorDescriptor(isolate));
}


// static
Callable CodeFactory::StoreIC(Isolate* isolate, LanguageMode language_mode) {
  VectorStoreICTrampolineStub stub(isolate, StoreICState(language_mode));
  return Callable(stub.GetCode(), VectorStoreICTrampolineDescriptor(isolate));
}


// static
Callable CodeFactory::StoreICInOptimizedCode(Isolate* isolate,
                                             LanguageMode language_mode) {
  CallInterfaceDescriptor descriptor = VectorStoreICDescriptor(isolate);
  return Callable(
      StoreIC::initialize_stub_in_optimized_code(isolate, language_mode),
      descriptor);
}


// static
Callable CodeFactory::KeyedStoreIC(Isolate* isolate,
                                   LanguageMode language_mode) {
  VectorKeyedStoreICTrampolineStub stub(isolate, StoreICState(language_mode));
  return Callable(stub.GetCode(), VectorStoreICTrampolineDescriptor(isolate));
}


// static
Callable CodeFactory::KeyedStoreICInOptimizedCode(Isolate* isolate,
                                                  LanguageMode language_mode) {
  CallInterfaceDescriptor descriptor = VectorStoreICDescriptor(isolate);
  return Callable(
      KeyedStoreIC::initialize_stub_in_optimized_code(isolate, language_mode),
      descriptor);
}


// static
Callable CodeFactory::CompareIC(Isolate* isolate, Token::Value op) {
  Handle<Code> code = CompareIC::GetUninitialized(isolate, op);
  return Callable(code, CompareDescriptor(isolate));
}


// static
Callable CodeFactory::BinaryOpIC(Isolate* isolate, Token::Value op) {
  BinaryOpICStub stub(isolate, op);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::InstanceOf(Isolate* isolate) {
  InstanceOfStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ToBoolean(Isolate* isolate) {
  ToBooleanStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ToNumber(Isolate* isolate) {
  return Callable(isolate->builtins()->ToNumber(),
                  TypeConversionDescriptor(isolate));
}


// static
Callable CodeFactory::NonNumberToNumber(Isolate* isolate) {
  return Callable(isolate->builtins()->NonNumberToNumber(),
                  TypeConversionDescriptor(isolate));
}

// static
Callable CodeFactory::StringToNumber(Isolate* isolate) {
  return Callable(isolate->builtins()->StringToNumber(),
                  TypeConversionDescriptor(isolate));
}

// static
Callable CodeFactory::ToString(Isolate* isolate) {
  ToStringStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ToName(Isolate* isolate) {
  ToNameStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ToInteger(Isolate* isolate) {
  ToIntegerStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::ToLength(Isolate* isolate) {
  ToLengthStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ToObject(Isolate* isolate) {
  ToObjectStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::NumberToString(Isolate* isolate) {
  NumberToStringStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::RegExpConstructResult(Isolate* isolate) {
  RegExpConstructResultStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::RegExpExec(Isolate* isolate) {
  RegExpExecStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Add(Isolate* isolate) {
  AddStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Subtract(Isolate* isolate) {
  SubtractStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Multiply(Isolate* isolate) {
  MultiplyStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Divide(Isolate* isolate) {
  DivideStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Modulus(Isolate* isolate) {
  ModulusStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::ShiftRight(Isolate* isolate) {
  ShiftRightStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::ShiftRightLogical(Isolate* isolate) {
  ShiftRightLogicalStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::ShiftLeft(Isolate* isolate) {
  ShiftLeftStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::BitwiseAnd(Isolate* isolate) {
  BitwiseAndStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::BitwiseOr(Isolate* isolate) {
  BitwiseOrStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::BitwiseXor(Isolate* isolate) {
  BitwiseXorStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Inc(Isolate* isolate) {
  IncStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Dec(Isolate* isolate) {
  DecStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::LessThan(Isolate* isolate) {
  LessThanStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::LessThanOrEqual(Isolate* isolate) {
  LessThanOrEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::GreaterThan(Isolate* isolate) {
  GreaterThanStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::GreaterThanOrEqual(Isolate* isolate) {
  GreaterThanOrEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::Equal(Isolate* isolate) {
  EqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::NotEqual(Isolate* isolate) {
  NotEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StrictEqual(Isolate* isolate) {
  StrictEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StrictNotEqual(Isolate* isolate) {
  StrictNotEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringAdd(Isolate* isolate, StringAddFlags flags,
                                PretenureFlag pretenure_flag) {
  StringAddStub stub(isolate, flags, pretenure_flag);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringCompare(Isolate* isolate, Token::Value token) {
  switch (token) {
    case Token::EQ:
    case Token::EQ_STRICT:
      return StringEqual(isolate);
    case Token::NE:
    case Token::NE_STRICT:
      return StringNotEqual(isolate);
    case Token::LT:
      return StringLessThan(isolate);
    case Token::GT:
      return StringGreaterThan(isolate);
    case Token::LTE:
      return StringLessThanOrEqual(isolate);
    case Token::GTE:
      return StringGreaterThanOrEqual(isolate);
    default:
      break;
  }
  UNREACHABLE();
  return StringEqual(isolate);
}

// static
Callable CodeFactory::StringEqual(Isolate* isolate) {
  StringEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringNotEqual(Isolate* isolate) {
  StringNotEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringLessThan(Isolate* isolate) {
  StringLessThanStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringLessThanOrEqual(Isolate* isolate) {
  StringLessThanOrEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringGreaterThan(Isolate* isolate) {
  StringGreaterThanStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::StringGreaterThanOrEqual(Isolate* isolate) {
  StringGreaterThanOrEqualStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::SubString(Isolate* isolate) {
  SubStringStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::ResumeGenerator(Isolate* isolate) {
  return Callable(isolate->builtins()->ResumeGeneratorTrampoline(),
                  ResumeGeneratorDescriptor(isolate));
}

// static
Callable CodeFactory::Typeof(Isolate* isolate) {
  TypeofStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastCloneRegExp(Isolate* isolate) {
  FastCloneRegExpStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastCloneShallowArray(Isolate* isolate) {
  // TODO(mstarzinger): Thread through AllocationSiteMode at some point.
  FastCloneShallowArrayStub stub(isolate, DONT_TRACK_ALLOCATION_SITE);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastCloneShallowObject(Isolate* isolate, int length) {
  FastCloneShallowObjectStub stub(isolate, length);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewContext(Isolate* isolate, int slot_count) {
  FastNewContextStub stub(isolate, slot_count);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewClosure(Isolate* isolate,
                                     LanguageMode language_mode,
                                     FunctionKind kind) {
  FastNewClosureStub stub(isolate, language_mode, kind);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewObject(Isolate* isolate) {
  FastNewObjectStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewRestParameter(Isolate* isolate,
                                           bool skip_stub_frame) {
  FastNewRestParameterStub stub(isolate, skip_stub_frame);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewSloppyArguments(Isolate* isolate,
                                             bool skip_stub_frame) {
  FastNewSloppyArgumentsStub stub(isolate, skip_stub_frame);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::FastNewStrictArguments(Isolate* isolate,
                                             bool skip_stub_frame) {
  FastNewStrictArgumentsStub stub(isolate, skip_stub_frame);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}


// static
Callable CodeFactory::AllocateHeapNumber(Isolate* isolate) {
  AllocateHeapNumberStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

#define SIMD128_ALLOC(TYPE, Type, type, lane_count, lane_type)          \
  Callable CodeFactory::Allocate##Type(Isolate* isolate) {              \
    Allocate##Type##Stub stub(isolate);                                 \
    return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor()); \
  }
SIMD128_TYPES(SIMD128_ALLOC)
#undef SIMD128_ALLOC

// static
Callable CodeFactory::ArgumentAdaptor(Isolate* isolate) {
  return Callable(isolate->builtins()->ArgumentsAdaptorTrampoline(),
                  ArgumentAdaptorDescriptor(isolate));
}


// static
Callable CodeFactory::Call(Isolate* isolate, ConvertReceiverMode mode,
                           TailCallMode tail_call_mode) {
  return Callable(isolate->builtins()->Call(mode, tail_call_mode),
                  CallTrampolineDescriptor(isolate));
}


// static
Callable CodeFactory::CallFunction(Isolate* isolate, ConvertReceiverMode mode) {
  return Callable(isolate->builtins()->CallFunction(mode),
                  CallTrampolineDescriptor(isolate));
}


// static
Callable CodeFactory::Construct(Isolate* isolate) {
  return Callable(isolate->builtins()->Construct(),
                  ConstructTrampolineDescriptor(isolate));
}


// static
Callable CodeFactory::ConstructFunction(Isolate* isolate) {
  return Callable(isolate->builtins()->ConstructFunction(),
                  ConstructTrampolineDescriptor(isolate));
}

// static
Callable CodeFactory::HasProperty(Isolate* isolate) {
  HasPropertyStub stub(isolate);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::MathPow(Isolate* isolate) {
  MathPowStub stub(isolate, MathPowStub::ON_STACK);
  return Callable(stub.GetCode(), stub.GetCallInterfaceDescriptor());
}

// static
Callable CodeFactory::InterpreterPushArgsAndCall(Isolate* isolate,
                                                 TailCallMode tail_call_mode) {
  return Callable(
      isolate->builtins()->InterpreterPushArgsAndCall(tail_call_mode),
      InterpreterPushArgsAndCallDescriptor(isolate));
}


// static
Callable CodeFactory::InterpreterPushArgsAndConstruct(Isolate* isolate) {
  return Callable(isolate->builtins()->InterpreterPushArgsAndConstruct(),
                  InterpreterPushArgsAndConstructDescriptor(isolate));
}


// static
Callable CodeFactory::InterpreterCEntry(Isolate* isolate, int result_size) {
  // Note: If we ever use fpregs in the interpreter then we will need to
  // save fpregs too.
  CEntryStub stub(isolate, result_size, kDontSaveFPRegs, kArgvInRegister);
  return Callable(stub.GetCode(), InterpreterCEntryDescriptor(isolate));
}

}  // namespace internal
}  // namespace v8
