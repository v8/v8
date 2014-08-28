// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODE_STUBS_H_
#define V8_CODE_STUBS_H_

#include "src/allocation.h"
#include "src/assembler.h"
#include "src/codegen.h"
#include "src/globals.h"
#include "src/ic/ic.h"
#include "src/ic/ic-conventions.h"
#include "src/macro-assembler.h"
#include "src/ostreams.h"

namespace v8 {
namespace internal {

// List of code stubs used on all platforms.
#define CODE_STUB_LIST_ALL_PLATFORMS(V)     \
  V(CallFunction)                           \
  V(CallConstruct)                          \
  V(BinaryOpIC)                             \
  V(BinaryOpICWithAllocationSite)           \
  V(BinaryOpWithAllocationSite)             \
  V(StringAdd)                              \
  V(SubString)                              \
  V(StringCompare)                          \
  V(Compare)                                \
  V(CompareIC)                              \
  V(CompareNilIC)                           \
  V(MathPow)                                \
  V(CallIC)                                 \
  V(CallIC_Array)                           \
  V(FunctionPrototype)                      \
  V(RecordWrite)                            \
  V(StoreBufferOverflow)                    \
  V(RegExpExec)                             \
  V(Instanceof)                             \
  V(ConvertToDouble)                        \
  V(WriteInt32ToHeapNumber)                 \
  V(StackCheck)                             \
  V(Interrupt)                              \
  V(FastNewClosure)                         \
  V(FastNewContext)                         \
  V(FastCloneShallowArray)                  \
  V(FastCloneShallowObject)                 \
  V(CreateAllocationSite)                   \
  V(ToBoolean)                              \
  V(ToNumber)                               \
  V(ArgumentsAccess)                        \
  V(RegExpConstructResult)                  \
  V(NumberToString)                         \
  V(DoubleToI)                              \
  V(CEntry)                                 \
  V(JSEntry)                                \
  V(LoadElement)                            \
  V(KeyedLoadGeneric)                       \
  V(ArrayNoArgumentConstructor)             \
  V(ArraySingleArgumentConstructor)         \
  V(ArrayNArgumentsConstructor)             \
  V(InternalArrayNoArgumentConstructor)     \
  V(InternalArraySingleArgumentConstructor) \
  V(InternalArrayNArgumentsConstructor)     \
  V(StoreElement)                           \
  V(DebuggerStatement)                      \
  V(NameDictionaryLookup)                   \
  V(ElementsTransitionAndStore)             \
  V(TransitionElementsKind)                 \
  V(StoreArrayLiteralElement)               \
  V(StubFailureTrampoline)                  \
  V(ArrayConstructor)                       \
  V(InternalArrayConstructor)               \
  V(ProfileEntryHook)                       \
  V(StoreGlobal)                            \
  V(CallApiFunction)                        \
  V(CallApiGetter)                          \
  V(LoadICTrampoline)                       \
  V(VectorLoad)                             \
  V(KeyedLoadICTrampoline)                  \
  V(VectorKeyedLoad)                        \
  /* IC Handler stubs */                    \
  V(LoadField)                              \
  V(StoreField)                             \
  V(LoadConstant)                           \
  V(StringLength)

// List of code stubs only used on ARM 32 bits platforms.
#if V8_TARGET_ARCH_ARM
#define CODE_STUB_LIST_ARM(V)  \
  V(GetProperty)               \
  V(SetProperty)               \
  V(InvokeBuiltin)             \
  V(DirectCEntry)
#else
#define CODE_STUB_LIST_ARM(V)
#endif

// List of code stubs only used on ARM 64 bits platforms.
#if V8_TARGET_ARCH_ARM64
#define CODE_STUB_LIST_ARM64(V)  \
  V(GetProperty)               \
  V(SetProperty)               \
  V(InvokeBuiltin)             \
  V(DirectCEntry)              \
  V(StoreRegistersState)       \
  V(RestoreRegistersState)
#else
#define CODE_STUB_LIST_ARM64(V)
#endif

// List of code stubs only used on MIPS platforms.
#if V8_TARGET_ARCH_MIPS
#define CODE_STUB_LIST_MIPS(V)  \
  V(RegExpCEntry)               \
  V(DirectCEntry)               \
  V(StoreRegistersState)        \
  V(RestoreRegistersState)
#elif V8_TARGET_ARCH_MIPS64
#define CODE_STUB_LIST_MIPS(V)  \
  V(RegExpCEntry)               \
  V(DirectCEntry)               \
  V(StoreRegistersState)        \
  V(RestoreRegistersState)
#else
#define CODE_STUB_LIST_MIPS(V)
#endif

// Combined list of code stubs.
#define CODE_STUB_LIST(V)            \
  CODE_STUB_LIST_ALL_PLATFORMS(V)    \
  CODE_STUB_LIST_ARM(V)              \
  CODE_STUB_LIST_ARM64(V)           \
  CODE_STUB_LIST_MIPS(V)

// Stub is base classes of all stubs.
class CodeStub BASE_EMBEDDED {
 public:
  enum Major {
    UninitializedMajorKey = 0,
#define DEF_ENUM(name) name,
    CODE_STUB_LIST(DEF_ENUM)
#undef DEF_ENUM
    NoCache,  // marker for stubs that do custom caching
    NUMBER_OF_IDS
  };

  // Retrieve the code for the stub. Generate the code if needed.
  Handle<Code> GetCode();

  // Retrieve the code for the stub, make and return a copy of the code.
  Handle<Code> GetCodeCopy(const Code::FindAndReplacePattern& pattern);

  static Major MajorKeyFromKey(uint32_t key) {
    return static_cast<Major>(MajorKeyBits::decode(key));
  }
  static uint32_t MinorKeyFromKey(uint32_t key) {
    return MinorKeyBits::decode(key);
  }

  // Gets the major key from a code object that is a code stub or binary op IC.
  static Major GetMajorKey(Code* code_stub) {
    return MajorKeyFromKey(code_stub->stub_key());
  }

  static uint32_t NoCacheKey() { return MajorKeyBits::encode(NoCache); }

  static const char* MajorName(Major major_key, bool allow_unknown_keys);

  explicit CodeStub(Isolate* isolate) : minor_key_(0), isolate_(isolate) {}
  virtual ~CodeStub() {}

  static void GenerateStubsAheadOfTime(Isolate* isolate);
  static void GenerateFPStubs(Isolate* isolate);

  // Some stubs put untagged junk on the stack that cannot be scanned by the
  // GC.  This means that we must be statically sure that no GC can occur while
  // they are running.  If that is the case they should override this to return
  // true, which will cause an assertion if we try to call something that can
  // GC or if we try to put a stack frame on top of the junk, which would not
  // result in a traversable stack.
  virtual bool SometimesSetsUpAFrame() { return true; }

  // Lookup the code in the (possibly custom) cache.
  bool FindCodeInCache(Code** code_out);

  // Returns information for computing the number key.
  virtual Major MajorKey() const = 0;
  virtual uint32_t MinorKey() const { return minor_key_; }

  virtual InlineCacheState GetICState() const { return UNINITIALIZED; }
  virtual ExtraICState GetExtraICState() const { return kNoExtraICState; }
  virtual Code::StubType GetStubType() {
    return Code::NORMAL;
  }

  friend OStream& operator<<(OStream& os, const CodeStub& s) {
    s.PrintName(os);
    return os;
  }

  Isolate* isolate() const { return isolate_; }

 protected:
  // Generates the assembler code for the stub.
  virtual Handle<Code> GenerateCode() = 0;

  // Returns whether the code generated for this stub needs to be allocated as
  // a fixed (non-moveable) code object.
  virtual bool NeedsImmovableCode() { return false; }

  virtual void PrintName(OStream& os) const;        // NOLINT
  virtual void PrintBaseName(OStream& os) const;    // NOLINT
  virtual void PrintState(OStream& os) const { ; }  // NOLINT

  // Computes the key based on major and minor.
  uint32_t GetKey() {
    DCHECK(static_cast<int>(MajorKey()) < NUMBER_OF_IDS);
    return MinorKeyBits::encode(MinorKey()) | MajorKeyBits::encode(MajorKey());
  }

  uint32_t minor_key_;

 private:
  // Perform bookkeeping required after code generation when stub code is
  // initially generated.
  void RecordCodeGeneration(Handle<Code> code);

  // Finish the code object after it has been generated.
  virtual void FinishCode(Handle<Code> code) { }

  // Activate newly generated stub. Is called after
  // registering stub in the stub cache.
  virtual void Activate(Code* code) { }

  // BinaryOpStub needs to override this.
  virtual Code::Kind GetCodeKind() const;

  // Add the code to a specialized cache, specific to an individual
  // stub type. Please note, this method must add the code object to a
  // roots object, otherwise we will remove the code during GC.
  virtual void AddToSpecialCache(Handle<Code> new_object) { }

  // Find code in a specialized cache, work is delegated to the specific stub.
  virtual bool FindCodeInSpecialCache(Code** code_out) {
    return false;
  }

  // If a stub uses a special cache override this.
  virtual bool UseSpecialCache() { return false; }

  STATIC_ASSERT(NUMBER_OF_IDS < (1 << kStubMajorKeyBits));
  class MajorKeyBits: public BitField<uint32_t, 0, kStubMajorKeyBits> {};
  class MinorKeyBits: public BitField<uint32_t,
      kStubMajorKeyBits, kStubMinorKeyBits> {};  // NOLINT

  friend class BreakPointIterator;

  Isolate* isolate_;
};


class PlatformCodeStub : public CodeStub {
 public:
  explicit PlatformCodeStub(Isolate* isolate) : CodeStub(isolate) { }

  // Retrieve the code for the stub. Generate the code if needed.
  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual Code::Kind GetCodeKind() const { return Code::STUB; }

 protected:
  // Generates the assembler code for the stub.
  virtual void Generate(MacroAssembler* masm) = 0;
};


enum StubFunctionMode { NOT_JS_FUNCTION_STUB_MODE, JS_FUNCTION_STUB_MODE };
enum HandlerArgumentsMode { DONT_PASS_ARGUMENTS, PASS_ARGUMENTS };


class PlatformInterfaceDescriptor;


class InterfaceDescriptor {
 public:
  bool IsInitialized() const { return register_param_count_ >= 0; }

  int GetEnvironmentLength() const { return register_param_count_; }

  int GetRegisterParameterCount() const { return register_param_count_; }

