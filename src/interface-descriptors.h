// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERFACE_DESCRIPTORS_H_
#define V8_INTERFACE_DESCRIPTORS_H_

#include <memory>

#include "src/assembler.h"
#include "src/globals.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

class PlatformInterfaceDescriptor;

#define INTERFACE_DESCRIPTOR_LIST(V)  \
  V(Allocate)                         \
  V(Void)                             \
  V(ContextOnly)                      \
  V(Load)                             \
  V(LoadWithVector)                   \
  V(LoadGlobal)                       \
  V(LoadGlobalWithVector)             \
  V(Store)                            \
  V(StoreWithVector)                  \
  V(StoreTransition)                  \
  V(StoreGlobal)                      \
  V(StoreGlobalWithVector)            \
  V(FastNewFunctionContext)           \
  V(FastNewObject)                    \
  V(RecordWrite)                      \
  V(TypeConversion)                   \
  V(TypeConversionStackParameter)     \
  V(Typeof)                           \
  V(CallFunction)                     \
  V(CallVarargs)                      \
  V(CallForwardVarargs)               \
  V(CallWithSpread)                   \
  V(CallWithArrayLike)                \
  V(CallTrampoline)                   \
  V(ConstructStub)                    \
  V(ConstructVarargs)                 \
  V(ConstructForwardVarargs)          \
  V(ConstructWithSpread)              \
  V(ConstructWithArrayLike)           \
  V(JSTrampoline)                     \
  V(AbortJS)                          \
  V(AllocateHeapNumber)               \
  V(ArrayConstructor)                 \
  V(ArrayNoArgumentConstructor)       \
  V(ArraySingleArgumentConstructor)   \
  V(ArrayNArgumentsConstructor)       \
  V(Compare)                          \
  V(BinaryOp)                         \
  V(StringAt)                         \
  V(StringSubstring)                  \
  V(GetProperty)                      \
  V(ArgumentAdaptor)                  \
  V(ApiCallback)                      \
  V(ApiGetter)                        \
  V(GrowArrayElements)                \
  V(NewArgumentsElements)             \
  V(InterpreterDispatch)              \
  V(InterpreterPushArgsThenCall)      \
  V(InterpreterPushArgsThenConstruct) \
  V(InterpreterCEntry)                \
  V(ResumeGenerator)                  \
  V(FrameDropperTrampoline)           \
  V(RunMicrotasks)                    \
  BUILTIN_LIST_TFS(V)

class V8_EXPORT_PRIVATE CallInterfaceDescriptorData {
 public:
  CallInterfaceDescriptorData() = default;

  // A copy of the passed in registers and param_representations is made
  // and owned by the CallInterfaceDescriptorData.

  void InitializePlatformSpecific(
      int register_parameter_count, const Register* registers,
      PlatformInterfaceDescriptor* platform_descriptor = nullptr);

  // if machine_types is null, then an array of size
  // (parameter_count + extra_parameter_count) will be created with
  // MachineType::AnyTagged() for each member.
  //
  // if machine_types is not null, then it should be of the size
  // parameter_count. Those members of the parameter array will be initialized
  // from {machine_types}, and the rest initialized to MachineType::AnyTagged().
  void InitializePlatformIndependent(int parameter_count,
                                     int extra_parameter_count,
                                     const MachineType* machine_types);

  void Reset();

  bool IsInitialized() const {
    return register_param_count_ >= 0 && param_count_ >= 0;
  }

  int param_count() const { return param_count_; }
  int register_param_count() const { return register_param_count_; }
  Register register_param(int index) const { return register_params_[index]; }
  Register* register_params() const { return register_params_; }
  MachineType param_type(int index) const { return machine_types_[index]; }
  PlatformInterfaceDescriptor* platform_specific_descriptor() const {
    return platform_specific_descriptor_;
  }

