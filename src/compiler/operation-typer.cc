// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/operation-typer.h"

#include "src/factory.h"
#include "src/isolate.h"
#include "src/type-cache.h"
#include "src/types.h"

#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

OperationTyper::OperationTyper(Isolate* isolate, Zone* zone)
    : zone_(zone), cache_(TypeCache::Get()) {
  Factory* factory = isolate->factory();
  singleton_false_ = Type::Constant(factory->false_value(), zone);
  singleton_true_ = Type::Constant(factory->true_value(), zone);
  singleton_the_hole_ = Type::Constant(factory->the_hole_value(), zone);
}

Type* OperationTyper::Merge(Type* left, Type* right) {
  return Type::Union(left, right, zone());
}

Type* OperationTyper::WeakenRange(Type* previous_range, Type* current_range) {
  static const double kWeakenMinLimits[] = {0.0,
                                            -1073741824.0,
                                            -2147483648.0,
                                            -4294967296.0,
                                            -8589934592.0,
                                            -17179869184.0,
                                            -34359738368.0,
                                            -68719476736.0,
                                            -137438953472.0,
                                            -274877906944.0,
                                            -549755813888.0,
                                            -1099511627776.0,
                                            -2199023255552.0,
                                            -4398046511104.0,
                                            -8796093022208.0,
                                            -17592186044416.0,
                                            -35184372088832.0,
                                            -70368744177664.0,
                                            -140737488355328.0,
                                            -281474976710656.0,
                                            -562949953421312.0};
  static const double kWeakenMaxLimits[] = {0.0,
                                            1073741823.0,
                                            2147483647.0,
                                            4294967295.0,
                                            8589934591.0,
                                            17179869183.0,
                                            34359738367.0,
                                            68719476735.0,
                                            137438953471.0,
                                            274877906943.0,
                                            549755813887.0,
                                            1099511627775.0,
                                            2199023255551.0,
                                            4398046511103.0,
                                            8796093022207.0,
                                            17592186044415.0,
                                            35184372088831.0,
                                            70368744177663.0,
                                            140737488355327.0,
                                            281474976710655.0,
                                            562949953421311.0};
  STATIC_ASSERT(arraysize(kWeakenMinLimits) == arraysize(kWeakenMaxLimits));

  double current_min = current_range->Min();
  double new_min = current_min;
  // Find the closest lower entry in the list of allowed
  // minima (or negative infinity if there is no such entry).
  if (current_min != previous_range->Min()) {
    new_min = -V8_INFINITY;
    for (double const min : kWeakenMinLimits) {
      if (min <= current_min) {
        new_min = min;
        break;
      }
    }
  }

  double current_max = current_range->Max();
  double new_max = current_max;
  // Find the closest greater entry in the list of allowed
  // maxima (or infinity if there is no such entry).
  if (current_max != previous_range->Max()) {
    new_max = V8_INFINITY;
    for (double const max : kWeakenMaxLimits) {
      if (max >= current_max) {
        new_max = max;
        break;
      }
    }
  }

  return Type::Range(new_min, new_max, zone());
}

Type* OperationTyper::Rangify(Type* type) {
  if (type->IsRange()) return type;  // Shortcut.
  if (!type->Is(cache_.kInteger)) {
    return type;  // Give up on non-integer types.
  }
  double min = type->Min();
  double max = type->Max();
  // Handle the degenerate case of empty bitset types (such as
  // OtherUnsigned31 and OtherSigned32 on 64-bit architectures).
  if (std::isnan(min)) {
    DCHECK(std::isnan(max));
    return type;
  }
  return Type::Range(min, max, zone());
}