  Register GetParameterRegister(int index) const {
    return register_params_[index];
  }

  Representation GetParameterRepresentation(int index) const {
    DCHECK(index < register_param_count_);
    if (register_param_representations_.get() == NULL) {
      return Representation::Tagged();
    }

    return register_param_representations_[index];
  }

  // "Environment" versions of parameter functions. The first register
  // parameter (context) is not included.
  int GetEnvironmentParameterCount() const {
    return GetEnvironmentLength() - 1;
  }

  Register GetEnvironmentParameterRegister(int index) const {
    return GetParameterRegister(index + 1);
  }

  Representation GetEnvironmentParameterRepresentation(int index) const {
    return GetParameterRepresentation(index + 1);
  }

  // Some platforms have extra information to associate with the descriptor.
  PlatformInterfaceDescriptor* platform_specific_descriptor() const {
    return platform_specific_descriptor_;
  }

  static const Register ContextRegister();

 protected:
  InterfaceDescriptor();
  virtual ~InterfaceDescriptor() {}

  void Initialize(int register_parameter_count, Register* registers,
                  Representation* register_param_representations,
                  PlatformInterfaceDescriptor* platform_descriptor = NULL);

 private:
  int register_param_count_;

  // The Register params are allocated dynamically by the
  // InterfaceDescriptor, and freed on destruction. This is because static
  // arrays of Registers cause creation of runtime static initializers
  // which we don't want.
  SmartArrayPointer<Register> register_params_;
  // Specifies Representations for the stub's parameter. Points to an array of
  // Representations of the same length of the numbers of parameters to the
  // stub, or if NULL (the default value), Representation of each parameter
  // assumed to be Tagged().
  SmartArrayPointer<Representation> register_param_representations_;

  PlatformInterfaceDescriptor* platform_specific_descriptor_;

  DISALLOW_COPY_AND_ASSIGN(InterfaceDescriptor);
};


class CodeStubInterfaceDescriptor: public InterfaceDescriptor {
 public:
  CodeStubInterfaceDescriptor();

  void Initialize(CodeStub::Major major, int register_parameter_count,
                  Register* registers, Address deoptimization_handler = NULL,
                  Representation* register_param_representations = NULL,
                  int hint_stack_parameter_count = -1,
                  StubFunctionMode function_mode = NOT_JS_FUNCTION_STUB_MODE);
  void Initialize(CodeStub::Major major, int register_parameter_count,
                  Register* registers, Register stack_parameter_count,
                  Address deoptimization_handler = NULL,
                  Representation* register_param_representations = NULL,
                  int hint_stack_parameter_count = -1,
                  StubFunctionMode function_mode = NOT_JS_FUNCTION_STUB_MODE,
                  HandlerArgumentsMode handler_mode = DONT_PASS_ARGUMENTS);

  void SetMissHandler(ExternalReference handler) {
    miss_handler_ = handler;
    has_miss_handler_ = true;
    // Our miss handler infrastructure doesn't currently support
    // variable stack parameter counts.
    DCHECK(!stack_parameter_count_.is_valid());
  }

  ExternalReference miss_handler() const {
    DCHECK(has_miss_handler_);
    return miss_handler_;
  }

  bool has_miss_handler() const {
    return has_miss_handler_;
  }

  bool IsEnvironmentParameterCountRegister(int index) const {
    return GetEnvironmentParameterRegister(index).is(stack_parameter_count_);
  }

  int GetHandlerParameterCount() const {
    int params = GetEnvironmentParameterCount();
    if (handler_arguments_mode_ == PASS_ARGUMENTS) {
      params += 1;
    }
    return params;
  }

  int hint_stack_parameter_count() const { return hint_stack_parameter_count_; }
  Register stack_parameter_count() const { return stack_parameter_count_; }
  StubFunctionMode function_mode() const { return function_mode_; }
  Address deoptimization_handler() const { return deoptimization_handler_; }
  CodeStub::Major MajorKey() const { return major_; }

 private:
  Register stack_parameter_count_;
  // If hint_stack_parameter_count_ > 0, the code stub can optimize the
  // return sequence. Default value is -1, which means it is ignored.
  int hint_stack_parameter_count_;
  StubFunctionMode function_mode_;

  Address deoptimization_handler_;
  HandlerArgumentsMode handler_arguments_mode_;

  ExternalReference miss_handler_;
  bool has_miss_handler_;
  CodeStub::Major major_;
};


class CallInterfaceDescriptor: public InterfaceDescriptor {
 public:
  CallInterfaceDescriptor() { }

  // A copy of the passed in registers and param_representations is made
  // and owned by the CallInterfaceDescriptor.

  // TODO(mvstanton): Instead of taking parallel arrays register and
  // param_representations, how about a struct that puts the representation
  // and register side by side (eg, RegRep(r1, Representation::Tagged()).
  // The same should go for the CodeStubInterfaceDescriptor class.
  void Initialize(int register_parameter_count, Register* registers,
                  Representation* param_representations,
                  PlatformInterfaceDescriptor* platform_descriptor = NULL);
};


class HydrogenCodeStub : public CodeStub {
 public:
  enum InitializationState {
    UNINITIALIZED,
    INITIALIZED
  };

  explicit HydrogenCodeStub(Isolate* isolate,
                            InitializationState state = INITIALIZED)
      : CodeStub(isolate) {
    minor_key_ = IsMissBits::encode(state == UNINITIALIZED);
  }

  virtual Code::Kind GetCodeKind() const { return Code::STUB; }

  CodeStubInterfaceDescriptor* GetInterfaceDescriptor() {
    return isolate()->code_stub_interface_descriptor(MajorKey());
  }

  template<class SubClass>
  static Handle<Code> GetUninitialized(Isolate* isolate) {
    SubClass::GenerateAheadOfTime(isolate);
    return SubClass().GetCode(isolate);
  }

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) = 0;

  // Retrieve the code for the stub. Generate the code if needed.
  virtual Handle<Code> GenerateCode() = 0;

  bool IsUninitialized() const { return IsMissBits::decode(minor_key_); }

  // TODO(yangguo): we use this temporarily to construct the minor key.
  //   We want to remove NotMissMinorKey methods one by one and eventually
  //   remove HydrogenStub::MinorKey and turn CodeStub::MinorKey into a
  //   non-virtual method that directly returns minor_key_.
  virtual int NotMissMinorKey() const {
    return SubMinorKeyBits::decode(minor_key_);
  }

  Handle<Code> GenerateLightweightMissCode();

  template<class StateType>
  void TraceTransition(StateType from, StateType to);

 protected:
  void set_sub_minor_key(uint32_t key) {
    minor_key_ = SubMinorKeyBits::update(minor_key_, key);
  }

  uint32_t sub_minor_key() const { return SubMinorKeyBits::decode(minor_key_); }

  static const int kSubMinorKeyBits = kStubMinorKeyBits - 1;

 private:
  class SubMinorKeyBits : public BitField<int, 0, kSubMinorKeyBits> {};
  class IsMissBits : public BitField<bool, kSubMinorKeyBits, 1> {};

  void GenerateLightweightMiss(MacroAssembler* masm);
  virtual uint32_t MinorKey() const {
    return IsMissBits::encode(IsUninitialized()) |
           SubMinorKeyBits::encode(NotMissMinorKey());
  }
};


// Helper interface to prepare to/restore after making runtime calls.
class RuntimeCallHelper {
 public:
  virtual ~RuntimeCallHelper() {}

  virtual void BeforeCall(MacroAssembler* masm) const = 0;

  virtual void AfterCall(MacroAssembler* masm) const = 0;

 protected:
  RuntimeCallHelper() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RuntimeCallHelper);
};


} }  // namespace v8::internal

#if V8_TARGET_ARCH_IA32
#include "src/ia32/code-stubs-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/x64/code-stubs-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/arm64/code-stubs-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/arm/code-stubs-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "src/mips/code-stubs-mips.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/mips64/code-stubs-mips64.h"
#elif V8_TARGET_ARCH_X87
#include "src/x87/code-stubs-x87.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {


// RuntimeCallHelper implementation used in stubs: enters/leaves a
// newly created internal frame before/after the runtime call.
class StubRuntimeCallHelper : public RuntimeCallHelper {
 public:
  StubRuntimeCallHelper() {}

  virtual void BeforeCall(MacroAssembler* masm) const;

  virtual void AfterCall(MacroAssembler* masm) const;
};


// Trivial RuntimeCallHelper implementation.
class NopRuntimeCallHelper : public RuntimeCallHelper {
 public:
  NopRuntimeCallHelper() {}

  virtual void BeforeCall(MacroAssembler* masm) const {}

  virtual void AfterCall(MacroAssembler* masm) const {}
};


class ToNumberStub: public HydrogenCodeStub {
 public:
  explicit ToNumberStub(Isolate* isolate) : HydrogenCodeStub(isolate) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate) {
    ToNumberStub stub(isolate);
    stub.InitializeInterfaceDescriptor(
        isolate->code_stub_interface_descriptor(CodeStub::ToNumber));
  }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return ToNumber; }
};


class NumberToStringStub V8_FINAL : public HydrogenCodeStub {
 public:
  explicit NumberToStringStub(Isolate* isolate) : HydrogenCodeStub(isolate) {}

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kNumber = 0;

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return NumberToString; }
};


class FastNewClosureStub : public HydrogenCodeStub {
 public:
  FastNewClosureStub(Isolate* isolate, StrictMode strict_mode,
                     bool is_generator)
      : HydrogenCodeStub(isolate) {
    set_sub_minor_key(StrictModeBits::encode(strict_mode) |
                      IsGeneratorBits::encode(is_generator));
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  StrictMode strict_mode() const {
    return StrictModeBits::decode(sub_minor_key());
  }

  bool is_generator() const { return IsGeneratorBits::decode(sub_minor_key()); }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return FastNewClosure; }

  class StrictModeBits : public BitField<StrictMode, 0, 1> {};
  class IsGeneratorBits : public BitField<bool, 1, 1> {};

  DISALLOW_COPY_AND_ASSIGN(FastNewClosureStub);
};


class FastNewContextStub V8_FINAL : public HydrogenCodeStub {
 public:
  static const int kMaximumSlots = 64;

