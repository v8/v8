// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

template <typename Next>
class TypedOptimizationsReducerImpl : public Next {
 public:
  using Next::Asm;
  template <typename... Args>
  explicit TypedOptimizationsReducerImpl(const std::tuple<Args...>& args)
      : Next(args), types_(Asm().output_graph().operation_types()) {}

  template <Opcode opcode, typename Continuation, typename... Args>
  OpIndex ReduceOperation(Args... args) {
    OpIndex index = Continuation{this}.Reduce(args...);
    if (!index.valid()) return index;

    if constexpr (opcode == Opcode::kConstant) {
      return index;
    } else {
      Type type = GetType(index);
      if (type.IsInvalid()) return index;

      switch (type.kind()) {
        case Type::Kind::kWord32: {
          auto w32 = type.AsWord32();
          if (auto c = w32.try_get_constant()) {
            return Asm().Word32Constant(*c);
          }
          break;
        }
        case Type::Kind::kWord64: {
          auto w64 = type.AsWord64();
          if (auto c = w64.try_get_constant()) {
            return Asm().Word64Constant(*c);
          }
          break;
        }
        case Type::Kind::kFloat32: {
          auto f32 = type.AsFloat32();
          if (f32.is_only_nan()) {
            return Asm().Float32Constant(nan_v<32>);
          }
          if (auto c = f32.try_get_constant()) {
            return Asm().Float32Constant(*c);
          }
          break;
        }
        case Type::Kind::kFloat64: {
          auto f64 = type.AsFloat64();
          if (f64.is_only_nan()) {
            return Asm().Float64Constant(nan_v<64>);
          }
          if (auto c = f64.try_get_constant()) {
            return Asm().Float64Constant(*c);
          }
          break;
        }
        default:
          break;
      }

      // Keep unchanged.
      return index;
    }
  }

  Type GetType(const OpIndex index) { return types_[index]; }

 private:
  GrowingSidetable<Type>& types_;
};

template <typename Next>
using TypedOptimizationsReducer =
    UniformReducerAdapter<TypedOptimizationsReducerImpl, Next>;

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPED_OPTIMIZATIONS_REDUCER_H_