namespace {

// Returns the array's least element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
double array_min(double a[], size_t n) {
  DCHECK(n != 0);
  double x = +V8_INFINITY;
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::min(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == 0 ? 0 : x;  // -0 -> 0
}

// Returns the array's greatest element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
double array_max(double a[], size_t n) {
  DCHECK(n != 0);
  double x = -V8_INFINITY;
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::max(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == 0 ? 0 : x;  // -0 -> 0
}

}  // namespace

Type* OperationTyper::AddRanger(double lhs_min, double lhs_max, double rhs_min,
                                double rhs_max) {
  double results[4];
  results[0] = lhs_min + rhs_min;
  results[1] = lhs_min + rhs_max;
  results[2] = lhs_max + rhs_min;
  results[3] = lhs_max + rhs_max;
  // Since none of the inputs can be -0, the result cannot be -0 either.
  // However, it can be nan (the sum of two infinities of opposite sign).
  // On the other hand, if none of the "results" above is nan, then the actual
  // result cannot be nan either.
  int nans = 0;
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(results[i])) ++nans;
  }
  if (nans == 4) return Type::NaN();  // [-inf..-inf] + [inf..inf] or vice versa
  Type* range =
      Type::Range(array_min(results, 4), array_max(results, 4), zone());
  return nans == 0 ? range : Type::Union(range, Type::NaN(), zone());
  // Examples:
  //   [-inf, -inf] + [+inf, +inf] = NaN
  //   [-inf, -inf] + [n, +inf] = [-inf, -inf] \/ NaN
  //   [-inf, +inf] + [n, +inf] = [-inf, +inf] \/ NaN
  //   [-inf, m] + [n, +inf] = [-inf, +inf] \/ NaN
}

Type* OperationTyper::SubtractRanger(RangeType* lhs, RangeType* rhs) {
  double results[4];
  results[0] = lhs->Min() - rhs->Min();
  results[1] = lhs->Min() - rhs->Max();
  results[2] = lhs->Max() - rhs->Min();
  results[3] = lhs->Max() - rhs->Max();
  // Since none of the inputs can be -0, the result cannot be -0.
  // However, it can be nan (the subtraction of two infinities of same sign).
  // On the other hand, if none of the "results" above is nan, then the actual
  // result cannot be nan either.
  int nans = 0;
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(results[i])) ++nans;
  }
  if (nans == 4) return Type::NaN();  // [inf..inf] - [inf..inf] (all same sign)
  Type* range =
      Type::Range(array_min(results, 4), array_max(results, 4), zone());
  return nans == 0 ? range : Type::Union(range, Type::NaN(), zone());
  // Examples:
  //   [-inf, +inf] - [-inf, +inf] = [-inf, +inf] \/ NaN
  //   [-inf, -inf] - [-inf, -inf] = NaN
  //   [-inf, -inf] - [n, +inf] = [-inf, -inf] \/ NaN
  //   [m, +inf] - [-inf, n] = [-inf, +inf] \/ NaN
}

Type* OperationTyper::ModulusRanger(RangeType* lhs, RangeType* rhs) {
  double lmin = lhs->Min();
  double lmax = lhs->Max();
  double rmin = rhs->Min();
  double rmax = rhs->Max();

  double labs = std::max(std::abs(lmin), std::abs(lmax));
  double rabs = std::max(std::abs(rmin), std::abs(rmax)) - 1;
  double abs = std::min(labs, rabs);
  bool maybe_minus_zero = false;
  double omin = 0;
  double omax = 0;
  if (lmin >= 0) {  // {lhs} positive.
    omin = 0;
    omax = abs;
  } else if (lmax <= 0) {  // {lhs} negative.
    omin = 0 - abs;
    omax = 0;
    maybe_minus_zero = true;
  } else {
    omin = 0 - abs;
    omax = abs;
    maybe_minus_zero = true;
  }

  Type* result = Type::Range(omin, omax, zone());
  if (maybe_minus_zero) result = Type::Union(result, Type::MinusZero(), zone());
  return result;
}