  FastNewContextStub(Isolate* isolate, int slots)
      : HydrogenCodeStub(isolate), slots_(slots) {
    DCHECK(slots_ > 0 && slots_ <= kMaximumSlots);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  int slots() const { return slots_; }

  virtual Major MajorKey() const V8_OVERRIDE { return FastNewContext; }
  virtual int NotMissMinorKey() const V8_OVERRIDE { return slots_; }

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kFunction = 0;

 private:
  int slots_;
};


class FastCloneShallowArrayStub : public HydrogenCodeStub {
 public:
  FastCloneShallowArrayStub(Isolate* isolate,
                            AllocationSiteMode allocation_site_mode)
      : HydrogenCodeStub(isolate),
      allocation_site_mode_(allocation_site_mode) {}

  AllocationSiteMode allocation_site_mode() const {
    return allocation_site_mode_;
  }

  virtual Handle<Code> GenerateCode();

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

 private:
  AllocationSiteMode allocation_site_mode_;

  class AllocationSiteModeBits: public BitField<AllocationSiteMode, 0, 1> {};
  // Ensure data fits within available bits.
  virtual Major MajorKey() const V8_OVERRIDE { return FastCloneShallowArray; }
  int NotMissMinorKey() const {
    return AllocationSiteModeBits::encode(allocation_site_mode_);
  }
};


class FastCloneShallowObjectStub : public HydrogenCodeStub {
 public:
  // Maximum number of properties in copied object.
  static const int kMaximumClonedProperties = 6;

  FastCloneShallowObjectStub(Isolate* isolate, int length)
      : HydrogenCodeStub(isolate), length_(length) {
    DCHECK_GE(length_, 0);
    DCHECK_LE(length_, kMaximumClonedProperties);
  }

  int length() const { return length_; }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  int length_;

  virtual Major MajorKey() const V8_OVERRIDE { return FastCloneShallowObject; }
  int NotMissMinorKey() const { return length_; }

  DISALLOW_COPY_AND_ASSIGN(FastCloneShallowObjectStub);
};


class CreateAllocationSiteStub : public HydrogenCodeStub {
 public:
  explicit CreateAllocationSiteStub(Isolate* isolate)
      : HydrogenCodeStub(isolate) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  static void GenerateAheadOfTime(Isolate* isolate);

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return CreateAllocationSite; }

  DISALLOW_COPY_AND_ASSIGN(CreateAllocationSiteStub);
};


class InstanceofStub: public PlatformCodeStub {
 public:
  enum Flags {
    kNoFlags = 0,
    kArgsInRegisters = 1 << 0,
    kCallSiteInlineCheck = 1 << 1,
    kReturnTrueFalseObject = 1 << 2
  };

  InstanceofStub(Isolate* isolate, Flags flags) : PlatformCodeStub(isolate) {
    minor_key_ = FlagBits::encode(flags);
  }

  void Generate(MacroAssembler* masm);

  static Register left();
  static Register right();

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return Instanceof; }

  Flags flags() const { return FlagBits::decode(minor_key_); }

  bool HasArgsInRegisters() const { return (flags() & kArgsInRegisters) != 0; }

  bool HasCallSiteInlineCheck() const {
    return (flags() & kCallSiteInlineCheck) != 0;
  }

  bool ReturnTrueFalseObject() const {
    return (flags() & kReturnTrueFalseObject) != 0;
  }

  virtual void PrintName(OStream& os) const V8_OVERRIDE;  // NOLINT

  class FlagBits : public BitField<Flags, 0, 3> {};

  DISALLOW_COPY_AND_ASSIGN(InstanceofStub);
};


enum AllocationSiteOverrideMode {
  DONT_OVERRIDE,
  DISABLE_ALLOCATION_SITES,
  LAST_ALLOCATION_SITE_OVERRIDE_MODE = DISABLE_ALLOCATION_SITES
};


class ArrayConstructorStub: public PlatformCodeStub {
 public:
  enum ArgumentCountKey { ANY, NONE, ONE, MORE_THAN_ONE };
  ArrayConstructorStub(Isolate* isolate, int argument_count);

  explicit ArrayConstructorStub(Isolate* isolate);

  void Generate(MacroAssembler* masm);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return ArrayConstructor; }

  ArgumentCountKey argument_count() const {
    return ArgumentCountBits::decode(minor_key_);
  }

  void GenerateDispatchToArrayStub(MacroAssembler* masm,
                                   AllocationSiteOverrideMode mode);

  virtual void PrintName(OStream& os) const V8_OVERRIDE;  // NOLINT

  class ArgumentCountBits : public BitField<ArgumentCountKey, 0, 2> {};

  DISALLOW_COPY_AND_ASSIGN(ArrayConstructorStub);
};


class InternalArrayConstructorStub: public PlatformCodeStub {
 public:
  explicit InternalArrayConstructorStub(Isolate* isolate);

  void Generate(MacroAssembler* masm);

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return InternalArrayConstructor;
  }

  void GenerateCase(MacroAssembler* masm, ElementsKind kind);

  DISALLOW_COPY_AND_ASSIGN(InternalArrayConstructorStub);
};


class MathPowStub: public PlatformCodeStub {
 public:
  enum ExponentType { INTEGER, DOUBLE, TAGGED, ON_STACK };

  MathPowStub(Isolate* isolate, ExponentType exponent_type)
      : PlatformCodeStub(isolate) {
    minor_key_ = ExponentTypeBits::encode(exponent_type);
  }

  virtual void Generate(MacroAssembler* masm);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return MathPow; }

  ExponentType exponent_type() const {
    return ExponentTypeBits::decode(minor_key_);
  }

  class ExponentTypeBits : public BitField<ExponentType, 0, 2> {};

  DISALLOW_COPY_AND_ASSIGN(MathPowStub);
};


class CallICStub: public PlatformCodeStub {
 public:
  CallICStub(Isolate* isolate, const CallIC::State& state)
      : PlatformCodeStub(isolate) {
    minor_key_ = state.GetExtraICState();
  }

  static int ExtractArgcFromMinorKey(int minor_key) {
    CallIC::State state(static_cast<ExtraICState>(minor_key));
    return state.arg_count();
  }

  virtual void Generate(MacroAssembler* masm);

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE { return Code::CALL_IC; }

  virtual InlineCacheState GetICState() const V8_OVERRIDE { return DEFAULT; }

  virtual ExtraICState GetExtraICState() const V8_FINAL V8_OVERRIDE {
    return static_cast<ExtraICState>(minor_key_);
  }

 protected:
  bool CallAsMethod() const { return state().call_type() == CallIC::METHOD; }

  int arg_count() const { return state().arg_count(); }

  CallIC::State state() const {
    return CallIC::State(static_cast<ExtraICState>(minor_key_));
  }

  // Code generation helpers.
  void GenerateMiss(MacroAssembler* masm, IC::UtilityId id);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return CallIC; }

  virtual void PrintState(OStream& os) const V8_OVERRIDE;  // NOLINT

  DISALLOW_COPY_AND_ASSIGN(CallICStub);
};


class CallIC_ArrayStub: public CallICStub {
 public:
  CallIC_ArrayStub(Isolate* isolate, const CallIC::State& state_in)
      : CallICStub(isolate, state_in) {}

  virtual void Generate(MacroAssembler* masm);

  virtual InlineCacheState GetICState() const V8_FINAL V8_OVERRIDE {
    return MONOMORPHIC;
  }

 private:
  virtual void PrintState(OStream& os) const V8_OVERRIDE;  // NOLINT

  virtual Major MajorKey() const V8_OVERRIDE { return CallIC_Array; }

  DISALLOW_COPY_AND_ASSIGN(CallIC_ArrayStub);
};


// TODO(verwaest): Translate to hydrogen code stub.
class FunctionPrototypeStub : public PlatformCodeStub {
 public:
  explicit FunctionPrototypeStub(Isolate* isolate)
      : PlatformCodeStub(isolate) {}
  virtual void Generate(MacroAssembler* masm);
  virtual Code::Kind GetCodeKind() const { return Code::HANDLER; }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return FunctionPrototype; }

  DISALLOW_COPY_AND_ASSIGN(FunctionPrototypeStub);
};


class HandlerStub : public HydrogenCodeStub {
 public:
  virtual Code::Kind GetCodeKind() const { return Code::HANDLER; }
  virtual ExtraICState GetExtraICState() const { return kind(); }
  virtual InlineCacheState GetICState() const { return MONOMORPHIC; }

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 protected:
  explicit HandlerStub(Isolate* isolate)
      : HydrogenCodeStub(isolate), bit_field_(0) {}
  virtual int NotMissMinorKey() const { return bit_field_; }
  virtual Code::Kind kind() const = 0;
  int bit_field_;
};


class LoadFieldStub: public HandlerStub {
 public:
  LoadFieldStub(Isolate* isolate, FieldIndex index)
    : HandlerStub(isolate), index_(index) {
    int property_index_key = index_.GetFieldAccessStubKey();
    bit_field_ = EncodedLoadFieldByIndexBits::encode(property_index_key);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  FieldIndex index() const { return index_; }

 protected:
  explicit LoadFieldStub(Isolate* isolate);
  virtual Code::Kind kind() const { return Code::LOAD_IC; }
  virtual Code::StubType GetStubType() { return Code::FAST; }

 private:
  class EncodedLoadFieldByIndexBits : public BitField<int, 0, 13> {};
  virtual Major MajorKey() const V8_OVERRIDE { return LoadField; }
  FieldIndex index_;
};


class LoadConstantStub : public HandlerStub {
 public:
  LoadConstantStub(Isolate* isolate, int descriptor) : HandlerStub(isolate) {
    bit_field_ = descriptor;
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  int descriptor() const { return bit_field_; }

 protected:
  explicit LoadConstantStub(Isolate* isolate);
  virtual Code::Kind kind() const { return Code::LOAD_IC; }
  virtual Code::StubType GetStubType() { return Code::FAST; }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return LoadConstant; }
};


class StringLengthStub: public HandlerStub {
 public:
  explicit StringLengthStub(Isolate* isolate) : HandlerStub(isolate) {}
  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

 protected:
  virtual Code::Kind kind() const { return Code::LOAD_IC; }
  virtual Code::StubType GetStubType() { return Code::FAST; }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return StringLength; }
};


