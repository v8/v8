// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
#define V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_

#include <type_traits>

#include "src/maglev/maglev-assembler.h"

#ifdef V8_TARGET_ARCH_ARM
#include "src/maglev/arm/maglev-assembler-arm-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/maglev/arm64/maglev-assembler-arm64-inl.h"
#elif V8_TARGET_ARCH_X64
#include "src/maglev/x64/maglev-assembler-x64-inl.h"
#else
#error "Maglev does not supported this architecture."
#endif

namespace v8 {
namespace internal {
namespace maglev {

namespace detail {

// Base case provides an error.
template <typename T, typename Enable = void>
struct CopyForDeferredHelper {
  template <typename U>
  struct No_Copy_Helper_Implemented_For_Type;
  static void Copy(MaglevCompilationInfo* compilation_info,
                   No_Copy_Helper_Implemented_For_Type<T>);
};

// Helper for copies by value.
template <typename T, typename Enable = void>
struct CopyForDeferredByValue {
  static T Copy(MaglevCompilationInfo* compilation_info, T node) {
    return node;
  }
};

// Node pointers are copied by value.
template <typename T>
struct CopyForDeferredHelper<
    T*, typename std::enable_if<std::is_base_of<NodeBase, T>::value>::type>
    : public CopyForDeferredByValue<T*> {};
// Arithmetic values and enums are copied by value.
template <typename T>
struct CopyForDeferredHelper<
    T, typename std::enable_if<std::is_arithmetic<T>::value>::type>
    : public CopyForDeferredByValue<T> {};
template <typename T>
struct CopyForDeferredHelper<
    T, typename std::enable_if<std::is_enum<T>::value>::type>
    : public CopyForDeferredByValue<T> {};
// MaglevCompilationInfos are copied by value.
template <>
struct CopyForDeferredHelper<MaglevCompilationInfo*>
    : public CopyForDeferredByValue<MaglevCompilationInfo*> {};
// Machine registers are copied by value.
template <>
struct CopyForDeferredHelper<Register>
    : public CopyForDeferredByValue<Register> {};
template <>
struct CopyForDeferredHelper<DoubleRegister>
    : public CopyForDeferredByValue<DoubleRegister> {};
// Bytecode offsets are copied by value.
template <>
struct CopyForDeferredHelper<BytecodeOffset>
    : public CopyForDeferredByValue<BytecodeOffset> {};
// EagerDeoptInfo pointers are copied by value.
template <>
struct CopyForDeferredHelper<EagerDeoptInfo*>
    : public CopyForDeferredByValue<EagerDeoptInfo*> {};
// LazyDeoptInfo pointers are copied by value.
template <>
struct CopyForDeferredHelper<LazyDeoptInfo*>
    : public CopyForDeferredByValue<LazyDeoptInfo*> {};
// ZoneLabelRef is copied by value.
template <>
struct CopyForDeferredHelper<ZoneLabelRef>
    : public CopyForDeferredByValue<ZoneLabelRef> {};
// RegList are copied by value.
template <>
struct CopyForDeferredHelper<RegList> : public CopyForDeferredByValue<RegList> {
};
// Register snapshots are copied by value.
template <>
struct CopyForDeferredHelper<RegisterSnapshot>
    : public CopyForDeferredByValue<RegisterSnapshot> {};
// Feedback slots are copied by value.
template <>
struct CopyForDeferredHelper<FeedbackSlot>
    : public CopyForDeferredByValue<FeedbackSlot> {};
// Heap Refs are copied by value.
template <typename T>
struct CopyForDeferredHelper<T, typename std::enable_if<std::is_base_of<
                                    compiler::ObjectRef, T>::value>::type>
    : public CopyForDeferredByValue<T> {};

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, T&& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info,
                                        std::forward<T>(value));
}

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, T& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info, value);
}

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, const T& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info, value);
}

