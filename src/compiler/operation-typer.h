// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATION_TYPER_H_
#define V8_COMPILER_OPERATION_TYPER_H_

#include "src/base/flags.h"
#include "src/compiler/opcodes.h"

namespace v8 {
namespace internal {

class Isolate;
class RangeType;
class Type;
class TypeCache;
class Zone;

namespace compiler {

class OperationTyper {
 public:
  OperationTyper(Isolate* isolate, Zone* zone);

  // Typing Phi.
  Type* Merge(Type* left, Type* right);

  Type* ToPrimitive(Type* type);

  // Helpers for number operation typing.
  Type* ToNumber(Type* type);
  Type* WeakenRange(Type* current_range, Type* previous_range);

  Type* NumericAdd(Type* lhs, Type* rhs);
  Type* NumericSubtract(Type* lhs, Type* rhs);
  Type* NumericMultiply(Type* lhs, Type* rhs);
  Type* NumericDivide(Type* lhs, Type* rhs);
  Type* NumericModulus(Type* lhs, Type* rhs);

  enum ComparisonOutcomeFlags {
    kComparisonTrue = 1,
    kComparisonFalse = 2,
    kComparisonUndefined = 4
  };

// Javascript binop typers.
#define DECLARE_CASE(x) Type* Type##x(Type* lhs, Type* rhs);
  JS_SIMPLE_BINOP_LIST(DECLARE_CASE)
#undef DECLARE_CASE

  Type* singleton_false() { return singleton_false_; }
  Type* singleton_true() { return singleton_true_; }
  Type* singleton_the_hole() { return singleton_the_hole_; }

 private:
  typedef base::Flags<ComparisonOutcomeFlags> ComparisonOutcome;

  ComparisonOutcome Invert(ComparisonOutcome);
  Type* Invert(Type*);
  Type* FalsifyUndefined(ComparisonOutcome);

  Type* Rangify(Type*);
  Type* AddRanger(double lhs_min, double lhs_max, double rhs_min,
                  double rhs_max);
  Type* SubtractRanger(RangeType* lhs, RangeType* rhs);
  Type* MultiplyRanger(Type* lhs, Type* rhs);
  Type* ModulusRanger(RangeType* lhs, RangeType* rhs);

  Zone* zone() { return zone_; }

  Zone* zone_;
  TypeCache const& cache_;

  Type* singleton_false_;
  Type* singleton_true_;
  Type* singleton_the_hole_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_OPERATION_TYPER_H_