class StoreFieldStub : public HandlerStub {
 public:
  StoreFieldStub(Isolate* isolate, FieldIndex index,
                 Representation representation)
      : HandlerStub(isolate), index_(index), representation_(representation) {
    int property_index_key = index_.GetFieldAccessStubKey();
    bit_field_ = EncodedStoreFieldByIndexBits::encode(property_index_key) |
                 RepresentationBits::encode(
                     PropertyDetails::EncodeRepresentation(representation));
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  FieldIndex index() const { return index_; }
  Representation representation() { return representation_; }
  static void InstallDescriptors(Isolate* isolate);

 protected:
  explicit StoreFieldStub(Isolate* isolate);
  virtual Code::Kind kind() const { return Code::STORE_IC; }
  virtual Code::StubType GetStubType() { return Code::FAST; }

 private:
  class EncodedStoreFieldByIndexBits : public BitField<int, 0, 13> {};
  class RepresentationBits : public BitField<int, 13, 4> {};
  virtual Major MajorKey() const V8_OVERRIDE { return StoreField; }
  FieldIndex index_;
  Representation representation_;
};


class StoreGlobalStub : public HandlerStub {
 public:
  StoreGlobalStub(Isolate* isolate, bool is_constant, bool check_global)
      : HandlerStub(isolate) {
    bit_field_ = IsConstantBits::encode(is_constant) |
        CheckGlobalBits::encode(check_global);
  }

  static Handle<HeapObject> global_placeholder(Isolate* isolate) {
    return isolate->factory()->uninitialized_value();
  }

  Handle<Code> GetCodeCopyFromTemplate(Handle<GlobalObject> global,
                                       Handle<PropertyCell> cell) {
    if (check_global()) {
      Code::FindAndReplacePattern pattern;
      pattern.Add(Handle<Map>(global_placeholder(isolate())->map()), global);
      pattern.Add(isolate()->factory()->meta_map(), Handle<Map>(global->map()));
      pattern.Add(isolate()->factory()->global_property_cell_map(), cell);
      return CodeStub::GetCodeCopy(pattern);
    } else {
      Code::FindAndReplacePattern pattern;
      pattern.Add(isolate()->factory()->global_property_cell_map(), cell);
      return CodeStub::GetCodeCopy(pattern);
    }
  }

  virtual Code::Kind kind() const { return Code::STORE_IC; }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  bool is_constant() const {
    return IsConstantBits::decode(bit_field_);
  }
  bool check_global() const {
    return CheckGlobalBits::decode(bit_field_);
  }
  void set_is_constant(bool value) {
    bit_field_ = IsConstantBits::update(bit_field_, value);
  }

  Representation representation() {
    return Representation::FromKind(RepresentationBits::decode(bit_field_));
  }
  void set_representation(Representation r) {
    bit_field_ = RepresentationBits::update(bit_field_, r.kind());
  }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return StoreGlobal; }

  class IsConstantBits: public BitField<bool, 0, 1> {};
  class RepresentationBits: public BitField<Representation::Kind, 1, 8> {};
  class CheckGlobalBits: public BitField<bool, 9, 1> {};

  DISALLOW_COPY_AND_ASSIGN(StoreGlobalStub);
};


class CallApiFunctionStub : public PlatformCodeStub {
 public:
  CallApiFunctionStub(Isolate* isolate,
                      bool is_store,
                      bool call_data_undefined,
                      int argc) : PlatformCodeStub(isolate) {
    minor_key_ = IsStoreBits::encode(is_store) |
                 CallDataUndefinedBits::encode(call_data_undefined) |
                 ArgumentBits::encode(argc);
    DCHECK(!is_store || argc == 1);
  }

 private:
  virtual void Generate(MacroAssembler* masm) V8_OVERRIDE;
  virtual Major MajorKey() const V8_OVERRIDE { return CallApiFunction; }

  bool is_store() const { return IsStoreBits::decode(minor_key_); }
  bool call_data_undefined() const {
    return CallDataUndefinedBits::decode(minor_key_);
  }
  int argc() const { return ArgumentBits::decode(minor_key_); }

  class IsStoreBits: public BitField<bool, 0, 1> {};
  class CallDataUndefinedBits: public BitField<bool, 1, 1> {};
  class ArgumentBits: public BitField<int, 2, Code::kArgumentsBits> {};
  STATIC_ASSERT(Code::kArgumentsBits + 2 <= kStubMinorKeyBits);

  DISALLOW_COPY_AND_ASSIGN(CallApiFunctionStub);
};


class CallApiGetterStub : public PlatformCodeStub {
 public:
  explicit CallApiGetterStub(Isolate* isolate) : PlatformCodeStub(isolate) {}

 private:
  virtual void Generate(MacroAssembler* masm) V8_OVERRIDE;
  virtual Major MajorKey() const V8_OVERRIDE { return CallApiGetter; }

  DISALLOW_COPY_AND_ASSIGN(CallApiGetterStub);
};


class BinaryOpICStub : public HydrogenCodeStub {
 public:
  BinaryOpICStub(Isolate* isolate, Token::Value op,
                 OverwriteMode mode = NO_OVERWRITE)
      : HydrogenCodeStub(isolate, UNINITIALIZED), state_(isolate, op, mode) {}

  explicit BinaryOpICStub(Isolate* isolate, const BinaryOpIC::State& state)
      : HydrogenCodeStub(isolate), state_(state) {}

  static void GenerateAheadOfTime(Isolate* isolate);

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE {
    return Code::BINARY_OP_IC;
  }

  virtual InlineCacheState GetICState() const V8_FINAL V8_OVERRIDE {
    return state_.GetICState();
  }

  virtual ExtraICState GetExtraICState() const V8_FINAL V8_OVERRIDE {
    return state_.GetExtraICState();
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  const BinaryOpIC::State& state() const { return state_; }

  virtual void PrintState(OStream& os) const V8_FINAL V8_OVERRIDE;  // NOLINT

  virtual Major MajorKey() const V8_OVERRIDE { return BinaryOpIC; }
  virtual int NotMissMinorKey() const V8_FINAL V8_OVERRIDE {
    return GetExtraICState();
  }

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kLeft = 0;
  static const int kRight = 1;

 private:
  static void GenerateAheadOfTime(Isolate* isolate,
                                  const BinaryOpIC::State& state);

  BinaryOpIC::State state_;

  DISALLOW_COPY_AND_ASSIGN(BinaryOpICStub);
};


// TODO(bmeurer): Merge this into the BinaryOpICStub once we have proper tail
// call support for stubs in Hydrogen.
class BinaryOpICWithAllocationSiteStub V8_FINAL : public PlatformCodeStub {
 public:
  BinaryOpICWithAllocationSiteStub(Isolate* isolate,
                                   const BinaryOpIC::State& state)
      : PlatformCodeStub(isolate) {
    minor_key_ = state.GetExtraICState();
  }

  static void GenerateAheadOfTime(Isolate* isolate);

  Handle<Code> GetCodeCopyFromTemplate(Handle<AllocationSite> allocation_site) {
    Code::FindAndReplacePattern pattern;
    pattern.Add(isolate()->factory()->undefined_map(), allocation_site);
    return CodeStub::GetCodeCopy(pattern);
  }

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE {
    return Code::BINARY_OP_IC;
  }

  virtual InlineCacheState GetICState() const V8_OVERRIDE {
    return state().GetICState();
  }

  virtual ExtraICState GetExtraICState() const V8_OVERRIDE {
    return static_cast<ExtraICState>(minor_key_);
  }

  virtual void Generate(MacroAssembler* masm) V8_OVERRIDE;

  virtual void PrintState(OStream& os) const V8_OVERRIDE;  // NOLINT

  virtual Major MajorKey() const V8_OVERRIDE {
    return BinaryOpICWithAllocationSite;
  }

 private:
  BinaryOpIC::State state() const {
    return BinaryOpIC::State(isolate(), static_cast<ExtraICState>(minor_key_));
  }

  static void GenerateAheadOfTime(Isolate* isolate,
                                  const BinaryOpIC::State& state);

  DISALLOW_COPY_AND_ASSIGN(BinaryOpICWithAllocationSiteStub);
};


class BinaryOpWithAllocationSiteStub V8_FINAL : public BinaryOpICStub {
 public:
  BinaryOpWithAllocationSiteStub(Isolate* isolate,
                                 Token::Value op,
                                 OverwriteMode mode)
      : BinaryOpICStub(isolate, op, mode) {}

  BinaryOpWithAllocationSiteStub(Isolate* isolate,
                                 const BinaryOpIC::State& state)
      : BinaryOpICStub(isolate, state) {}

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual Code::Kind GetCodeKind() const V8_FINAL V8_OVERRIDE {
    return Code::STUB;
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual Major MajorKey() const V8_OVERRIDE {
    return BinaryOpWithAllocationSite;
  }

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kAllocationSite = 0;
  static const int kLeft = 1;
  static const int kRight = 2;
};


enum StringAddFlags {
  // Omit both parameter checks.
  STRING_ADD_CHECK_NONE = 0,
  // Check left parameter.
  STRING_ADD_CHECK_LEFT = 1 << 0,
  // Check right parameter.
  STRING_ADD_CHECK_RIGHT = 1 << 1,
  // Check both parameters.
  STRING_ADD_CHECK_BOTH = STRING_ADD_CHECK_LEFT | STRING_ADD_CHECK_RIGHT
};


class StringAddStub V8_FINAL : public HydrogenCodeStub {
 public:
  StringAddStub(Isolate* isolate,
                StringAddFlags flags,
                PretenureFlag pretenure_flag)
      : HydrogenCodeStub(isolate),
        bit_field_(StringAddFlagsBits::encode(flags) |
                   PretenureFlagBits::encode(pretenure_flag)) {}

  StringAddFlags flags() const {
    return StringAddFlagsBits::decode(bit_field_);
  }

  PretenureFlag pretenure_flag() const {
    return PretenureFlagBits::decode(bit_field_);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kLeft = 0;
  static const int kRight = 1;

 private:
  class StringAddFlagsBits: public BitField<StringAddFlags, 0, 2> {};
  class PretenureFlagBits: public BitField<PretenureFlag, 2, 1> {};
  uint32_t bit_field_;

  virtual Major MajorKey() const V8_OVERRIDE { return StringAdd; }
  virtual int NotMissMinorKey() const V8_OVERRIDE { return bit_field_; }

  virtual void PrintBaseName(OStream& os) const V8_OVERRIDE;  // NOLINT

  DISALLOW_COPY_AND_ASSIGN(StringAddStub);
};


class ICCompareStub: public PlatformCodeStub {
 public:
  ICCompareStub(Isolate* isolate,
                Token::Value op,
                CompareIC::State left,
                CompareIC::State right,
                CompareIC::State handler)
      : PlatformCodeStub(isolate),
        op_(op),
        left_(left),
        right_(right),
        state_(handler) {
    DCHECK(Token::IsCompareOp(op));
  }