Type* OperationTyper::MultiplyRanger(Type* lhs, Type* rhs) {
  double results[4];
  double lmin = lhs->AsRange()->Min();
  double lmax = lhs->AsRange()->Max();
  double rmin = rhs->AsRange()->Min();
  double rmax = rhs->AsRange()->Max();
  results[0] = lmin * rmin;
  results[1] = lmin * rmax;
  results[2] = lmax * rmin;
  results[3] = lmax * rmax;
  // If the result may be nan, we give up on calculating a precise type,
  // because
  // the discontinuity makes it too complicated.  Note that even if none of
  // the
  // "results" above is nan, the actual result may still be, so we have to do
  // a
  // different check:
  bool maybe_nan = (lhs->Maybe(cache_.kSingletonZero) &&
                    (rmin == -V8_INFINITY || rmax == +V8_INFINITY)) ||
                   (rhs->Maybe(cache_.kSingletonZero) &&
                    (lmin == -V8_INFINITY || lmax == +V8_INFINITY));
  if (maybe_nan) return cache_.kIntegerOrMinusZeroOrNaN;  // Giving up.
  bool maybe_minuszero = (lhs->Maybe(cache_.kSingletonZero) && rmin < 0) ||
                         (rhs->Maybe(cache_.kSingletonZero) && lmin < 0);
  Type* range =
      Type::Range(array_min(results, 4), array_max(results, 4), zone());
  return maybe_minuszero ? Type::Union(range, Type::MinusZero(), zone())
                         : range;
}

Type* OperationTyper::ToNumber(Type* type) {
  if (type->Is(Type::Number())) return type;
  if (type->Is(Type::NullOrUndefined())) {
    if (type->Is(Type::Null())) return cache_.kSingletonZero;
    if (type->Is(Type::Undefined())) return Type::NaN();
    return Type::Union(Type::NaN(), cache_.kSingletonZero, zone());
  }
  if (type->Is(Type::NumberOrUndefined())) {
    return Type::Union(Type::Intersect(type, Type::Number(), zone()),
                       Type::NaN(), zone());
  }
  if (type->Is(singleton_false_)) return cache_.kSingletonZero;
  if (type->Is(singleton_true_)) return cache_.kSingletonOne;
  if (type->Is(Type::Boolean())) return cache_.kZeroOrOne;
  if (type->Is(Type::BooleanOrNumber())) {
    return Type::Union(Type::Intersect(type, Type::Number(), zone()),
                       cache_.kZeroOrOne, zone());
  }
  return Type::Number();
}

Type* OperationTyper::NumericAdd(Type* lhs, Type* rhs) {
  DCHECK(lhs->Is(Type::Number()));
  DCHECK(rhs->Is(Type::Number()));

  // We can give more precise types for integers.
  if (!lhs->Is(cache_.kIntegerOrMinusZeroOrNaN) ||
      !rhs->Is(cache_.kIntegerOrMinusZeroOrNaN)) {
    return Type::Number();
  }
  Type* int_lhs = Type::Intersect(lhs, cache_.kInteger, zone());
  Type* int_rhs = Type::Intersect(rhs, cache_.kInteger, zone());
  Type* result =
      AddRanger(int_lhs->Min(), int_lhs->Max(), int_rhs->Min(), int_rhs->Max());
  if (lhs->Maybe(Type::NaN()) || rhs->Maybe(Type::NaN())) {
    result = Type::Union(result, Type::NaN(), zone());
  }
  if (lhs->Maybe(Type::MinusZero()) && rhs->Maybe(Type::MinusZero())) {
    result = Type::Union(result, Type::MinusZero(), zone());
  }
  return result;
}

Type* OperationTyper::NumericSubtract(Type* lhs, Type* rhs) {
  DCHECK(lhs->Is(Type::Number()));
  DCHECK(rhs->Is(Type::Number()));

  lhs = Rangify(lhs);
  rhs = Rangify(rhs);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  if (lhs->IsRange() && rhs->IsRange()) {
    return SubtractRanger(lhs->AsRange(), rhs->AsRange());
  }
  // TODO(neis): Deal with numeric bitsets here and elsewhere.
  return Type::Number();
}

