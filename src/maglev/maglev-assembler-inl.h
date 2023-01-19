// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
#define V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_

#ifdef V8_TARGET_ARCH_ARM64
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
    std::apply(function,
               std::tuple_cat(std::make_tuple(masm), std::move(args)));
  }

 private:
  FunctionPointer function;
  Tuple args;
  RegList general_temporaries_;
  DoubleRegList double_temporaries_;
};

}  // namespace detail

template <typename Function, typename... Args>
inline DeferredCodeInfo* MaglevAssembler::PushDeferredCode(
    Function&& deferred_code_gen, Args&&... args) {
  using FunctionPointer =
      typename detail::FunctionArgumentsTupleHelper<Function>::FunctionPointer;
  static_assert(
      std::is_invocable_v<FunctionPointer, MaglevAssembler*,
                          decltype(detail::CopyForDeferred(
                              std::declval<MaglevCompilationInfo*>(),
                              std::declval<Args>()))...>,
      "Parameters of deferred_code_gen function should match arguments into "
      "PushDeferredCode");

  ScratchRegisterScope scratch_scope(this);
  using DeferredCodeInfoT = detail::DeferredCodeInfoImpl<Function>;
  DeferredCodeInfoT* deferred_code =
      compilation_info()->zone()->New<DeferredCodeInfoT>(
          compilation_info(), scratch_scope.Available(),
          scratch_scope.AvailableDouble(), deferred_code_gen,
          std::forward<Args>(args)...);

  code_gen_state()->PushDeferredCode(deferred_code);
  return deferred_code;
}

// Note this doesn't take capturing lambdas by design, since state may
// change until `deferred_code_gen` is actually executed. Use either a
// non-capturing lambda, or a plain function pointer.
template <typename Function, typename... Args>
inline void MaglevAssembler::JumpToDeferredIf(Condition cond,
                                              Function&& deferred_code_gen,
                                              Args&&... args) {
  DeferredCodeInfo* deferred_code = PushDeferredCode<Function, Args...>(
      std::forward<Function>(deferred_code_gen), std::forward<Args>(args)...);
  if (v8_flags.code_comments) {
    RecordComment("-- Jump to deferred code");
  }
  JumpIf(cond, &deferred_code->deferred_code_label);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