  virtual void Generate(MacroAssembler* masm);

  void set_known_map(Handle<Map> map) { known_map_ = map; }

  static void DecodeKey(uint32_t stub_key, CompareIC::State* left_state,
                        CompareIC::State* right_state,
                        CompareIC::State* handler_state, Token::Value* op);

  virtual InlineCacheState GetICState() const;

 private:
  class OpField: public BitField<int, 0, 3> { };
  class LeftStateField: public BitField<int, 3, 4> { };
  class RightStateField: public BitField<int, 7, 4> { };
  class HandlerStateField: public BitField<int, 11, 4> { };

  virtual Major MajorKey() const V8_OVERRIDE { return CompareIC; }
  virtual uint32_t MinorKey() const;

  virtual Code::Kind GetCodeKind() const { return Code::COMPARE_IC; }

  void GenerateSmis(MacroAssembler* masm);
  void GenerateNumbers(MacroAssembler* masm);
  void GenerateInternalizedStrings(MacroAssembler* masm);
  void GenerateStrings(MacroAssembler* masm);
  void GenerateUniqueNames(MacroAssembler* masm);
  void GenerateObjects(MacroAssembler* masm);
  void GenerateMiss(MacroAssembler* masm);
  void GenerateKnownObjects(MacroAssembler* masm);
  void GenerateGeneric(MacroAssembler* masm);

  bool strict() const { return op_ == Token::EQ_STRICT; }
  Condition GetCondition() const { return CompareIC::ComputeCondition(op_); }

  virtual void AddToSpecialCache(Handle<Code> new_object);
  virtual bool FindCodeInSpecialCache(Code** code_out);
  virtual bool UseSpecialCache() { return state_ == CompareIC::KNOWN_OBJECT; }

  Token::Value op_;
  CompareIC::State left_;
  CompareIC::State right_;
  CompareIC::State state_;
  Handle<Map> known_map_;
};


class CompareNilICStub : public HydrogenCodeStub  {
 public:
  Type* GetType(Zone* zone, Handle<Map> map = Handle<Map>());
  Type* GetInputType(Zone* zone, Handle<Map> map);

  CompareNilICStub(Isolate* isolate, NilValue nil)
      : HydrogenCodeStub(isolate), nil_value_(nil) { }

  CompareNilICStub(Isolate* isolate,
                   ExtraICState ic_state,
                   InitializationState init_state = INITIALIZED)
      : HydrogenCodeStub(isolate, init_state),
        nil_value_(NilValueField::decode(ic_state)),
        state_(State(TypesField::decode(ic_state))) {
      }

  static Handle<Code> GetUninitialized(Isolate* isolate,
                                       NilValue nil) {
    return CompareNilICStub(isolate, nil, UNINITIALIZED).GetCode();
  }

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate) {
    CompareNilICStub compare_stub(isolate, kNullValue, UNINITIALIZED);
    compare_stub.InitializeInterfaceDescriptor(
        isolate->code_stub_interface_descriptor(CodeStub::CompareNilIC));
  }

  virtual InlineCacheState GetICState() const {
    if (state_.Contains(GENERIC)) {
      return MEGAMORPHIC;
    } else if (state_.Contains(MONOMORPHIC_MAP)) {
      return MONOMORPHIC;
    } else {
      return PREMONOMORPHIC;
    }
  }

  virtual Code::Kind GetCodeKind() const { return Code::COMPARE_NIL_IC; }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual ExtraICState GetExtraICState() const {
    return NilValueField::encode(nil_value_) |
           TypesField::encode(state_.ToIntegral());
  }

  void UpdateStatus(Handle<Object> object);

  bool IsMonomorphic() const { return state_.Contains(MONOMORPHIC_MAP); }
  NilValue GetNilValue() const { return nil_value_; }
  void ClearState() { state_.RemoveAll(); }

  virtual void PrintState(OStream& os) const V8_OVERRIDE;     // NOLINT
  virtual void PrintBaseName(OStream& os) const V8_OVERRIDE;  // NOLINT

 private:
  friend class CompareNilIC;

  enum CompareNilType {
    UNDEFINED,
    NULL_TYPE,
    MONOMORPHIC_MAP,
    GENERIC,
    NUMBER_OF_TYPES
  };

  // At most 6 different types can be distinguished, because the Code object
  // only has room for a single byte to hold a set and there are two more
  // boolean flags we need to store. :-P
  STATIC_ASSERT(NUMBER_OF_TYPES <= 6);

  class State : public EnumSet<CompareNilType, byte> {
   public:
    State() : EnumSet<CompareNilType, byte>(0) { }
    explicit State(byte bits) : EnumSet<CompareNilType, byte>(bits) { }
  };
  friend OStream& operator<<(OStream& os, const State& s);

  CompareNilICStub(Isolate* isolate,
                   NilValue nil,
                   InitializationState init_state)
      : HydrogenCodeStub(isolate, init_state), nil_value_(nil) { }

  class NilValueField : public BitField<NilValue, 0, 1> {};
  class TypesField    : public BitField<byte,     1, NUMBER_OF_TYPES> {};

  virtual Major MajorKey() const V8_OVERRIDE { return CompareNilIC; }
  virtual int NotMissMinorKey() const { return GetExtraICState(); }

  NilValue nil_value_;
  State state_;

  DISALLOW_COPY_AND_ASSIGN(CompareNilICStub);
};


OStream& operator<<(OStream& os, const CompareNilICStub::State& s);


class CEntryStub : public PlatformCodeStub {
 public:
  CEntryStub(Isolate* isolate, int result_size,
             SaveFPRegsMode save_doubles = kDontSaveFPRegs)
      : PlatformCodeStub(isolate) {
    minor_key_ = SaveDoublesBits::encode(save_doubles == kSaveFPRegs);
    DCHECK(result_size == 1 || result_size == 2);
#ifdef _WIN64
    minor_key_ = ResultSizeBits::update(minor_key_, result_size);
#endif  // _WIN64
  }

  void Generate(MacroAssembler* masm);

  // The version of this stub that doesn't save doubles is generated ahead of
  // time, so it's OK to call it from other stubs that can't cope with GC during
  // their code generation.  On machines that always have gp registers (x64) we
  // can generate both variants ahead of time.
  static void GenerateAheadOfTime(Isolate* isolate);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return CEntry; }

  bool save_doubles() const { return SaveDoublesBits::decode(minor_key_); }
#ifdef _WIN64
  int result_size() const { return ResultSizeBits::decode(minor_key_); }
#endif  // _WIN64

  bool NeedsImmovableCode();

  class SaveDoublesBits : public BitField<bool, 0, 1> {};
  class ResultSizeBits : public BitField<int, 1, 3> {};

  DISALLOW_COPY_AND_ASSIGN(CEntryStub);
};


class JSEntryStub : public PlatformCodeStub {
 public:
  explicit JSEntryStub(Isolate* isolate) : PlatformCodeStub(isolate) { }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, false); }

 protected:
  void GenerateBody(MacroAssembler* masm, bool is_construct);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return JSEntry; }

  virtual void FinishCode(Handle<Code> code);

  int handler_offset_;

  DISALLOW_COPY_AND_ASSIGN(JSEntryStub);
};


class JSConstructEntryStub : public JSEntryStub {
 public:
  explicit JSConstructEntryStub(Isolate* isolate) : JSEntryStub(isolate) {
    minor_key_ = 1;
  }

  void Generate(MacroAssembler* masm) { GenerateBody(masm, true); }

 private:
  virtual void PrintName(OStream& os) const V8_OVERRIDE {  // NOLINT
    os << "JSConstructEntryStub";
  }

  DISALLOW_COPY_AND_ASSIGN(JSConstructEntryStub);
};


class ArgumentsAccessStub: public PlatformCodeStub {
 public:
  enum Type {
    READ_ELEMENT,
    NEW_SLOPPY_FAST,
    NEW_SLOPPY_SLOW,
    NEW_STRICT
  };

  ArgumentsAccessStub(Isolate* isolate, Type type) : PlatformCodeStub(isolate) {
    minor_key_ = TypeBits::encode(type);
  }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return ArgumentsAccess; }

  Type type() const { return TypeBits::decode(minor_key_); }

  void Generate(MacroAssembler* masm);
  void GenerateReadElement(MacroAssembler* masm);
  void GenerateNewStrict(MacroAssembler* masm);
  void GenerateNewSloppyFast(MacroAssembler* masm);
  void GenerateNewSloppySlow(MacroAssembler* masm);

  virtual void PrintName(OStream& os) const V8_OVERRIDE;  // NOLINT

  class TypeBits : public BitField<Type, 0, 2> {};

  DISALLOW_COPY_AND_ASSIGN(ArgumentsAccessStub);
};


class RegExpExecStub: public PlatformCodeStub {
 public:
  explicit RegExpExecStub(Isolate* isolate) : PlatformCodeStub(isolate) { }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return RegExpExec; }

  void Generate(MacroAssembler* masm);

  DISALLOW_COPY_AND_ASSIGN(RegExpExecStub);
};


class RegExpConstructResultStub V8_FINAL : public HydrogenCodeStub {
 public:
  explicit RegExpConstructResultStub(Isolate* isolate)
      : HydrogenCodeStub(isolate) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  virtual Major MajorKey() const V8_OVERRIDE { return RegExpConstructResult; }

  static void InstallDescriptors(Isolate* isolate);

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kLength = 0;
  static const int kIndex = 1;
  static const int kInput = 2;

 private:
  DISALLOW_COPY_AND_ASSIGN(RegExpConstructResultStub);
};


class CallFunctionStub: public PlatformCodeStub {
 public:
  CallFunctionStub(Isolate* isolate, int argc, CallFunctionFlags flags)
      : PlatformCodeStub(isolate) {
    DCHECK(argc >= 0 && argc <= Code::kMaxArguments);
    minor_key_ = ArgcBits::encode(argc) | FlagBits::encode(flags);
  }

  void Generate(MacroAssembler* masm);