Type* OperationTyper::NumericMultiply(Type* lhs, Type* rhs) {
  DCHECK(lhs->Is(Type::Number()));
  DCHECK(rhs->Is(Type::Number()));
  lhs = Rangify(lhs);
  rhs = Rangify(rhs);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  if (lhs->IsRange() && rhs->IsRange()) {
    return MultiplyRanger(lhs, rhs);
  }
  return Type::Number();
}

Type* OperationTyper::NumericDivide(Type* lhs, Type* rhs) {
  DCHECK(lhs->Is(Type::Number()));
  DCHECK(rhs->Is(Type::Number()));

  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // Division is tricky, so all we do is try ruling out nan.
  bool maybe_nan =
      lhs->Maybe(Type::NaN()) || rhs->Maybe(cache_.kZeroish) ||
      ((lhs->Min() == -V8_INFINITY || lhs->Max() == +V8_INFINITY) &&
       (rhs->Min() == -V8_INFINITY || rhs->Max() == +V8_INFINITY));
  return maybe_nan ? Type::Number() : Type::OrderedNumber();
}

Type* OperationTyper::NumericModulus(Type* lhs, Type* rhs) {
  DCHECK(lhs->Is(Type::Number()));
  DCHECK(rhs->Is(Type::Number()));
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();

  if (lhs->Maybe(Type::NaN()) || rhs->Maybe(cache_.kZeroish) ||
      lhs->Min() == -V8_INFINITY || lhs->Max() == +V8_INFINITY) {
    // Result maybe NaN.
    return Type::Number();
  }

  lhs = Rangify(lhs);
  rhs = Rangify(rhs);
  if (lhs->IsRange() && rhs->IsRange()) {
    return ModulusRanger(lhs->AsRange(), rhs->AsRange());
  }
  return Type::OrderedNumber();
}

Type* OperationTyper::ToPrimitive(Type* type) {
  if (type->Is(Type::Primitive()) && !type->Maybe(Type::Receiver())) {
    return type;
  }
  return Type::Primitive();
}

Type* OperationTyper::Invert(Type* type) {
  DCHECK(type->Is(Type::Boolean()));
  DCHECK(type->IsInhabited());
  if (type->Is(singleton_false())) return singleton_true();
  if (type->Is(singleton_true())) return singleton_false();
  return type;
}

OperationTyper::ComparisonOutcome OperationTyper::Invert(
    ComparisonOutcome outcome) {
  ComparisonOutcome result(0);
  if ((outcome & kComparisonUndefined) != 0) result |= kComparisonUndefined;
  if ((outcome & kComparisonTrue) != 0) result |= kComparisonFalse;
  if ((outcome & kComparisonFalse) != 0) result |= kComparisonTrue;
  return result;
}

Type* OperationTyper::FalsifyUndefined(ComparisonOutcome outcome) {
  if ((outcome & kComparisonFalse) != 0 ||
      (outcome & kComparisonUndefined) != 0) {
    return (outcome & kComparisonTrue) != 0 ? Type::Boolean()
                                            : singleton_false();
  }
  // Type should be non empty, so we know it should be true.
  DCHECK((outcome & kComparisonTrue) != 0);
  return singleton_true();
}

Type* OperationTyper::TypeJSAdd(Type* lhs, Type* rhs) {
  lhs = ToPrimitive(lhs);
  rhs = ToPrimitive(rhs);
  if (lhs->Maybe(Type::String()) || rhs->Maybe(Type::String())) {
    if (lhs->Is(Type::String()) || rhs->Is(Type::String())) {
      return Type::String();
    } else {
      return Type::NumberOrString();
    }
  }
  lhs = ToNumber(lhs);
  rhs = ToNumber(rhs);
  return NumericAdd(lhs, rhs);
}

Type* OperationTyper::TypeJSSubtract(Type* lhs, Type* rhs) {
  return NumericSubtract(ToNumber(lhs), ToNumber(rhs));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