  void RestrictAllocatableRegisters(const Register* registers, int num) {
    DCHECK_EQ(allocatable_registers_, 0);
    for (int i = 0; i < num; ++i) {
      allocatable_registers_ |= registers[i].bit();
    }
    DCHECK_GT(NumRegs(allocatable_registers_), 0);
  }

  RegList allocatable_registers() const { return allocatable_registers_; }

 private:
  int register_param_count_ = -1;
  int param_count_ = -1;

  // Specifying the set of registers that could be used by the register
  // allocator. Currently, it's only used by RecordWrite code stub.
  RegList allocatable_registers_ = 0;

  // The Register params are allocated dynamically by the
  // InterfaceDescriptor, and freed on destruction. This is because static
  // arrays of Registers cause creation of runtime static initializers
  // which we don't want.
  Register* register_params_ = nullptr;
  MachineType* machine_types_ = nullptr;

  PlatformInterfaceDescriptor* platform_specific_descriptor_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(CallInterfaceDescriptorData);
};

class V8_EXPORT_PRIVATE CallDescriptors : public AllStatic {
 public:
  enum Key {
#define DEF_ENUM(name, ...) name,
    INTERFACE_DESCRIPTOR_LIST(DEF_ENUM)
#undef DEF_ENUM
        NUMBER_OF_DESCRIPTORS
  };

  static void InitializeOncePerProcess();
  static void TearDown();

  static CallInterfaceDescriptorData* call_descriptor_data(
      CallDescriptors::Key key) {
    return &call_descriptor_data_[key];
  }

  static Key GetKey(const CallInterfaceDescriptorData* data) {
    ptrdiff_t index = data - call_descriptor_data_;
    DCHECK_LE(0, index);
    DCHECK_LT(index, CallDescriptors::NUMBER_OF_DESCRIPTORS);
    return static_cast<CallDescriptors::Key>(index);
  }

 private:
  static CallInterfaceDescriptorData
      call_descriptor_data_[NUMBER_OF_DESCRIPTORS];
};

class V8_EXPORT_PRIVATE CallInterfaceDescriptor {
 public:
  CallInterfaceDescriptor() : data_(nullptr) {}
  virtual ~CallInterfaceDescriptor() {}

  CallInterfaceDescriptor(CallDescriptors::Key key)
      : data_(CallDescriptors::call_descriptor_data(key)) {}

  int GetParameterCount() const { return data()->param_count(); }

  int GetRegisterParameterCount() const {
    return data()->register_param_count();
  }

  int GetStackParameterCount() const {
    return data()->param_count() - data()->register_param_count();
  }

  Register GetRegisterParameter(int index) const {
    return data()->register_param(index);
  }

  MachineType GetParameterType(int index) const {
    DCHECK(index < data()->param_count());
    return data()->param_type(index);
  }

  // Some platforms have extra information to associate with the descriptor.
  PlatformInterfaceDescriptor* platform_specific_descriptor() const {
    return data()->platform_specific_descriptor();
  }

  RegList allocatable_registers() const {
    return data()->allocatable_registers();
  }

  static const Register ContextRegister();

  const char* DebugName() const;

 protected:
  const CallInterfaceDescriptorData* data() const { return data_; }

  virtual void InitializePlatformSpecific(CallInterfaceDescriptorData* data) {
    UNREACHABLE();
  }

  virtual void InitializePlatformIndependent(
      CallInterfaceDescriptorData* data) {
    data->InitializePlatformIndependent(data->register_param_count(), 0,
                                        nullptr);
  }

  // Initializes |data| using the platform dependent default set of registers.
  // It is intended to be used for TurboFan stubs when particular set of
  // registers does not matter.
  static void DefaultInitializePlatformSpecific(
      CallInterfaceDescriptorData* data, int register_parameter_count);

