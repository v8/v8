// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_
#define V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_

#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

// UniformReducerAdapater allows to handle all operations uniformly during a
// reduction by wiring all ReduceXyz calls through a single ReduceOperation
// method. This is how to use it (MyReducer can then be used in a ReducerStack
// like any other reducer):
//
// template <typename Next>
// class MyReducerImpl : public Next {
//  public:
//   using Next::Asm;
//   template <typename... Args>
//   explicit MyReducerImpl(const std::tuple<Args...>& args)
//       : Next(args) { /* ... */ }
//
//   template <Opcode opcode, typename Continuation, typename... Args>
//   OpIndex ReduceOperation(Args... args) {
//
//     /* ... */
//
//     // Forward to Next reducer.
//     OpIndex index = Continuation{this}.Reduce(args...);
//
//     /* ... */
//
//     return index;
//   }
//
//  private:
//   /* ... */
// };
//
// template <typename Next>
// using MyReducer = UniformReducerAdapater<MyReducerImpl, Next>;
//
template <template <typename> typename Impl, typename Next>
class UniformReducerAdapter : public Impl<Next> {
 public:
  template <typename... Args>
  explicit UniformReducerAdapter(const std::tuple<Args...>& args)
      : Impl<Next>(args) {}

#define REDUCE(op)                                                         \
  struct Reduce##op##Continuation final {                                  \
    explicit Reduce##op##Continuation(Next* _this) : this_(_this) {}       \
    template <typename... Args>                                            \
    OpIndex Reduce(Args... args) const {                                   \
      return this_->Reduce##op(args...);                                   \
    }                                                                      \
    Next* this_;                                                           \
  };                                                                       \
  template <typename... Args>                                              \
  OpIndex Reduce##op(Args... args) {                                       \
    return Impl<Next>::template ReduceOperation<Opcode::k##op,             \
                                                Reduce##op##Continuation>( \
        args...);                                                          \
  }
  TURBOSHAFT_OPERATION_LIST(REDUCE)
#undef REDUCE
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_UNIFORM_REDUCER_ADAPTER_H_
