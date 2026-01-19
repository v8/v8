// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-iterator-inl.h"
#include "src/builtins/builtins-math-xsum.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/execution/execution.h"
#include "src/execution/protectors-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

class SumPreciseState {
 public:
  void Update(double n) {
    if (state_ == State::kNaN) return;

    if (std::isnan(n)) {
      state_ = State::kNaN;
    } else if (std::isinf(n)) {
      if (n > 0) {
        if (state_ == State::kMinusInfinity) {
          state_ = State::kNaN;
        } else {
          state_ = State::kPlusInfinity;
        }
      } else {
        if (state_ == State::kPlusInfinity) {
          state_ = State::kNaN;
        } else {
          state_ = State::kMinusInfinity;
        }
      }
    } else {
      // Finite number.
      if (!IsMinusZero(n)) {
        if (state_ == State::kMinusZero || state_ == State::kFinite) {
          state_ = State::kFinite;
          xsum_.Add(n);
        }
      }
    }
  }

  bool IsFinite() const { return state_ == State::kFinite; }
  bool IsNaN() const { return state_ == State::kNaN; }
  void SetFinite() {
    if (state_ == State::kMinusZero) state_ = State::kFinite;
  }
  void UpdateFinite(double n) {
    if (state_ == State::kMinusZero) state_ = State::kFinite;
    xsum_.Add(n);
  }

  Tagged<Object> Result(Isolate* isolate) {
    switch (state_) {
      case State::kNaN:
        return ReadOnlyRoots(isolate).nan_value();
      case State::kPlusInfinity:
        return *isolate->factory()->NewNumber(V8_INFINITY);
      case State::kMinusInfinity:
        return *isolate->factory()->NewNumber(-V8_INFINITY);
      case State::kMinusZero:
        return ReadOnlyRoots(isolate).minus_zero_value();
      case State::kFinite:
        return *isolate->factory()->NewNumber(xsum_.Round());
    }
    UNREACHABLE();
  }

 private:
  Xsum xsum_;
  enum class State { kMinusZero, kFinite, kPlusInfinity, kMinusInfinity, kNaN };
  State state_ = State::kMinusZero;
};

}  // namespace

BUILTIN(MathSumPrecise) {
  HandleScope scope(isolate);
  Handle<Object> items = args.atOrUndefined(isolate, 1);

  // 1. Perform ? RequireObjectCoercible(items).
  if (IsNullOrUndefined(*items, isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNullOrUndefined,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Math.sumPrecise")));
  }

  SumPreciseState state;

  auto smi_visitor = [&](int32_t val) -> bool {
    state.UpdateFinite(val);
    return true;
  };

  auto double_visitor = [&](double val) -> bool {
    state.Update(val);
    return true;
  };

  auto generic_visitor = [&](Tagged<Object> val) -> bool {
    if (!IsNumber(val)) {
      DirectHandle<Object> error_args[] = {
          isolate->factory()->NewStringFromAsciiChecked("Iterator value"),
          Object::TypeOf(isolate, handle(val, isolate))};
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kIsNotNumber, base::VectorOf(error_args)));
      return false;
    }
    state.Update(Object::NumberValue(val));
    return true;
  };

  if (IterableForEach(isolate, items, smi_visitor, double_visitor,
                      generic_visitor, kMaxSafeIntegerUint64)
          .is_null()) {
    return ReadOnlyRoots(isolate).exception();
  }

  return state.Result(isolate);
}

}  // namespace internal
}  // namespace v8