  // Initializes |data| using the platform dependent default set of registers
  // for JavaScript-compatible calling convention.
  // It is intended to be used for TurboFan stubs being called with JavaScript
  // linkage + additional parameters on registers and stack.
  static void JSDefaultInitializePlatformSpecific(
      CallInterfaceDescriptorData* data, int non_js_register_parameter_count);

 private:
  // {CallDescriptors} is allowed to call the private {Initialize} method.
  friend class CallDescriptors;

  const CallInterfaceDescriptorData* data_;

  void Initialize(CallInterfaceDescriptorData* data) {
    // The passed pointer should be a modifiable pointer to our own data.
    DCHECK_EQ(data, data_);
    DCHECK(!data->IsInitialized());
    InitializePlatformSpecific(data);
    InitializePlatformIndependent(data);
    DCHECK(data->IsInitialized());
  }
};

#define DECLARE_DESCRIPTOR_WITH_BASE(name, base) \
 public:                                         \
  explicit name() : base(key()) {}               \
  static inline CallDescriptors::Key key();

constexpr int kMaxBuiltinRegisterParams = 5;

#define DECLARE_DEFAULT_DESCRIPTOR(name, base, parameter_count)               \
  DECLARE_DESCRIPTOR_WITH_BASE(name, base)                                    \
 protected:                                                                   \
  static const int kRegisterParams =                                          \
      parameter_count > kMaxBuiltinRegisterParams ? kMaxBuiltinRegisterParams \
                                                  : parameter_count;          \
  static const int kStackParams = parameter_count - kRegisterParams;          \
  void InitializePlatformSpecific(CallInterfaceDescriptorData* data)          \
      override {                                                              \
    DefaultInitializePlatformSpecific(data, kRegisterParams);                 \
  }                                                                           \
  void InitializePlatformIndependent(CallInterfaceDescriptorData* data)       \
      override {                                                              \
    data->InitializePlatformIndependent(kRegisterParams, kStackParams,        \
                                        nullptr);                             \
  }                                                                           \
  name(CallDescriptors::Key key) : base(key) {}                               \
                                                                              \
 public:

#define DECLARE_JS_COMPATIBLE_DESCRIPTOR(name, base,                        \
                                         non_js_reg_parameters_count)       \
  DECLARE_DESCRIPTOR_WITH_BASE(name, base)                                  \
 protected:                                                                 \
  void InitializePlatformSpecific(CallInterfaceDescriptorData* data)        \
      override {                                                            \
    JSDefaultInitializePlatformSpecific(data, non_js_reg_parameters_count); \
  }                                                                         \
  name(CallDescriptors::Key key) : base(key) {}                             \
                                                                            \
 public:

#define DEFINE_JS_PARAMETER_TYPES(...)                                        \
  void InitializePlatformIndependent(CallInterfaceDescriptorData* data)       \
      override {                                                              \
    /* kTarget, kNewTarget, kActualArgumentsCount, */                         \
    MachineType machine_types[] = {MachineType::AnyTagged(),                  \
                                   MachineType::AnyTagged(),                  \
                                   MachineType::Int32(), __VA_ARGS__};        \
    static_assert(                                                            \
        kParameterCount == arraysize(machine_types),                          \
        "Parameter names definition is not consistent with parameter types"); \
    data->InitializePlatformIndependent(arraysize(machine_types), 0,          \
                                        machine_types);                       \
  }

#define DECLARE_DESCRIPTOR(name, base)                                         \
  DECLARE_DESCRIPTOR_WITH_BASE(name, base)                                     \
 protected:                                                                    \
  void InitializePlatformSpecific(CallInterfaceDescriptorData* data) override; \
  name(CallDescriptors::Key key) : base(key) {}                                \
                                                                               \
 public:

#define DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(name, base)        \
  DECLARE_DESCRIPTOR(name, base)                                        \
 protected:                                                             \
  void InitializePlatformIndependent(CallInterfaceDescriptorData* data) \
      override;                                                         \
                                                                        \
 public:

