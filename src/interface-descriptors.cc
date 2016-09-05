// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {


void CallInterfaceDescriptorData::InitializePlatformSpecific(
    int register_parameter_count, const Register* registers,
    PlatformInterfaceDescriptor* platform_descriptor) {
  platform_specific_descriptor_ = platform_descriptor;
  register_param_count_ = register_parameter_count;

  // InterfaceDescriptor owns a copy of the registers array.
  register_params_.reset(NewArray<Register>(register_parameter_count));
  for (int i = 0; i < register_parameter_count; i++) {
    register_params_[i] = registers[i];
  }
}

void CallInterfaceDescriptorData::InitializePlatformIndependent(
    int parameter_count, int extra_parameter_count,
    const MachineType* machine_types) {
  // InterfaceDescriptor owns a copy of the MachineType array.
  // We only care about parameters, not receiver and result.
  param_count_ = parameter_count + extra_parameter_count;
  machine_types_.reset(NewArray<MachineType>(param_count_));
  for (int i = 0; i < param_count_; i++) {
    if (machine_types == NULL || i >= parameter_count) {
      machine_types_[i] = MachineType::AnyTagged();
    } else {
      machine_types_[i] = machine_types[i];
    }
  }
}

const char* CallInterfaceDescriptor::DebugName(Isolate* isolate) const {
  CallInterfaceDescriptorData* start = isolate->call_descriptor_data(0);
  size_t index = data_ - start;
  DCHECK(index < CallDescriptors::NUMBER_OF_DESCRIPTORS);
  CallDescriptors::Key key = static_cast<CallDescriptors::Key>(index);
  switch (key) {
#define DEF_CASE(NAME)        \
  case CallDescriptors::NAME: \
    return #NAME " Descriptor";
    INTERFACE_DESCRIPTOR_LIST(DEF_CASE)
#undef DEF_CASE
    case CallDescriptors::NUMBER_OF_DESCRIPTORS:
      break;
  }
  return "";
}


void VoidDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(0, nullptr);
}

void FastNewFunctionContextDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void FastNewFunctionContextDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {FunctionRegister(), SlotsRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void LoadDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kReceiver, kName, kSlot
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::AnyTagged(),
                                 MachineType::TaggedSigned()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void LoadDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), NameRegister(), SlotRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void LoadGlobalDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kSlot
  MachineType machine_types[] = {MachineType::TaggedSigned()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void LoadGlobalDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {LoadWithVectorDescriptor::SlotRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void LoadGlobalWithVectorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kSlot, kVector
  MachineType machine_types[] = {MachineType::TaggedSigned(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void LoadGlobalWithVectorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {LoadWithVectorDescriptor::SlotRegister(),
                          LoadWithVectorDescriptor::VectorRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void StoreDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kReceiver, kName, kValue, kSlot
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(),
      MachineType::AnyTagged(), MachineType::TaggedSigned()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void StoreDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), NameRegister(), ValueRegister(),
                          SlotRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void StoreTransitionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), NameRegister(), ValueRegister(),
                          MapRegister()};

  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void VectorStoreTransitionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  if (SlotRegister().is(no_reg)) {
    Register registers[] = {ReceiverRegister(), NameRegister(), ValueRegister(),
                            MapRegister(), VectorRegister()};
    data->InitializePlatformSpecific(arraysize(registers), registers);
  } else {
    Register registers[] = {ReceiverRegister(), NameRegister(),
                            ValueRegister(),    MapRegister(),
                            SlotRegister(),     VectorRegister()};
    data->InitializePlatformSpecific(arraysize(registers), registers);
  }
}

void StoreTransitionDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kReceiver, kName, kValue, kMap
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(),
      MachineType::AnyTagged(), MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void StoreGlobalViaContextDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kSlot, kValue
  MachineType machine_types[] = {MachineType::Int32(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void StoreGlobalViaContextDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {SlotRegister(), ValueRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}


void StringCompareDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {LeftRegister(), RightRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void TypeConversionDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ArgumentRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void MathPowTaggedDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {exponent()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void MathPowIntegerDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {exponent()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void LoadWithVectorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kReceiver, kName, kSlot, kVector
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(),
      MachineType::TaggedSigned(), MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}


void LoadWithVectorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), NameRegister(), SlotRegister(),
                          VectorRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void VectorStoreTransitionDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  bool has_slot = !VectorStoreTransitionDescriptor::SlotRegister().is(no_reg);

  if (has_slot) {
    // kReceiver, kName, kValue, kMap, kSlot, kVector
    MachineType machine_types[] = {
        MachineType::AnyTagged(),    MachineType::AnyTagged(),
        MachineType::AnyTagged(),    MachineType::AnyTagged(),
        MachineType::TaggedSigned(), MachineType::AnyTagged()};
    data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                        machine_types);
  } else {
    // kReceiver, kName, kValue, kMap, kVector
    MachineType machine_types[] = {
        MachineType::AnyTagged(), MachineType::AnyTagged(),
        MachineType::AnyTagged(), MachineType::AnyTagged(),
        MachineType::AnyTagged()};
    data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                        machine_types);
  }
}

void StoreWithVectorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kReceiver, kName, kValue, kSlot, kVector
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(),
      MachineType::AnyTagged(), MachineType::TaggedSigned(),
      MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void StoreWithVectorDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), NameRegister(), ValueRegister(),
                          SlotRegister(), VectorRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void BinaryOpWithVectorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kLeft, kRight, kSlot, kVector
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::AnyTagged(), MachineType::Int32(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

const Register ApiGetterDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}

void ApiGetterDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ReceiverRegister(), HolderRegister(),
                          CallbackRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void ContextOnlyDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(0, nullptr);
}

CallInterfaceDescriptor OnStackArgsDescriptorBase::ForArgs(
    Isolate* isolate, int parameter_count) {
  switch (parameter_count) {
    case 1:
      return OnStackWith1ArgsDescriptor(isolate);
    case 2:
      return OnStackWith2ArgsDescriptor(isolate);
    case 3:
      return OnStackWith3ArgsDescriptor(isolate);
    case 4:
      return OnStackWith4ArgsDescriptor(isolate);
    case 5:
      return OnStackWith5ArgsDescriptor(isolate);
    case 6:
      return OnStackWith6ArgsDescriptor(isolate);
    case 7:
      return OnStackWith7ArgsDescriptor(isolate);
    default:
      UNREACHABLE();
      return VoidDescriptor(isolate);
  }
}

void OnStackArgsDescriptorBase::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformIndependent(0, extra_args(), NULL);
}

void OnStackArgsDescriptorBase::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  data->InitializePlatformSpecific(0, nullptr);
}

void GrowArrayElementsDescriptor::InitializePlatformSpecific(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ObjectRegister(), KeyRegister()};
  data->InitializePlatformSpecific(arraysize(registers), registers);
}

void VarArgFunctionDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kActualArgumentsCount
  MachineType machine_types[] = {MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void FastCloneRegExpDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kClosure, kLiteralIndex, kPattern, kFlags
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::TaggedSigned(),
      MachineType::AnyTagged(), MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void FastCloneShallowArrayDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kClosure, kLiteralIndex, kConstantElements
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::TaggedSigned(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void CreateAllocationSiteDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kVector, kSlot
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::TaggedSigned()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void CreateWeakCellDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kVector, kSlot, kValue
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::TaggedSigned(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void CallTrampolineDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kActualArgumentsCount
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ConstructStubDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kNewTarget, kActualArgumentsCount, kAllocationSite
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::AnyTagged(), MachineType::Int32(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ConstructTrampolineDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kNewTarget, kActualArgumentsCount
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(), MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void CallFunctionWithFeedbackDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kSlot
  MachineType machine_types[] = {MachineType::AnyTagged(),
                                 MachineType::TaggedSigned()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void CallFunctionWithFeedbackAndVectorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kSlot, kVector
  MachineType machine_types[] = {MachineType::TaggedPointer(),
                                 MachineType::TaggedSigned(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ArrayNoArgumentConstructorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kAllocationSite, kActualArgumentsCount, kFunctionParameter
  MachineType machine_types[] = {MachineType::TaggedPointer(),
                                 MachineType::AnyTagged(), MachineType::Int32(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ArraySingleArgumentConstructorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kAllocationSite, kActualArgumentsCount, kFunctionParameter,
  // kArraySizeSmiParameter
  MachineType machine_types[] = {
      MachineType::TaggedPointer(), MachineType::AnyTagged(),
      MachineType::Int32(), MachineType::AnyTagged(), MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ArrayNArgumentsConstructorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kAllocationSite, kActualArgumentsCount
  MachineType machine_types[] = {MachineType::TaggedPointer(),
                                 MachineType::AnyTagged(),
                                 MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

void ArgumentAdaptorDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kNewTarget, kActualArgumentsCount, kExpectedArgumentsCount
  MachineType machine_types[] = {MachineType::TaggedPointer(),
                                 MachineType::AnyTagged(), MachineType::Int32(),
                                 MachineType::Int32()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

CallInterfaceDescriptor ApiCallbackDescriptorBase::ForArgs(Isolate* isolate,
                                                           int argc) {
  switch (argc) {
    case 0:
      return ApiCallbackWith0ArgsDescriptor(isolate);
    case 1:
      return ApiCallbackWith1ArgsDescriptor(isolate);
    case 2:
      return ApiCallbackWith2ArgsDescriptor(isolate);
    case 3:
      return ApiCallbackWith3ArgsDescriptor(isolate);
    case 4:
      return ApiCallbackWith4ArgsDescriptor(isolate);
    case 5:
      return ApiCallbackWith5ArgsDescriptor(isolate);
    case 6:
      return ApiCallbackWith6ArgsDescriptor(isolate);
    case 7:
      return ApiCallbackWith7ArgsDescriptor(isolate);
    default:
      UNREACHABLE();
      return VoidDescriptor(isolate);
  }
}

void ApiCallbackDescriptorBase::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kFunction, kCallData, kHolder, kApiFunctionAddress
  MachineType machine_types[] = {
      MachineType::AnyTagged(), MachineType::AnyTagged(),
      MachineType::AnyTagged(), MachineType::Pointer()};
  data->InitializePlatformIndependent(arraysize(machine_types), extra_args(),
                                      machine_types);
}

void InterpreterDispatchDescriptor::InitializePlatformIndependent(
    CallInterfaceDescriptorData* data) {
  // kAccumulator, kBytecodeOffset, kBytecodeArray, kDispatchTable
  MachineType machine_types[] = {MachineType::AnyTagged(), MachineType::Int32(),
                                 MachineType::AnyTagged(),
                                 MachineType::AnyTagged()};
  data->InitializePlatformIndependent(arraysize(machine_types), 0,
                                      machine_types);
}

}  // namespace internal
}  // namespace v8