  static int ExtractArgcFromMinorKey(int minor_key) {
    return ArgcBits::decode(minor_key);
  }

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return CallFunction; }

  int argc() const { return ArgcBits::decode(minor_key_); }
  int flags() const { return FlagBits::decode(minor_key_); }

  bool CallAsMethod() const {
    return flags() == CALL_AS_METHOD || flags() == WRAP_AND_CALL;
  }

  bool NeedsChecks() const { return flags() != WRAP_AND_CALL; }

  virtual void PrintName(OStream& os) const V8_OVERRIDE;  // NOLINT

  // Minor key encoding in 32 bits with Bitfield <Type, shift, size>.
  class FlagBits : public BitField<CallFunctionFlags, 0, 2> {};
  class ArgcBits : public BitField<unsigned, 2, Code::kArgumentsBits> {};
  STATIC_ASSERT(Code::kArgumentsBits + 2 <= kStubMinorKeyBits);

  DISALLOW_COPY_AND_ASSIGN(CallFunctionStub);
};


class CallConstructStub: public PlatformCodeStub {
 public:
  CallConstructStub(Isolate* isolate, CallConstructorFlags flags)
      : PlatformCodeStub(isolate) {
    minor_key_ = FlagBits::encode(flags);
  }

  void Generate(MacroAssembler* masm);

  virtual void FinishCode(Handle<Code> code) {
    code->set_has_function_cache(RecordCallTarget());
  }

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return CallConstruct; }

  CallConstructorFlags flags() const { return FlagBits::decode(minor_key_); }

  bool RecordCallTarget() const {
    return (flags() & RECORD_CONSTRUCTOR_TARGET) != 0;
  }

  virtual void PrintName(OStream& os) const V8_OVERRIDE;  // NOLINT

  class FlagBits : public BitField<CallConstructorFlags, 0, 1> {};

  DISALLOW_COPY_AND_ASSIGN(CallConstructStub);
};


enum StringIndexFlags {
  // Accepts smis or heap numbers.
  STRING_INDEX_IS_NUMBER,

  // Accepts smis or heap numbers that are valid array indices
  // (ECMA-262 15.4). Invalid indices are reported as being out of
  // range.
  STRING_INDEX_IS_ARRAY_INDEX
};


// Generates code implementing String.prototype.charCodeAt.
//
// Only supports the case when the receiver is a string and the index
// is a number (smi or heap number) that is a valid index into the
// string. Additional index constraints are specified by the
// flags. Otherwise, bails out to the provided labels.
//
// Register usage: |object| may be changed to another string in a way
// that doesn't affect charCodeAt/charAt semantics, |index| is
// preserved, |scratch| and |result| are clobbered.
class StringCharCodeAtGenerator {
 public:
  StringCharCodeAtGenerator(Register object,
                            Register index,
                            Register result,
                            Label* receiver_not_string,
                            Label* index_not_number,
                            Label* index_out_of_range,
                            StringIndexFlags index_flags)
      : object_(object),
        index_(index),
        result_(result),
        receiver_not_string_(receiver_not_string),
        index_not_number_(index_not_number),
        index_out_of_range_(index_out_of_range),
        index_flags_(index_flags) {
    DCHECK(!result_.is(object_));
    DCHECK(!result_.is(index_));
  }

  // Generates the fast case code. On the fallthrough path |result|
  // register contains the result.
  void GenerateFast(MacroAssembler* masm);

  // Generates the slow case code. Must not be naturally
  // reachable. Expected to be put after a ret instruction (e.g., in
  // deferred code). Always jumps back to the fast case.
  void GenerateSlow(MacroAssembler* masm,
                    const RuntimeCallHelper& call_helper);

  // Skip handling slow case and directly jump to bailout.
  void SkipSlow(MacroAssembler* masm, Label* bailout) {
    masm->bind(&index_not_smi_);
    masm->bind(&call_runtime_);
    masm->jmp(bailout);
  }

 private:
  Register object_;
  Register index_;
  Register result_;

  Label* receiver_not_string_;
  Label* index_not_number_;
  Label* index_out_of_range_;

  StringIndexFlags index_flags_;

  Label call_runtime_;
  Label index_not_smi_;
  Label got_smi_index_;
  Label exit_;

  DISALLOW_COPY_AND_ASSIGN(StringCharCodeAtGenerator);
};


// Generates code for creating a one-char string from a char code.
class StringCharFromCodeGenerator {
 public:
  StringCharFromCodeGenerator(Register code,
                              Register result)
      : code_(code),
        result_(result) {
    DCHECK(!code_.is(result_));
  }

  // Generates the fast case code. On the fallthrough path |result|
  // register contains the result.
  void GenerateFast(MacroAssembler* masm);

  // Generates the slow case code. Must not be naturally
  // reachable. Expected to be put after a ret instruction (e.g., in
  // deferred code). Always jumps back to the fast case.
  void GenerateSlow(MacroAssembler* masm,
                    const RuntimeCallHelper& call_helper);

  // Skip handling slow case and directly jump to bailout.
  void SkipSlow(MacroAssembler* masm, Label* bailout) {
    masm->bind(&slow_case_);
    masm->jmp(bailout);
  }

 private:
  Register code_;
  Register result_;

  Label slow_case_;
  Label exit_;

  DISALLOW_COPY_AND_ASSIGN(StringCharFromCodeGenerator);
};


// Generates code implementing String.prototype.charAt.
//
// Only supports the case when the receiver is a string and the index
// is a number (smi or heap number) that is a valid index into the
// string. Additional index constraints are specified by the
// flags. Otherwise, bails out to the provided labels.
//
// Register usage: |object| may be changed to another string in a way
// that doesn't affect charCodeAt/charAt semantics, |index| is
// preserved, |scratch1|, |scratch2|, and |result| are clobbered.
class StringCharAtGenerator {
 public:
  StringCharAtGenerator(Register object,
                        Register index,
                        Register scratch,
                        Register result,
                        Label* receiver_not_string,
                        Label* index_not_number,
                        Label* index_out_of_range,
                        StringIndexFlags index_flags)
      : char_code_at_generator_(object,
                                index,
                                scratch,
                                receiver_not_string,
                                index_not_number,
                                index_out_of_range,
                                index_flags),
        char_from_code_generator_(scratch, result) {}

  // Generates the fast case code. On the fallthrough path |result|
  // register contains the result.
  void GenerateFast(MacroAssembler* masm) {
    char_code_at_generator_.GenerateFast(masm);
    char_from_code_generator_.GenerateFast(masm);
  }

  // Generates the slow case code. Must not be naturally
  // reachable. Expected to be put after a ret instruction (e.g., in
  // deferred code). Always jumps back to the fast case.
  void GenerateSlow(MacroAssembler* masm,
                    const RuntimeCallHelper& call_helper) {
    char_code_at_generator_.GenerateSlow(masm, call_helper);
    char_from_code_generator_.GenerateSlow(masm, call_helper);
  }

  // Skip handling slow case and directly jump to bailout.
  void SkipSlow(MacroAssembler* masm, Label* bailout) {
    char_code_at_generator_.SkipSlow(masm, bailout);
    char_from_code_generator_.SkipSlow(masm, bailout);
  }

 private:
  StringCharCodeAtGenerator char_code_at_generator_;
  StringCharFromCodeGenerator char_from_code_generator_;

  DISALLOW_COPY_AND_ASSIGN(StringCharAtGenerator);
};


class LoadDictionaryElementStub : public HydrogenCodeStub {
 public:
  explicit LoadDictionaryElementStub(Isolate* isolate)
      : HydrogenCodeStub(isolate) {
    set_sub_minor_key(DICTIONARY_ELEMENTS);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return LoadElement; }

  DISALLOW_COPY_AND_ASSIGN(LoadDictionaryElementStub);
};


class LoadDictionaryElementPlatformStub : public PlatformCodeStub {
 public:
  explicit LoadDictionaryElementPlatformStub(Isolate* isolate)
      : PlatformCodeStub(isolate) {
    minor_key_ = DICTIONARY_ELEMENTS;
  }

  void Generate(MacroAssembler* masm);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return LoadElement; }

  DISALLOW_COPY_AND_ASSIGN(LoadDictionaryElementPlatformStub);
};


class KeyedLoadGenericStub : public HydrogenCodeStub {
 public:
  explicit KeyedLoadGenericStub(Isolate* isolate) : HydrogenCodeStub(isolate) {}

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual Code::Kind GetCodeKind() const { return Code::KEYED_LOAD_IC; }
  virtual InlineCacheState GetICState() const { return GENERIC; }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return KeyedLoadGeneric; }

  DISALLOW_COPY_AND_ASSIGN(KeyedLoadGenericStub);
};


class LoadICTrampolineStub : public PlatformCodeStub {
 public:
  LoadICTrampolineStub(Isolate* isolate, const LoadIC::State& state)
      : PlatformCodeStub(isolate) {
    minor_key_ = state.GetExtraICState();
  }

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE { return Code::LOAD_IC; }

  virtual InlineCacheState GetICState() const V8_FINAL V8_OVERRIDE {
    return GENERIC;
  }

  virtual ExtraICState GetExtraICState() const V8_FINAL V8_OVERRIDE {
    return static_cast<ExtraICState>(minor_key_);
  }

  virtual Major MajorKey() const V8_OVERRIDE { return LoadICTrampoline; }

 private:
  LoadIC::State state() const {
    return LoadIC::State(static_cast<ExtraICState>(minor_key_));
  }

  virtual void Generate(MacroAssembler* masm);

  DISALLOW_COPY_AND_ASSIGN(LoadICTrampolineStub);
};


class KeyedLoadICTrampolineStub : public LoadICTrampolineStub {
 public:
  explicit KeyedLoadICTrampolineStub(Isolate* isolate)
      : LoadICTrampolineStub(isolate, LoadIC::State(0)) {}

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE {
    return Code::KEYED_LOAD_IC;
  }

  virtual Major MajorKey() const V8_OVERRIDE { return KeyedLoadICTrampoline; }

 private:
  virtual void Generate(MacroAssembler* masm);

  DISALLOW_COPY_AND_ASSIGN(KeyedLoadICTrampolineStub);
};


class VectorLoadStub : public HydrogenCodeStub {
 public:
  explicit VectorLoadStub(Isolate* isolate, const LoadIC::State& state)
      : HydrogenCodeStub(isolate), state_(state) {}

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE { return Code::LOAD_IC; }

  virtual InlineCacheState GetICState() const V8_FINAL V8_OVERRIDE {
    return GENERIC;
  }

  virtual ExtraICState GetExtraICState() const V8_FINAL V8_OVERRIDE {
    return state_.GetExtraICState();
  }

  virtual Major MajorKey() const V8_OVERRIDE { return VectorLoad; }