#define DEFINE_PARAMETERS(...)                            \
  enum ParameterIndices {                                 \
    __dummy = -1, /* to be able to pass zero arguments */ \
    ##__VA_ARGS__,                                        \
                                                          \
    kParameterCount,                                      \
    kContext = kParameterCount /* implicit parameter */   \
  };

#define DEFINE_JS_PARAMETERS(...)                       \
  enum ParameterIndices {                               \
    kTarget,                                            \
    kNewTarget,                                         \
    kActualArgumentsCount,                              \
    ##__VA_ARGS__,                                      \
                                                        \
    kParameterCount,                                    \
    kContext = kParameterCount /* implicit parameter */ \
  };

class V8_EXPORT_PRIVATE VoidDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(VoidDescriptor, CallInterfaceDescriptor)
};

class AllocateDescriptor : public CallInterfaceDescriptor {
 public:
  // No context parameter
  enum ParameterIndices { kRequestedSize, kParameterCount };
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(AllocateDescriptor,
                                               CallInterfaceDescriptor)
};

// This descriptor defines the JavaScript calling convention that can be used
// by stubs: target, new.target, argc (not including the receiver) and context
// are passed in registers while receiver and the rest of the JS arguments are
// passed on the stack.
class JSTrampolineDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_JS_PARAMETERS()
  DEFINE_JS_PARAMETER_TYPES();

  DECLARE_JS_COMPATIBLE_DESCRIPTOR(JSTrampolineDescriptor,
                                   CallInterfaceDescriptor, 0)
};

class ContextOnlyDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ContextOnlyDescriptor, CallInterfaceDescriptor)
};

// LoadDescriptor is used by all stubs that implement Load/KeyedLoad ICs.
class LoadDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kName, kSlot)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(LoadDescriptor,
                                               CallInterfaceDescriptor)

  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register SlotRegister();
};

class LoadGlobalDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kName, kSlot)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(LoadGlobalDescriptor,
                                               CallInterfaceDescriptor)

  static const Register NameRegister() {
    return LoadDescriptor::NameRegister();
  }

  static const Register SlotRegister() {
    return LoadDescriptor::SlotRegister();
  }
};

class StoreDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kSlot)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StoreDescriptor,
                                               CallInterfaceDescriptor)

  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register ValueRegister();
  static const Register SlotRegister();

#if V8_TARGET_ARCH_IA32
  static const bool kPassLastArgsOnStack = true;
#else
  static const bool kPassLastArgsOnStack = false;
#endif

  // Pass value and slot through the stack.
  static const int kStackArgumentsCount = kPassLastArgsOnStack ? 2 : 0;
};

class StoreTransitionDescriptor : public StoreDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kName, kMap, kValue, kSlot, kVector)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StoreTransitionDescriptor,
                                               StoreDescriptor)

  static const Register MapRegister();
  static const Register SlotRegister();
  static const Register VectorRegister();

  // Pass value, slot and vector through the stack.
  static const int kStackArgumentsCount = kPassLastArgsOnStack ? 3 : 0;
};

class StoreWithVectorDescriptor : public StoreDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kSlot, kVector)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StoreWithVectorDescriptor,
                                               StoreDescriptor)

  static const Register VectorRegister();

  // Pass value, slot and vector through the stack.
  static const int kStackArgumentsCount = kPassLastArgsOnStack ? 3 : 0;
};

class StoreGlobalDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kName, kValue, kSlot)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StoreGlobalDescriptor,
                                               CallInterfaceDescriptor)

  static const bool kPassLastArgsOnStack =
      StoreDescriptor::kPassLastArgsOnStack;
  // Pass value and slot through the stack.
  static const int kStackArgumentsCount = kPassLastArgsOnStack ? 2 : 0;

  static const Register NameRegister() {
    return StoreDescriptor::NameRegister();
  }

  static const Register ValueRegister() {
    return StoreDescriptor::ValueRegister();
  }

  static const Register SlotRegister() {
    return StoreDescriptor::SlotRegister();
  }
};