template <typename Function>
struct FunctionArgumentsTupleHelper
    : public FunctionArgumentsTupleHelper<decltype(&Function::operator())> {};

template <typename C, typename R, typename... A>
struct FunctionArgumentsTupleHelper<R (C::*)(A...) const> {
  using FunctionPointer = R (*)(A...);
  using Tuple = std::tuple<A...>;
  static constexpr size_t kSize = sizeof...(A);
};

template <typename R, typename... A>
struct FunctionArgumentsTupleHelper<R (&)(A...)> {
  using FunctionPointer = R (*)(A...);
  using Tuple = std::tuple<A...>;
  static constexpr size_t kSize = sizeof...(A);
};

template <typename T>
struct StripFirstTupleArg;

template <typename T1, typename... T>
struct StripFirstTupleArg<std::tuple<T1, T...>> {
  using Stripped = std::tuple<T...>;
};

template <typename Function>
class DeferredCodeInfoImpl final : public DeferredCodeInfo {
 public:
  using FunctionPointer =
      typename FunctionArgumentsTupleHelper<Function>::FunctionPointer;
  using Tuple = typename StripFirstTupleArg<
      typename FunctionArgumentsTupleHelper<Function>::Tuple>::Stripped;

  template <typename... InArgs>
  explicit DeferredCodeInfoImpl(MaglevCompilationInfo* compilation_info,
                                RegList general_temporaries,
                                DoubleRegList double_temporaries,
                                FunctionPointer function, InArgs&&... args)
      : function(function),
        args(CopyForDeferred(compilation_info, std::forward<InArgs>(args))...),
        general_temporaries_(general_temporaries),
        double_temporaries_(double_temporaries) {}

  DeferredCodeInfoImpl(DeferredCodeInfoImpl&&) = delete;
  DeferredCodeInfoImpl(const DeferredCodeInfoImpl&) = delete;

  void Generate(MaglevAssembler* masm) override {
    MaglevAssembler::ScratchRegisterScope scratch_scope(masm);
    scratch_scope.SetAvailable(general_temporaries_);
    scratch_scope.SetAvailableDouble(double_temporaries_);
#ifdef DEBUG
    masm->set_allow_call(allow_call_);
    masm->set_allow_deferred_call(allow_call_);
    masm->set_allow_allocate(allow_allocate_);
#endif  // DEBUG
    std::apply(function,
               std::tuple_cat(std::make_tuple(masm), std::move(args)));
#ifdef DEBUG
    masm->set_allow_call(false);
    masm->set_allow_deferred_call(false);
    masm->set_allow_allocate(false);
#endif  // DEBUG
  }

#ifdef DEBUG
  void set_allow_call(bool value) { allow_call_ = value; }
  void set_allow_allocate(bool value) { allow_allocate_ = value; }
#endif  // DEBUG

 private:
  FunctionPointer function;
  Tuple args;
  RegList general_temporaries_;
  DoubleRegList double_temporaries_;

#ifdef DEBUG
  bool allow_call_ = false;
  bool allow_allocate_ = false;
#endif  // DEBUG
};

}  // namespace detail

template <typename Function, typename... Args>
inline Label* MaglevAssembler::MakeDeferredCode(Function&& deferred_code_gen,
                                                Args&&... args) {
  using FunctionPointer =
      typename detail::FunctionArgumentsTupleHelper<Function>::FunctionPointer;
  static_assert(
      std::is_invocable_v<FunctionPointer, MaglevAssembler*,
                          decltype(detail::CopyForDeferred(
                              std::declval<MaglevCompilationInfo*>(),
                              std::declval<Args>()))...>,
      "Parameters of deferred_code_gen function should match arguments into "
      "MakeDeferredCode");

  ScratchRegisterScope scratch_scope(this);
  using DeferredCodeInfoT = detail::DeferredCodeInfoImpl<Function>;
  DeferredCodeInfoT* deferred_code =
      compilation_info()->zone()->New<DeferredCodeInfoT>(
          compilation_info(), scratch_scope.Available(),
          scratch_scope.AvailableDouble(), deferred_code_gen,
          std::forward<Args>(args)...);

#ifdef DEBUG
  deferred_code->set_allow_call(allow_deferred_call_);
  deferred_code->set_allow_allocate(allow_allocate_);
#endif  // DEBUG

  code_gen_state()->PushDeferredCode(deferred_code);
  return &deferred_code->deferred_code_label;
}