 private:
  int NotMissMinorKey() const { return state_.GetExtraICState(); }

  const LoadIC::State state_;

  DISALLOW_COPY_AND_ASSIGN(VectorLoadStub);
};


class VectorKeyedLoadStub : public VectorLoadStub {
 public:
  explicit VectorKeyedLoadStub(Isolate* isolate)
      : VectorLoadStub(isolate, LoadIC::State(0)) {}

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual Code::Kind GetCodeKind() const V8_OVERRIDE {
    return Code::KEYED_LOAD_IC;
  }

  virtual Major MajorKey() const V8_OVERRIDE { return VectorKeyedLoad; }

 private:
  DISALLOW_COPY_AND_ASSIGN(VectorKeyedLoadStub);
};


class DoubleToIStub : public PlatformCodeStub {
 public:
  DoubleToIStub(Isolate* isolate, Register source, Register destination,
                int offset, bool is_truncating, bool skip_fastpath = false)
      : PlatformCodeStub(isolate) {
    minor_key_ = SourceRegisterBits::encode(source.code()) |
                 DestinationRegisterBits::encode(destination.code()) |
                 OffsetBits::encode(offset) |
                 IsTruncatingBits::encode(is_truncating) |
                 SkipFastPathBits::encode(skip_fastpath) |
                 SSE3Bits::encode(CpuFeatures::IsSupported(SSE3) ? 1 : 0);
  }

  void Generate(MacroAssembler* masm);

  virtual bool SometimesSetsUpAFrame() { return false; }

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return DoubleToI; }

  Register source() const {
    return Register::from_code(SourceRegisterBits::decode(minor_key_));
  }
  Register destination() const {
    return Register::from_code(DestinationRegisterBits::decode(minor_key_));
  }
  bool is_truncating() const { return IsTruncatingBits::decode(minor_key_); }
  bool skip_fastpath() const { return SkipFastPathBits::decode(minor_key_); }
  int offset() const { return OffsetBits::decode(minor_key_); }

  static const int kBitsPerRegisterNumber = 6;
  STATIC_ASSERT((1L << kBitsPerRegisterNumber) >= Register::kNumRegisters);
  class SourceRegisterBits:
      public BitField<int, 0, kBitsPerRegisterNumber> {};  // NOLINT
  class DestinationRegisterBits:
      public BitField<int, kBitsPerRegisterNumber,
        kBitsPerRegisterNumber> {};  // NOLINT
  class IsTruncatingBits:
      public BitField<bool, 2 * kBitsPerRegisterNumber, 1> {};  // NOLINT
  class OffsetBits:
      public BitField<int, 2 * kBitsPerRegisterNumber + 1, 3> {};  // NOLINT
  class SkipFastPathBits:
      public BitField<int, 2 * kBitsPerRegisterNumber + 4, 1> {};  // NOLINT
  class SSE3Bits:
      public BitField<int, 2 * kBitsPerRegisterNumber + 5, 1> {};  // NOLINT

  DISALLOW_COPY_AND_ASSIGN(DoubleToIStub);
};


class LoadFastElementStub : public HydrogenCodeStub {
 public:
  LoadFastElementStub(Isolate* isolate, bool is_js_array,
                      ElementsKind elements_kind)
      : HydrogenCodeStub(isolate) {
    bit_field_ = ElementsKindBits::encode(elements_kind) |
        IsJSArrayBits::encode(is_js_array);
  }

  bool is_js_array() const {
    return IsJSArrayBits::decode(bit_field_);
  }

  ElementsKind elements_kind() const {
    return ElementsKindBits::decode(bit_field_);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  static void InstallDescriptors(Isolate* isolate);

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  class ElementsKindBits: public BitField<ElementsKind, 0, 8> {};
  class IsJSArrayBits: public BitField<bool, 8, 1> {};
  uint32_t bit_field_;

  virtual Major MajorKey() const V8_OVERRIDE { return LoadElement; }
  int NotMissMinorKey() const { return bit_field_; }

  DISALLOW_COPY_AND_ASSIGN(LoadFastElementStub);
};


class StoreFastElementStub : public HydrogenCodeStub {
 public:
  StoreFastElementStub(Isolate* isolate, bool is_js_array,
                       ElementsKind elements_kind, KeyedAccessStoreMode mode)
      : HydrogenCodeStub(isolate) {
    bit_field_ = ElementsKindBits::encode(elements_kind) |
        IsJSArrayBits::encode(is_js_array) |
        StoreModeBits::encode(mode);
  }

  bool is_js_array() const {
    return IsJSArrayBits::decode(bit_field_);
  }

  ElementsKind elements_kind() const {
    return ElementsKindBits::decode(bit_field_);
  }

  KeyedAccessStoreMode store_mode() const {
    return StoreModeBits::decode(bit_field_);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  class ElementsKindBits: public BitField<ElementsKind,      0, 8> {};
  class StoreModeBits: public BitField<KeyedAccessStoreMode, 8, 4> {};
  class IsJSArrayBits: public BitField<bool,                12, 1> {};
  uint32_t bit_field_;

  virtual Major MajorKey() const V8_OVERRIDE { return StoreElement; }
  int NotMissMinorKey() const { return bit_field_; }

  DISALLOW_COPY_AND_ASSIGN(StoreFastElementStub);
};


class TransitionElementsKindStub : public HydrogenCodeStub {
 public:
  TransitionElementsKindStub(Isolate* isolate,
                             ElementsKind from_kind,
                             ElementsKind to_kind,
                             bool is_js_array) : HydrogenCodeStub(isolate) {
    bit_field_ = FromKindBits::encode(from_kind) |
                 ToKindBits::encode(to_kind) |
                 IsJSArrayBits::encode(is_js_array);
  }

  ElementsKind from_kind() const {
    return FromKindBits::decode(bit_field_);
  }

  ElementsKind to_kind() const {
    return ToKindBits::decode(bit_field_);
  }

  bool is_js_array() const {
    return IsJSArrayBits::decode(bit_field_);
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  class FromKindBits: public BitField<ElementsKind, 8, 8> {};
  class ToKindBits: public BitField<ElementsKind, 0, 8> {};
  class IsJSArrayBits: public BitField<bool, 16, 1> {};
  uint32_t bit_field_;

  virtual Major MajorKey() const V8_OVERRIDE { return TransitionElementsKind; }
  int NotMissMinorKey() const { return bit_field_; }

  DISALLOW_COPY_AND_ASSIGN(TransitionElementsKindStub);
};


class ArrayConstructorStubBase : public HydrogenCodeStub {
 public:
  ArrayConstructorStubBase(Isolate* isolate,
                           ElementsKind kind,
                           AllocationSiteOverrideMode override_mode)
      : HydrogenCodeStub(isolate) {
    // It only makes sense to override local allocation site behavior
    // if there is a difference between the global allocation site policy
    // for an ElementsKind and the desired usage of the stub.
    DCHECK(override_mode != DISABLE_ALLOCATION_SITES ||
           AllocationSite::GetMode(kind) == TRACK_ALLOCATION_SITE);
    bit_field_ = ElementsKindBits::encode(kind) |
        AllocationSiteOverrideModeBits::encode(override_mode);
  }

  ElementsKind elements_kind() const {
    return ElementsKindBits::decode(bit_field_);
  }

  AllocationSiteOverrideMode override_mode() const {
    return AllocationSiteOverrideModeBits::decode(bit_field_);
  }

  static void GenerateStubsAheadOfTime(Isolate* isolate);
  static void InstallDescriptors(Isolate* isolate);

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kConstructor = 0;
  static const int kAllocationSite = 1;

 protected:
  OStream& BasePrintName(OStream& os, const char* name) const;  // NOLINT

 private:
  int NotMissMinorKey() const { return bit_field_; }

  // Ensure data fits within available bits.
  STATIC_ASSERT(LAST_ALLOCATION_SITE_OVERRIDE_MODE == 1);

  class ElementsKindBits: public BitField<ElementsKind, 0, 8> {};
  class AllocationSiteOverrideModeBits: public
      BitField<AllocationSiteOverrideMode, 8, 1> {};  // NOLINT
  uint32_t bit_field_;

  DISALLOW_COPY_AND_ASSIGN(ArrayConstructorStubBase);
};


class ArrayNoArgumentConstructorStub : public ArrayConstructorStubBase {
 public:
  ArrayNoArgumentConstructorStub(
      Isolate* isolate,
      ElementsKind kind,
      AllocationSiteOverrideMode override_mode = DONT_OVERRIDE)
      : ArrayConstructorStubBase(isolate, kind, override_mode) {
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return ArrayNoArgumentConstructor;
  }

  virtual void PrintName(OStream& os) const V8_OVERRIDE {  // NOLINT
    BasePrintName(os, "ArrayNoArgumentConstructorStub");
  }

  DISALLOW_COPY_AND_ASSIGN(ArrayNoArgumentConstructorStub);
};


class ArraySingleArgumentConstructorStub : public ArrayConstructorStubBase {
 public:
  ArraySingleArgumentConstructorStub(
      Isolate* isolate,
      ElementsKind kind,
      AllocationSiteOverrideMode override_mode = DONT_OVERRIDE)
      : ArrayConstructorStubBase(isolate, kind, override_mode) {
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return ArraySingleArgumentConstructor;
  }

  virtual void PrintName(OStream& os) const {  // NOLINT
    BasePrintName(os, "ArraySingleArgumentConstructorStub");
  }

  DISALLOW_COPY_AND_ASSIGN(ArraySingleArgumentConstructorStub);
};


class ArrayNArgumentsConstructorStub : public ArrayConstructorStubBase {
 public:
  ArrayNArgumentsConstructorStub(
      Isolate* isolate,
      ElementsKind kind,
      AllocationSiteOverrideMode override_mode = DONT_OVERRIDE)
      : ArrayConstructorStubBase(isolate, kind, override_mode) {
  }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return ArrayNArgumentsConstructor;
  }

  virtual void PrintName(OStream& os) const {  // NOLINT
    BasePrintName(os, "ArrayNArgumentsConstructorStub");
  }

  DISALLOW_COPY_AND_ASSIGN(ArrayNArgumentsConstructorStub);
};


class InternalArrayConstructorStubBase : public HydrogenCodeStub {
 public:
  InternalArrayConstructorStubBase(Isolate* isolate, ElementsKind kind)
      : HydrogenCodeStub(isolate) {
    kind_ = kind;
  }