class StoreGlobalWithVectorDescriptor : public StoreGlobalDescriptor {
 public:
  DEFINE_PARAMETERS(kName, kValue, kSlot, kVector)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StoreGlobalWithVectorDescriptor,
                                               StoreGlobalDescriptor)

  static const Register VectorRegister() {
    return StoreWithVectorDescriptor::VectorRegister();
  }

  // Pass value, slot and vector through the stack.
  static const int kStackArgumentsCount = kPassLastArgsOnStack ? 3 : 0;
};

class LoadWithVectorDescriptor : public LoadDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kName, kSlot, kVector)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(LoadWithVectorDescriptor,
                                               LoadDescriptor)

  static const Register VectorRegister();
};

class LoadGlobalWithVectorDescriptor : public LoadGlobalDescriptor {
 public:
  DEFINE_PARAMETERS(kName, kSlot, kVector)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(LoadGlobalWithVectorDescriptor,
                                               LoadGlobalDescriptor)

  static const Register VectorRegister() {
    return LoadWithVectorDescriptor::VectorRegister();
  }
};

class FastNewFunctionContextDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kScopeInfo, kSlots)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(FastNewFunctionContextDescriptor,
                                               CallInterfaceDescriptor)

  static const Register ScopeInfoRegister();
  static const Register SlotsRegister();
};

class FastNewObjectDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kNewTarget)
  DECLARE_DESCRIPTOR(FastNewObjectDescriptor, CallInterfaceDescriptor)
  static const Register TargetRegister();
  static const Register NewTargetRegister();
};

class RecordWriteDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kObject, kSlot, kIsolate, kRememberedSet, kFPMode)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(RecordWriteDescriptor,
                                               CallInterfaceDescriptor)
};

class TypeConversionDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kArgument)
  DECLARE_DESCRIPTOR(TypeConversionDescriptor, CallInterfaceDescriptor)

  static const Register ArgumentRegister();
};

class TypeConversionStackParameterDescriptor final
    : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kArgument)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      TypeConversionStackParameterDescriptor, CallInterfaceDescriptor)
};

class GetPropertyDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kObject, kKey)
  DECLARE_DEFAULT_DESCRIPTOR(GetPropertyDescriptor, CallInterfaceDescriptor,
                             kParameterCount)
};

class TypeofDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kObject)
  DECLARE_DESCRIPTOR(TypeofDescriptor, CallInterfaceDescriptor)
};

class CallTrampolineDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kFunction, kActualArgumentsCount)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(CallTrampolineDescriptor,
                                               CallInterfaceDescriptor)
};

class CallVarargsDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kActualArgumentsCount, kArgumentsList,
                    kArgumentsLength)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(CallVarargsDescriptor,
                                               CallInterfaceDescriptor)
};

class CallForwardVarargsDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kActualArgumentsCount, kStartIndex)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(CallForwardVarargsDescriptor,
                                               CallInterfaceDescriptor)
};

class CallWithSpreadDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kArgumentsCount, kSpread)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(CallWithSpreadDescriptor,
                                               CallInterfaceDescriptor)
};

class CallWithArrayLikeDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kArgumentsList)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(CallWithArrayLikeDescriptor,
                                               CallInterfaceDescriptor)
};

class ConstructVarargsDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kNewTarget, kActualArgumentsCount, kArgumentsList,
                    kArgumentsLength)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ConstructVarargsDescriptor,
                                               CallInterfaceDescriptor)
};

class ConstructForwardVarargsDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kNewTarget, kActualArgumentsCount, kStartIndex)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      ConstructForwardVarargsDescriptor, CallInterfaceDescriptor)
};

class ConstructWithSpreadDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kNewTarget, kArgumentsCount, kSpread)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ConstructWithSpreadDescriptor,
                                               CallInterfaceDescriptor)
};

class ConstructWithArrayLikeDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTarget, kNewTarget, kArgumentsList)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ConstructWithArrayLikeDescriptor,
                                               CallInterfaceDescriptor)
};

class ConstructStubDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kFunction, kNewTarget, kActualArgumentsCount,
                    kAllocationSite)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ConstructStubDescriptor,
                                               CallInterfaceDescriptor)
};

class CallFunctionDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CallFunctionDescriptor, CallInterfaceDescriptor)
};

class AbortJSDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kObject)
  DECLARE_DESCRIPTOR(AbortJSDescriptor, CallInterfaceDescriptor)
};

class AllocateHeapNumberDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS()
  DECLARE_DESCRIPTOR(AllocateHeapNumberDescriptor, CallInterfaceDescriptor)
};

class ArrayConstructorDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_JS_PARAMETERS(kAllocationSite)
  DEFINE_JS_PARAMETER_TYPES(MachineType::AnyTagged());

  DECLARE_JS_COMPATIBLE_DESCRIPTOR(ArrayConstructorDescriptor,
                                   CallInterfaceDescriptor, 1)
};

class ArrayNArgumentsConstructorDescriptor : public CallInterfaceDescriptor {
 public:
  // This descriptor declares only register arguments while respective number
  // of JS arguments stay on the expression stack.
  // The ArrayNArgumentsConstructor builtin does not access stack arguments
  // directly it just forwards them to the runtime function.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      ArrayNArgumentsConstructorDescriptor, CallInterfaceDescriptor)
};

class ArrayNoArgumentConstructorDescriptor
    : public ArrayNArgumentsConstructorDescriptor {
 public:
  // This descriptor declares same register arguments as the parent
  // ArrayNArgumentsConstructorDescriptor and it declares indices for
  // JS arguments passed on the expression stack.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount,
                    kFunctionParameter)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      ArrayNoArgumentConstructorDescriptor,
      ArrayNArgumentsConstructorDescriptor)
};

class ArraySingleArgumentConstructorDescriptor
    : public ArrayNArgumentsConstructorDescriptor {
 public:
  // This descriptor declares same register arguments as the parent
  // ArrayNArgumentsConstructorDescriptor and it declares indices for
  // JS arguments passed on the expression stack.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount,
                    kFunctionParameter, kArraySizeSmiParameter)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      ArraySingleArgumentConstructorDescriptor,
      ArrayNArgumentsConstructorDescriptor)
};

class CompareDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kLeft, kRight)
  DECLARE_DESCRIPTOR(CompareDescriptor, CallInterfaceDescriptor)
};


class BinaryOpDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kLeft, kRight)
  DECLARE_DESCRIPTOR(BinaryOpDescriptor, CallInterfaceDescriptor)
};

// This desciptor is shared among String.p.charAt/charCodeAt/codePointAt
// as they all have the same interface.
class StringAtDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kPosition)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StringAtDescriptor,
                                               CallInterfaceDescriptor)
};

class StringSubstringDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kString, kFrom, kTo)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(StringSubstringDescriptor,
                                               CallInterfaceDescriptor)
};

class ArgumentAdaptorDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kFunction, kNewTarget, kActualArgumentsCount,
                    kExpectedArgumentsCount)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ArgumentAdaptorDescriptor,
                                               CallInterfaceDescriptor)
};

class ApiCallbackDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kTargetContext, kCallData, kHolder, kApiFunctionAddress)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(ApiCallbackDescriptor,
                                               CallInterfaceDescriptor)
};

class ApiGetterDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kReceiver, kHolder, kCallback)
  DECLARE_DESCRIPTOR(ApiGetterDescriptor, CallInterfaceDescriptor)

  static const Register ReceiverRegister();
  static const Register HolderRegister();
  static const Register CallbackRegister();
};