// Note this doesn't take capturing lambdas by design, since state may
// change until `deferred_code_gen` is actually executed. Use either a
// non-capturing lambda, or a plain function pointer.
template <typename Function, typename... Args>
inline void MaglevAssembler::JumpToDeferredIf(Condition cond,
                                              Function&& deferred_code_gen,
                                              Args&&... args) {
  if (v8_flags.code_comments) {
    RecordComment("-- Jump to deferred code");
  }
  JumpIf(cond, MakeDeferredCode<Function, Args...>(
                   std::forward<Function>(deferred_code_gen),
                   std::forward<Args>(args)...));
}

inline void MaglevAssembler::SmiToDouble(DoubleRegister result, Register smi) {
  AssertSmi(smi);
  SmiUntag(smi);
  Int32ToDouble(result, smi);
}

inline void MaglevAssembler::Branch(Condition condition, BasicBlock* if_true,
                                    BasicBlock* if_false,
                                    BasicBlock* next_block) {
  Branch(condition, if_true->label(), Label::kFar, if_true == next_block,
         if_false->label(), Label::kFar, if_false == next_block);
}

inline void MaglevAssembler::Branch(Condition condition, Label* if_true,
                                    Label::Distance true_distance,
                                    bool fallthrough_when_true, Label* if_false,
                                    Label::Distance false_distance,
                                    bool fallthrough_when_false) {
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true, if_false);
      return;
    }
    // Jump over the false block if true, otherwise fall through into it.
    JumpIf(condition, if_true, true_distance);
  } else {
    // Jump to the false block if true.
    JumpIf(NegateCondition(condition), if_false, false_distance);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      Jump(if_true, true_distance);
    }
  }
}

inline void MaglevAssembler::LoadTaggedField(Register result,
                                             MemOperand operand) {
  MacroAssembler::LoadTaggedField(result, operand);
}

inline void MaglevAssembler::LoadTaggedField(Register result, Register object,
                                             int offset) {
  MacroAssembler::LoadTaggedField(result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadTaggedFieldWithoutDecompressing(
    Register result, Register object, int offset) {
  MacroAssembler::LoadTaggedFieldWithoutDecompressing(
      result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadTaggedSignedField(Register result,
                                                   MemOperand operand) {
  MacroAssembler::LoadTaggedField(result, operand);
}

inline void MaglevAssembler::LoadTaggedSignedField(Register result,
                                                   Register object,
                                                   int offset) {
  MacroAssembler::LoadTaggedField(result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::CallBuiltin(Builtin builtin) {
  // Special case allowing calls to DoubleToI, which takes care to preserve all
  // registers and therefore doesn't require special spill handling.
  DCHECK(allow_call() || builtin == Builtin::kDoubleToI);
  MacroAssembler::CallBuiltin(builtin);
}

inline void MaglevAssembler::CallRuntime(Runtime::FunctionId fid) {
  DCHECK(allow_call());
  MacroAssembler::CallRuntime(fid);
}

inline void MaglevAssembler::CallRuntime(Runtime::FunctionId fid,
                                         int num_args) {
  DCHECK(allow_call());
  MacroAssembler::CallRuntime(fid, num_args);
}

inline void MaglevAssembler::SetMapAsRoot(Register object, RootIndex map) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.GetDefaultScratchRegister();
  LoadTaggedRoot(scratch, map);
  StoreTaggedFieldNoWriteBarrier(object, HeapObject::kMapOffset, scratch);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