  static void GenerateStubsAheadOfTime(Isolate* isolate);
  static void InstallDescriptors(Isolate* isolate);

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  static const int kConstructor = 0;

  ElementsKind elements_kind() const { return kind_; }

 private:
  int NotMissMinorKey() const { return kind_; }

  ElementsKind kind_;

  DISALLOW_COPY_AND_ASSIGN(InternalArrayConstructorStubBase);
};


class InternalArrayNoArgumentConstructorStub : public
    InternalArrayConstructorStubBase {
 public:
  InternalArrayNoArgumentConstructorStub(Isolate* isolate,
                                         ElementsKind kind)
      : InternalArrayConstructorStubBase(isolate, kind) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return InternalArrayNoArgumentConstructor;
  }

  DISALLOW_COPY_AND_ASSIGN(InternalArrayNoArgumentConstructorStub);
};


class InternalArraySingleArgumentConstructorStub : public
    InternalArrayConstructorStubBase {
 public:
  InternalArraySingleArgumentConstructorStub(Isolate* isolate,
                                             ElementsKind kind)
      : InternalArrayConstructorStubBase(isolate, kind) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return InternalArraySingleArgumentConstructor;
  }

  DISALLOW_COPY_AND_ASSIGN(InternalArraySingleArgumentConstructorStub);
};


class InternalArrayNArgumentsConstructorStub : public
    InternalArrayConstructorStubBase {
 public:
  InternalArrayNArgumentsConstructorStub(Isolate* isolate, ElementsKind kind)
      : InternalArrayConstructorStubBase(isolate, kind) { }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return InternalArrayNArgumentsConstructor;
  }

  DISALLOW_COPY_AND_ASSIGN(InternalArrayNArgumentsConstructorStub);
};


class StoreElementStub : public PlatformCodeStub {
 public:
  StoreElementStub(Isolate* isolate, ElementsKind elements_kind)
      : PlatformCodeStub(isolate) {
    minor_key_ = ElementsKindBits::encode(elements_kind);
  }

  void Generate(MacroAssembler* masm);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return StoreElement; }

  ElementsKind elements_kind() const {
    return ElementsKindBits::decode(minor_key_);
  }

  class ElementsKindBits : public BitField<ElementsKind, 0, 8> {};

  DISALLOW_COPY_AND_ASSIGN(StoreElementStub);
};


class ToBooleanStub: public HydrogenCodeStub {
 public:
  enum Type {
    UNDEFINED,
    BOOLEAN,
    NULL_TYPE,
    SMI,
    SPEC_OBJECT,
    STRING,
    SYMBOL,
    HEAP_NUMBER,
    NUMBER_OF_TYPES
  };

  enum ResultMode {
    RESULT_AS_SMI,             // For Smi(1) on truthy value, Smi(0) otherwise.
    RESULT_AS_ODDBALL,         // For {true} on truthy value, {false} otherwise.
    RESULT_AS_INVERSE_ODDBALL  // For {false} on truthy value, {true} otherwise.
  };

  // At most 8 different types can be distinguished, because the Code object
  // only has room for a single byte to hold a set of these types. :-P
  STATIC_ASSERT(NUMBER_OF_TYPES <= 8);

  class Types : public EnumSet<Type, byte> {
   public:
    Types() : EnumSet<Type, byte>(0) {}
    explicit Types(byte bits) : EnumSet<Type, byte>(bits) {}

    byte ToByte() const { return ToIntegral(); }
    bool UpdateStatus(Handle<Object> object);
    bool NeedsMap() const;
    bool CanBeUndetectable() const;
    bool IsGeneric() const { return ToIntegral() == Generic().ToIntegral(); }

    static Types Generic() { return Types((1 << NUMBER_OF_TYPES) - 1); }
  };

  ToBooleanStub(Isolate* isolate, ResultMode mode, Types types = Types())
      : HydrogenCodeStub(isolate), types_(types), mode_(mode) {}
  ToBooleanStub(Isolate* isolate, ExtraICState state)
      : HydrogenCodeStub(isolate),
        types_(static_cast<byte>(state)),
        mode_(RESULT_AS_SMI) {}

  bool UpdateStatus(Handle<Object> object);
  Types GetTypes() { return types_; }
  ResultMode GetMode() { return mode_; }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;
  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  virtual Code::Kind GetCodeKind() const { return Code::TO_BOOLEAN_IC; }
  virtual void PrintState(OStream& os) const V8_OVERRIDE;  // NOLINT

  virtual bool SometimesSetsUpAFrame() { return false; }

  static void InstallDescriptors(Isolate* isolate) {
    ToBooleanStub stub(isolate, RESULT_AS_SMI);
    stub.InitializeInterfaceDescriptor(
        isolate->code_stub_interface_descriptor(CodeStub::ToBoolean));
  }

  static Handle<Code> GetUninitialized(Isolate* isolate) {
    return ToBooleanStub(isolate, UNINITIALIZED).GetCode();
  }

  virtual ExtraICState GetExtraICState() const { return types_.ToIntegral(); }

  virtual InlineCacheState GetICState() const {
    if (types_.IsEmpty()) {
      return ::v8::internal::UNINITIALIZED;
    } else {
      return MONOMORPHIC;
    }
  }

 private:
  class TypesBits : public BitField<byte, 0, NUMBER_OF_TYPES> {};
  class ResultModeBits : public BitField<ResultMode, NUMBER_OF_TYPES, 2> {};

  virtual Major MajorKey() const V8_OVERRIDE { return ToBoolean; }
  int NotMissMinorKey() const {
    return TypesBits::encode(types_.ToByte()) | ResultModeBits::encode(mode_);
  }

  ToBooleanStub(Isolate* isolate, InitializationState init_state)
      : HydrogenCodeStub(isolate, init_state), mode_(RESULT_AS_SMI) {}

  Types types_;
  ResultMode mode_;
};


OStream& operator<<(OStream& os, const ToBooleanStub::Types& t);


class ElementsTransitionAndStoreStub : public HydrogenCodeStub {
 public:
  ElementsTransitionAndStoreStub(Isolate* isolate,
                                 ElementsKind from_kind,
                                 ElementsKind to_kind,
                                 bool is_jsarray,
                                 KeyedAccessStoreMode store_mode)
      : HydrogenCodeStub(isolate),
        from_kind_(from_kind),
        to_kind_(to_kind),
        is_jsarray_(is_jsarray),
        store_mode_(store_mode) {}

  ElementsKind from_kind() const { return from_kind_; }
  ElementsKind to_kind() const { return to_kind_; }
  bool is_jsarray() const { return is_jsarray_; }
  KeyedAccessStoreMode store_mode() const { return store_mode_; }

  virtual Handle<Code> GenerateCode() V8_OVERRIDE;

  virtual void InitializeInterfaceDescriptor(
      CodeStubInterfaceDescriptor* descriptor) V8_OVERRIDE;

  // Parameters accessed via CodeStubGraphBuilder::GetParameter()
  enum ParameterIndices {
    kValueIndex,
    kMapIndex,
    kKeyIndex,
    kObjectIndex,
    kParameterCount
  };

  static const Register ValueRegister() {
    return StoreConvention::ValueRegister();
  }
  static const Register MapRegister() { return StoreConvention::MapRegister(); }
  static const Register KeyRegister() {
    return StoreConvention::NameRegister();
  }
  static const Register ObjectRegister() {
    return StoreConvention::ReceiverRegister();
  }

 private:
  class FromBits:      public BitField<ElementsKind,          0, 8> {};
  class ToBits:        public BitField<ElementsKind,          8, 8> {};
  class IsJSArrayBits: public BitField<bool,                 16, 1> {};
  class StoreModeBits: public BitField<KeyedAccessStoreMode, 17, 4> {};

  virtual Major MajorKey() const V8_OVERRIDE {
    return ElementsTransitionAndStore;
  }
  int NotMissMinorKey() const {
    return FromBits::encode(from_kind_) |
        ToBits::encode(to_kind_) |
        IsJSArrayBits::encode(is_jsarray_) |
        StoreModeBits::encode(store_mode_);
  }

  ElementsKind from_kind_;
  ElementsKind to_kind_;
  bool is_jsarray_;
  KeyedAccessStoreMode store_mode_;

  DISALLOW_COPY_AND_ASSIGN(ElementsTransitionAndStoreStub);
};


class StoreArrayLiteralElementStub : public PlatformCodeStub {
 public:
  explicit StoreArrayLiteralElementStub(Isolate* isolate)
      : PlatformCodeStub(isolate) { }

 private:
  virtual Major MajorKey() const V8_OVERRIDE {
    return StoreArrayLiteralElement;
  }

  void Generate(MacroAssembler* masm);

  DISALLOW_COPY_AND_ASSIGN(StoreArrayLiteralElementStub);
};


class StubFailureTrampolineStub : public PlatformCodeStub {
 public:
  StubFailureTrampolineStub(Isolate* isolate, StubFunctionMode function_mode)
      : PlatformCodeStub(isolate) {
    minor_key_ = FunctionModeField::encode(function_mode);
  }

  static void GenerateAheadOfTime(Isolate* isolate);

 private:
  virtual Major MajorKey() const V8_OVERRIDE { return StubFailureTrampoline; }

  StubFunctionMode function_mode() const {
    return FunctionModeField::decode(minor_key_);
  }

  void Generate(MacroAssembler* masm);

  class FunctionModeField : public BitField<StubFunctionMode, 0, 1> {};

  DISALLOW_COPY_AND_ASSIGN(StubFailureTrampolineStub);
};


class ProfileEntryHookStub : public PlatformCodeStub {
 public:
  explicit ProfileEntryHookStub(Isolate* isolate) : PlatformCodeStub(isolate) {}

  // The profile entry hook function is not allowed to cause a GC.
  virtual bool SometimesSetsUpAFrame() { return false; }

  // Generates a call to the entry hook if it's enabled.
  static void MaybeCallEntryHook(MacroAssembler* masm);

 private:
  static void EntryHookTrampoline(intptr_t function,
                                  intptr_t stack_pointer,
                                  Isolate* isolate);

  virtual Major MajorKey() const V8_OVERRIDE { return ProfileEntryHook; }

  void Generate(MacroAssembler* masm);

  DISALLOW_COPY_AND_ASSIGN(ProfileEntryHookStub);
};


class CallDescriptors {
 public:
  static void InitializeForIsolate(Isolate* isolate);
};

} }  // namespace v8::internal

#endif  // V8_CODE_STUBS_H_