// TODO(turbofan): We should probably rename this to GrowFastElementsDescriptor.
class GrowArrayElementsDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kObject, kKey)
  DECLARE_DESCRIPTOR(GrowArrayElementsDescriptor, CallInterfaceDescriptor)

  static const Register ObjectRegister();
  static const Register KeyRegister();
};

class NewArgumentsElementsDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kFrame, kLength, kMappedCount)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(NewArgumentsElementsDescriptor,
                                               CallInterfaceDescriptor)
};

class V8_EXPORT_PRIVATE InterpreterDispatchDescriptor
    : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kAccumulator, kBytecodeOffset, kBytecodeArray,
                    kDispatchTable)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(InterpreterDispatchDescriptor,
                                               CallInterfaceDescriptor)
};

class InterpreterPushArgsThenCallDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kNumberOfArguments, kFirstArgument, kFunction)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      InterpreterPushArgsThenCallDescriptor, CallInterfaceDescriptor)
};

class InterpreterPushArgsThenConstructDescriptor
    : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kNumberOfArguments, kNewTarget, kConstructor,
                    kFeedbackElement, kFirstArgument)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(
      InterpreterPushArgsThenConstructDescriptor, CallInterfaceDescriptor)
};

class InterpreterCEntryDescriptor : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS(kNumberOfArguments, kFirstArgument, kFunctionEntry)
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(InterpreterCEntryDescriptor,
                                               CallInterfaceDescriptor)
};

class ResumeGeneratorDescriptor final : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ResumeGeneratorDescriptor, CallInterfaceDescriptor)
};

class FrameDropperTrampolineDescriptor final : public CallInterfaceDescriptor {
  DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE(FrameDropperTrampolineDescriptor,
                                               CallInterfaceDescriptor)
};

class RunMicrotasksDescriptor final : public CallInterfaceDescriptor {
 public:
  DEFINE_PARAMETERS()
  DECLARE_DEFAULT_DESCRIPTOR(RunMicrotasksDescriptor, CallInterfaceDescriptor,
                             0)
};

#define DEFINE_TFS_BUILTIN_DESCRIPTOR(Name, ...)                          \
  class Name##Descriptor : public CallInterfaceDescriptor {               \
   public:                                                                \
    DEFINE_PARAMETERS(__VA_ARGS__)                                        \
    DECLARE_DEFAULT_DESCRIPTOR(Name##Descriptor, CallInterfaceDescriptor, \
                               kParameterCount)                           \
  };
BUILTIN_LIST_TFS(DEFINE_TFS_BUILTIN_DESCRIPTOR)
#undef DEFINE_TFS_BUILTIN_DESCRIPTOR

#undef DECLARE_DEFAULT_DESCRIPTOR
#undef DECLARE_DESCRIPTOR_WITH_BASE
#undef DECLARE_DESCRIPTOR
#undef DECLARE_JS_COMPATIBLE_DESCRIPTOR
#undef DECLARE_DESCRIPTOR_WITH_CUSTOM_FUNCTION_TYPE
#undef DECLARE_DESCRIPTOR_WITH_BASE_AND_FUNCTION_TYPE_ARG
#undef DEFINE_PARAMETERS
#undef DEFINE_JS_PARAMETES
#undef DEFINE_JS_PARAMETER_TYPES

// We define the association between CallDescriptors::Key and the specialized
// descriptor here to reduce boilerplate and mistakes.
#define DEF_KEY(name, ...) \
  CallDescriptors::Key name##Descriptor::key() { return CallDescriptors::name; }
INTERFACE_DESCRIPTOR_LIST(DEF_KEY)
#undef DEF_KEY
}  // namespace internal
}  // namespace v8


#if V8_TARGET_ARCH_ARM64
#include "src/arm64/interface-descriptors-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/arm/interface-descriptors-arm.h"
#endif

#endif  // V8_INTERFACE_DESCRIPTORS_H_
