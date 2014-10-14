// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-inl.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/typer.h"

namespace v8 {
namespace internal {
namespace compiler {

Typer::Typer(Zone* zone) : zone_(zone) {
  Factory* f = zone->isolate()->factory();

  Handle<Object> zero = f->NewNumber(0);
  Handle<Object> one = f->NewNumber(1);
  Handle<Object> positive_infinity = f->NewNumber(+V8_INFINITY);
  Handle<Object> negative_infinity = f->NewNumber(-V8_INFINITY);

  negative_signed32 = Type::Union(
      Type::SignedSmall(), Type::OtherSigned32(), zone);
  non_negative_signed32 = Type::Union(
      Type::UnsignedSmall(), Type::OtherUnsigned31(), zone);
  undefined_or_null = Type::Union(Type::Undefined(), Type::Null(), zone);
  singleton_false = Type::Constant(f->false_value(), zone);
  singleton_true = Type::Constant(f->true_value(), zone);
  singleton_zero = Type::Range(zero, zero, zone);
  singleton_one = Type::Range(one, one, zone);
  zero_or_one = Type::Union(singleton_zero, singleton_one, zone);
  zeroish = Type::Union(
      singleton_zero, Type::Union(Type::NaN(), Type::MinusZero(), zone), zone);
  falsish = Type::Union(Type::Undetectable(),
      Type::Union(zeroish, undefined_or_null, zone), zone);
  integer = Type::Range(negative_infinity, positive_infinity, zone);

  Type* number = Type::Number();
  Type* signed32 = Type::Signed32();
  Type* unsigned32 = Type::Unsigned32();
  Type* integral32 = Type::Integral32();
  Type* object = Type::Object();
  Type* undefined = Type::Undefined();
  Type* weakint = Type::Union(
      integer, Type::Union(Type::NaN(), Type::MinusZero(), zone), zone);

  number_fun0_ = Type::Function(number, zone);
  number_fun1_ = Type::Function(number, number, zone);
  number_fun2_ = Type::Function(number, number, number, zone);
  weakint_fun1_ = Type::Function(weakint, number, zone);
  imul_fun_ = Type::Function(signed32, integral32, integral32, zone);
  random_fun_ = Type::Function(Type::Union(
      Type::UnsignedSmall(), Type::OtherNumber(), zone), zone);

  Type* int8 = Type::Intersect(
      Type::Range(f->NewNumber(-0x7F), f->NewNumber(0x7F-1), zone),
      Type::UntaggedInt8(), zone);
  Type* int16 = Type::Intersect(
      Type::Range(f->NewNumber(-0x7FFF), f->NewNumber(0x7FFF-1), zone),
      Type::UntaggedInt16(), zone);
  Type* uint8 = Type::Intersect(
      Type::Range(zero, f->NewNumber(0xFF-1), zone),
      Type::UntaggedInt8(), zone);
  Type* uint16 = Type::Intersect(
      Type::Range(zero, f->NewNumber(0xFFFF-1), zone),
      Type::UntaggedInt16(), zone);

#define NATIVE_TYPE(sem, rep) \
    Type::Intersect(Type::sem(), Type::rep(), zone)
  Type* int32 = NATIVE_TYPE(Signed32, UntaggedInt32);
  Type* uint32 = NATIVE_TYPE(Unsigned32, UntaggedInt32);
  Type* float32 = NATIVE_TYPE(Number, UntaggedFloat32);
  Type* float64 = NATIVE_TYPE(Number, UntaggedFloat64);
#undef NATIVE_TYPE

  Type* buffer = Type::Buffer(zone);
  Type* int8_array = Type::Array(int8, zone);
  Type* int16_array = Type::Array(int16, zone);
  Type* int32_array = Type::Array(int32, zone);
  Type* uint8_array = Type::Array(uint8, zone);
  Type* uint16_array = Type::Array(uint16, zone);
  Type* uint32_array = Type::Array(uint32, zone);
  Type* float32_array = Type::Array(float32, zone);
  Type* float64_array = Type::Array(float64, zone);
  Type* arg1 = Type::Union(unsigned32, object, zone);
  Type* arg2 = Type::Union(unsigned32, undefined, zone);
  Type* arg3 = arg2;
  array_buffer_fun_ = Type::Function(buffer, unsigned32, zone);
  int8_array_fun_ = Type::Function(int8_array, arg1, arg2, arg3, zone);
  int16_array_fun_ = Type::Function(int16_array, arg1, arg2, arg3, zone);
  int32_array_fun_ = Type::Function(int32_array, arg1, arg2, arg3, zone);
  uint8_array_fun_ = Type::Function(uint8_array, arg1, arg2, arg3, zone);
  uint16_array_fun_ = Type::Function(uint16_array, arg1, arg2, arg3, zone);
  uint32_array_fun_ = Type::Function(uint32_array, arg1, arg2, arg3, zone);
  float32_array_fun_ = Type::Function(float32_array, arg1, arg2, arg3, zone);
  float64_array_fun_ = Type::Function(float64_array, arg1, arg2, arg3, zone);
}


class Typer::Visitor : public NullNodeVisitor {
 public:
  Visitor(Typer* typer, MaybeHandle<Context> context)
      : typer_(typer), context_(context) {}

  Bounds TypeNode(Node* node) {
    switch (node->opcode()) {
#define DECLARE_CASE(x) \
      case IrOpcode::k##x: return TypeBinaryOp(node, x##Typer);
      JS_SIMPLE_BINOP_LIST(DECLARE_CASE)
#undef DECLARE_CASE

#define DECLARE_CASE(x) case IrOpcode::k##x: return Type##x(node);
      DECLARE_CASE(Start)
      // VALUE_OP_LIST without JS_SIMPLE_BINOP_LIST:
      COMMON_OP_LIST(DECLARE_CASE)
      SIMPLIFIED_OP_LIST(DECLARE_CASE)
      MACHINE_OP_LIST(DECLARE_CASE)
      JS_SIMPLE_UNOP_LIST(DECLARE_CASE)
      JS_OBJECT_OP_LIST(DECLARE_CASE)
      JS_CONTEXT_OP_LIST(DECLARE_CASE)
      JS_OTHER_OP_LIST(DECLARE_CASE)
#undef DECLARE_CASE

#define DECLARE_CASE(x) case IrOpcode::k##x:
      DECLARE_CASE(End)
      INNER_CONTROL_OP_LIST(DECLARE_CASE)
#undef DECLARE_CASE
      break;
    }
    UNREACHABLE();
    return Bounds();
  }

  Type* TypeConstant(Handle<Object> value);

 protected:
#define DECLARE_METHOD(x) inline Bounds Type##x(Node* node);
  DECLARE_METHOD(Start)
  VALUE_OP_LIST(DECLARE_METHOD)
#undef DECLARE_METHOD

  static Bounds OperandType(Node* node, int i) {
    return NodeProperties::GetBounds(NodeProperties::GetValueInput(node, i));
  }

  static Type* ContextType(Node* node) {
    Bounds result =
        NodeProperties::GetBounds(NodeProperties::GetContextInput(node));
    DCHECK(result.upper->Maybe(Type::Internal()));
    // TODO(rossberg): More precisely, instead of the above assertion, we should
    // back-propagate the constraint that it has to be a subtype of Internal.
    return result.upper;
  }

  Zone* zone() { return typer_->zone(); }
  Isolate* isolate() { return typer_->isolate(); }
  MaybeHandle<Context> context() { return context_; }

 private:
  Typer* typer_;
  MaybeHandle<Context> context_;

  typedef Type* (*UnaryTyperFun)(Type*, Typer* t);
  typedef Type* (*BinaryTyperFun)(Type*, Type*, Typer* t);

  Bounds TypeUnaryOp(Node* node, UnaryTyperFun);
  Bounds TypeBinaryOp(Node* node, BinaryTyperFun);

  static Type* Invert(Type*, Typer*);
  static Type* FalsifyUndefined(Type*, Typer*);

  static Type* ToPrimitive(Type*, Typer*);
  static Type* ToBoolean(Type*, Typer*);
  static Type* ToNumber(Type*, Typer*);
  static Type* ToString(Type*, Typer*);
  static Type* NumberToInt32(Type*, Typer*);
  static Type* NumberToUint32(Type*, Typer*);

  static Type* JSAddRanger(Type::RangeType*, Type::RangeType*, Typer*);
  static Type* JSSubtractRanger(Type::RangeType*, Type::RangeType*, Typer*);
  static Type* JSMultiplyRanger(Type::RangeType*, Type::RangeType*, Typer*);
  static Type* JSDivideRanger(Type::RangeType*, Type::RangeType*, Typer*);

  static Type* JSCompareTyper(Type*, Type*, Typer*);

#define DECLARE_METHOD(x) static Type* x##Typer(Type*, Type*, Typer*);
  JS_SIMPLE_BINOP_LIST(DECLARE_METHOD)
#undef DECLARE_METHOD

  static Type* JSUnaryNotTyper(Type*, Typer*);
  static Type* JSLoadPropertyTyper(Type*, Type*, Typer*);
  static Type* JSCallFunctionTyper(Type*, Typer*);
};


class Typer::RunVisitor : public Typer::Visitor {
 public:
  RunVisitor(Typer* typer, MaybeHandle<Context> context)
      : Visitor(typer, context),
        redo(NodeSet::key_compare(), NodeSet::allocator_type(typer->zone())) {}

  GenericGraphVisit::Control Post(Node* node) {
    if (OperatorProperties::HasValueOutput(node->op())) {
      Bounds bounds = TypeNode(node);
      NodeProperties::SetBounds(node, bounds);
      // Remember incompletely typed nodes for least fixpoint iteration.
      int arity = OperatorProperties::GetValueInputCount(node->op());
      for (int i = 0; i < arity; ++i) {
        // TODO(rossberg): change once IsTyped is available.
        // if (!NodeProperties::IsTyped(NodeProperties::GetValueInput(node, i)))
        if (OperandType(node, i).upper->Is(Type::None())) {
          redo.insert(node);
          break;
        }
      }
    }
    return GenericGraphVisit::CONTINUE;
  }

  NodeSet redo;
};


class Typer::NarrowVisitor : public Typer::Visitor {
 public:
  NarrowVisitor(Typer* typer, MaybeHandle<Context> context)
      : Visitor(typer, context) {}

  GenericGraphVisit::Control Pre(Node* node) {
    if (OperatorProperties::HasValueOutput(node->op())) {
      Bounds previous = NodeProperties::GetBounds(node);
      Bounds bounds = TypeNode(node);
      NodeProperties::SetBounds(node, Bounds::Both(bounds, previous, zone()));
      DCHECK(bounds.Narrows(previous));
      // Stop when nothing changed (but allow re-entry in case it does later).
      return previous.Narrows(bounds)
          ? GenericGraphVisit::DEFER : GenericGraphVisit::REENTER;
    } else {
      return GenericGraphVisit::SKIP;
    }
  }

  GenericGraphVisit::Control Post(Node* node) {
    return GenericGraphVisit::REENTER;
  }
};


class Typer::WidenVisitor : public Typer::Visitor {
 public:
  WidenVisitor(Typer* typer, MaybeHandle<Context> context)
      : Visitor(typer, context) {}

  GenericGraphVisit::Control Pre(Node* node) {
    if (OperatorProperties::HasValueOutput(node->op())) {
      Bounds previous = NodeProperties::GetBounds(node);
      Bounds bounds = TypeNode(node);
      DCHECK(previous.lower->Is(bounds.lower));
      DCHECK(previous.upper->Is(bounds.upper));
      NodeProperties::SetBounds(node, bounds);  // TODO(rossberg): Either?
      // Stop when nothing changed (but allow re-entry in case it does later).
      return bounds.Narrows(previous)
          ? GenericGraphVisit::DEFER : GenericGraphVisit::REENTER;
    } else {
      return GenericGraphVisit::SKIP;
    }
  }

  GenericGraphVisit::Control Post(Node* node) {
    return GenericGraphVisit::REENTER;
  }
};


void Typer::Run(Graph* graph, MaybeHandle<Context> context) {
  RunVisitor typing(this, context);
  graph->VisitNodeInputsFromEnd(&typing);
  // Find least fixpoint.
  for (NodeSetIter i = typing.redo.begin(); i != typing.redo.end(); ++i) {
    Widen(graph, *i, context);
  }
}


void Typer::Narrow(Graph* graph, Node* start, MaybeHandle<Context> context) {
  NarrowVisitor typing(this, context);
  graph->VisitNodeUsesFrom(start, &typing);
}


void Typer::Widen(Graph* graph, Node* start, MaybeHandle<Context> context) {
  WidenVisitor typing(this, context);
  graph->VisitNodeUsesFrom(start, &typing);
}


void Typer::Init(Node* node) {
  if (OperatorProperties::HasValueOutput(node->op())) {
    Visitor typing(this, MaybeHandle<Context>());
    Bounds bounds = typing.TypeNode(node);
    NodeProperties::SetBounds(node, bounds);
  }
}


// -----------------------------------------------------------------------------

// Helper functions that lift a function f on types to a function on bounds,
// and uses that to type the given node.  Note that f is never called with None
// as an argument.


Bounds Typer::Visitor::TypeUnaryOp(Node* node, UnaryTyperFun f) {
  Bounds input = OperandType(node, 0);
  Type* upper = input.upper->Is(Type::None())
      ? Type::None()
      : f(input.upper, typer_);
  Type* lower = input.lower->Is(Type::None())
      ? Type::None()
      : (input.lower == input.upper || upper->IsConstant())
      ? upper  // TODO(neis): Extend this to Range(x,x), NaN, MinusZero, ...?
      : f(input.lower, typer_);
  // TODO(neis): Figure out what to do with lower bound.
  return Bounds(lower, upper);
}


Bounds Typer::Visitor::TypeBinaryOp(Node* node, BinaryTyperFun f) {
  Bounds left = OperandType(node, 0);
  Bounds right = OperandType(node, 1);
  Type* upper = left.upper->Is(Type::None()) || right.upper->Is(Type::None())
      ? Type::None()
      : f(left.upper, right.upper, typer_);
  Type* lower = left.lower->Is(Type::None()) || right.lower->Is(Type::None())
      ? Type::None()
      : ((left.lower == left.upper && right.lower == right.upper) ||
         upper->IsConstant())
      ? upper
      : f(left.lower, right.lower, typer_);
  // TODO(neis): Figure out what to do with lower bound.
  return Bounds(lower, upper);
}


Type* Typer::Visitor::Invert(Type* type, Typer* t) {
  if (type->Is(t->singleton_false)) return t->singleton_true;
  if (type->Is(t->singleton_true)) return t->singleton_false;
  return type;
}


Type* Typer::Visitor::FalsifyUndefined(Type* type, Typer* t) {
  if (type->Is(Type::Undefined())) return t->singleton_false;
  return type;
}


// Type conversion.


Type* Typer::Visitor::ToPrimitive(Type* type, Typer* t) {
  if (type->Is(Type::Primitive()) && !type->Maybe(Type::Receiver())) {
    return type;
  }
  return Type::Primitive();
}


Type* Typer::Visitor::ToBoolean(Type* type, Typer* t) {
  if (type->Is(Type::Boolean())) return type;
  if (type->Is(t->falsish)) return t->singleton_false;
  if (type->Is(Type::DetectableReceiver())) return t->singleton_true;
  if (type->Is(Type::OrderedNumber()) && (type->Max() < 0 || 0 < type->Min())) {
    return t->singleton_true;  // Ruled out nan, -0 and +0.
  }
  return Type::Boolean();
}


Type* Typer::Visitor::ToNumber(Type* type, Typer* t) {
  if (type->Is(Type::Number())) return type;
  if (type->Is(Type::Undefined())) return Type::NaN();
  if (type->Is(t->singleton_false)) return t->singleton_zero;
  if (type->Is(t->singleton_true)) return t->singleton_one;
  if (type->Is(Type::Boolean())) return t->zero_or_one;
  return Type::Number();
}


Type* Typer::Visitor::ToString(Type* type, Typer* t) {
  if (type->Is(Type::String())) return type;
  return Type::String();
}


Type* Typer::Visitor::NumberToInt32(Type* type, Typer* t) {
  // TODO(neis): DCHECK(type->Is(Type::Number()));
  if (type->Is(Type::Signed32())) return type;
  if (type->Is(t->zeroish)) return t->singleton_zero;
  return Type::Signed32();
}


Type* Typer::Visitor::NumberToUint32(Type* type, Typer* t) {
  // TODO(neis): DCHECK(type->Is(Type::Number()));
  if (type->Is(Type::Unsigned32())) return type;
  if (type->Is(t->zeroish)) return t->singleton_zero;
  return Type::Unsigned32();
}


// -----------------------------------------------------------------------------


// Control operators.


Bounds Typer::Visitor::TypeStart(Node* node) {
  return Bounds(Type::Internal());
}


// Common operators.


Bounds Typer::Visitor::TypeParameter(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeInt32Constant(Node* node) {
  Factory* f = zone()->isolate()->factory();
  Handle<Object> number = f->NewNumber(OpParameter<int32_t>(node));
  return Bounds(Type::Intersect(
      Type::Range(number, number, zone()), Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeInt64Constant(Node* node) {
  return Bounds(Type::Internal());  // TODO(rossberg): Add int64 bitset type?
}


Bounds Typer::Visitor::TypeFloat32Constant(Node* node) {
  return Bounds(Type::Intersect(
      Type::Of(OpParameter<float>(node), zone()),
      Type::UntaggedFloat32(), zone()));
}


Bounds Typer::Visitor::TypeFloat64Constant(Node* node) {
  return Bounds(Type::Intersect(
      Type::Of(OpParameter<double>(node), zone()),
      Type::UntaggedFloat64(), zone()));
}


Bounds Typer::Visitor::TypeNumberConstant(Node* node) {
  Factory* f = zone()->isolate()->factory();
  return Bounds(Type::Constant(
      f->NewNumber(OpParameter<double>(node)), zone()));
}


Bounds Typer::Visitor::TypeHeapConstant(Node* node) {
  return Bounds(TypeConstant(OpParameter<Unique<Object> >(node).handle()));
}


Bounds Typer::Visitor::TypeExternalConstant(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypePhi(Node* node) {
  int arity = OperatorProperties::GetValueInputCount(node->op());
  Bounds bounds = OperandType(node, 0);
  for (int i = 1; i < arity; ++i) {
    bounds = Bounds::Either(bounds, OperandType(node, i), zone());
  }
  return bounds;
}


Bounds Typer::Visitor::TypeEffectPhi(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeValueEffect(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeFinish(Node* node) {
  return OperandType(node, 0);
}


Bounds Typer::Visitor::TypeFrameState(Node* node) {
  // TODO(rossberg): Ideally FrameState wouldn't have a value output.
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeStateValues(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeCall(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeProjection(Node* node) {
  // TODO(titzer): use the output type of the input to determine the bounds.
  return Bounds::Unbounded(zone());
}


// JS comparison operators.


Type* Typer::Visitor::JSEqualTyper(Type* lhs, Type* rhs, Typer* t) {
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return t->singleton_false;
  if (lhs->Is(t->undefined_or_null) && rhs->Is(t->undefined_or_null)) {
    return t->singleton_true;
  }
  if (lhs->Is(Type::Number()) && rhs->Is(Type::Number()) &&
      (lhs->Max() < rhs->Min() || lhs->Min() > rhs->Max())) {
      return t->singleton_false;
  }
  if (lhs->IsConstant() && rhs->Is(lhs)) {
    // Types are equal and are inhabited only by a single semantic value,
    // which is not nan due to the earlier check.
    // TODO(neis): Extend this to Range(x,x), MinusZero, ...?
    return t->singleton_true;
  }
  return Type::Boolean();
}


Type* Typer::Visitor::JSNotEqualTyper(Type* lhs, Type* rhs, Typer* t) {
  return Invert(JSEqualTyper(lhs, rhs, t), t);
}


static Type* JSType(Type* type) {
  if (type->Is(Type::Boolean())) return Type::Boolean();
  if (type->Is(Type::String())) return Type::String();
  if (type->Is(Type::Number())) return Type::Number();
  if (type->Is(Type::Undefined())) return Type::Undefined();
  if (type->Is(Type::Null())) return Type::Null();
  if (type->Is(Type::Symbol())) return Type::Symbol();
  if (type->Is(Type::Receiver())) return Type::Receiver();  // JS "Object"
  return Type::Any();
}


Type* Typer::Visitor::JSStrictEqualTyper(Type* lhs, Type* rhs, Typer* t) {
  if (!JSType(lhs)->Maybe(JSType(rhs))) return t->singleton_false;
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return t->singleton_false;
  if (lhs->Is(Type::Number()) && rhs->Is(Type::Number()) &&
      (lhs->Max() < rhs->Min() || lhs->Min() > rhs->Max())) {
      return t->singleton_false;
  }
  if (lhs->IsConstant() && rhs->Is(lhs)) {
    // Types are equal and are inhabited only by a single semantic value,
    // which is not nan due to the earlier check.
    return t->singleton_true;
  }
  return Type::Boolean();
}


Type* Typer::Visitor::JSStrictNotEqualTyper(Type* lhs, Type* rhs, Typer* t) {
  return Invert(JSStrictEqualTyper(lhs, rhs, t), t);
}


// The EcmaScript specification defines the four relational comparison operators
// (<, <=, >=, >) with the help of a single abstract one.  It behaves like <
// but returns undefined when the inputs cannot be compared.
// We implement the typing analogously.
Type* Typer::Visitor::JSCompareTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToPrimitive(lhs, t);
  rhs = ToPrimitive(rhs, t);
  if (lhs->Maybe(Type::String()) && rhs->Maybe(Type::String())) {
    return Type::Boolean();
  }
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::Undefined();
  if (lhs->IsConstant() && rhs->Is(lhs)) {
    // Types are equal and are inhabited only by a single semantic value,
    // which is not NaN due to the previous check.
    return t->singleton_false;
  }
  if (lhs->Min() >= rhs->Max()) return t->singleton_false;
  if (lhs->Max() < rhs->Min() &&
      !lhs->Maybe(Type::NaN()) && !rhs->Maybe(Type::NaN())) {
    return t->singleton_true;
  }
  return Type::Boolean();
}


Type* Typer::Visitor::JSLessThanTyper(Type* lhs, Type* rhs, Typer* t) {
  return FalsifyUndefined(JSCompareTyper(lhs, rhs, t), t);
}


Type* Typer::Visitor::JSGreaterThanTyper(Type* lhs, Type* rhs, Typer* t) {
  return FalsifyUndefined(JSCompareTyper(rhs, lhs, t), t);
}


Type* Typer::Visitor::JSLessThanOrEqualTyper(Type* lhs, Type* rhs, Typer* t) {
  return FalsifyUndefined(Invert(JSCompareTyper(rhs, lhs, t), t), t);
}


Type* Typer::Visitor::JSGreaterThanOrEqualTyper(
    Type* lhs, Type* rhs, Typer* t) {
  return FalsifyUndefined(Invert(JSCompareTyper(lhs, rhs, t), t), t);
}


// JS bitwise operators.


Type* Typer::Visitor::JSBitwiseOrTyper(Type* lhs, Type* rhs, Typer* t) {
  Factory* f = t->zone()->isolate()->factory();
  lhs = NumberToInt32(ToNumber(lhs, t), t);
  rhs = NumberToInt32(ToNumber(rhs, t), t);
  double lmin = lhs->Min();
  double rmin = rhs->Min();
  double lmax = lhs->Max();
  double rmax = rhs->Max();
  // Or-ing any two values results in a value no smaller than their minimum.
  // Even no smaller than their maximum if both values are non-negative.
  Handle<Object> min = f->NewNumber(
      lmin >= 0 && rmin >= 0 ? std::max(lmin, rmin) : std::min(lmin, rmin));
  if (lmax < 0 || rmax < 0) {
    // Or-ing two values of which at least one is negative results in a negative
    // value.
    Handle<Object> max = f->NewNumber(-1);
    return Type::Range(min, max, t->zone());
  }
  Handle<Object> max = f->NewNumber(Type::Signed32()->Max());
  return Type::Range(min, max, t->zone());
  // TODO(neis): Be precise for singleton inputs, here and elsewhere.
}


Type* Typer::Visitor::JSBitwiseAndTyper(Type* lhs, Type* rhs, Typer* t) {
  Factory* f = t->zone()->isolate()->factory();
  lhs = NumberToInt32(ToNumber(lhs, t), t);
  rhs = NumberToInt32(ToNumber(rhs, t), t);
  double lmin = lhs->Min();
  double rmin = rhs->Min();
  double lmax = lhs->Max();
  double rmax = rhs->Max();
  // And-ing any two values results in a value no larger than their maximum.
  // Even no larger than their minimum if both values are non-negative.
  Handle<Object> max = f->NewNumber(
      lmin >= 0 && rmin >= 0 ? std::min(lmax, rmax) : std::max(lmax, rmax));
  if (lmin >= 0 || rmin >= 0) {
    // And-ing two values of which at least one is non-negative results in a
    // non-negative value.
    Handle<Object> min = f->NewNumber(0);
    return Type::Range(min, max, t->zone());
  }
  Handle<Object> min = f->NewNumber(Type::Signed32()->Min());
  return Type::Range(min, max, t->zone());
}


Type* Typer::Visitor::JSBitwiseXorTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = NumberToInt32(ToNumber(lhs, t), t);
  rhs = NumberToInt32(ToNumber(rhs, t), t);
  double lmin = lhs->Min();
  double rmin = rhs->Min();
  double lmax = lhs->Max();
  double rmax = rhs->Max();
  if ((lmin >= 0 && rmin >= 0) || (lmax < 0 && rmax < 0)) {
    // Xor-ing negative or non-negative values results in a non-negative value.
    return t->non_negative_signed32;
  }
  if ((lmax < 0 && rmin >= 0) || (lmin >= 0 && rmax < 0)) {
    // Xor-ing a negative and a non-negative value results in a negative value.
    return t->negative_signed32;
  }
  return Type::Signed32();
}


Type* Typer::Visitor::JSShiftLeftTyper(Type* lhs, Type* rhs, Typer* t) {
  return Type::Signed32();
}


Type* Typer::Visitor::JSShiftRightTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = NumberToInt32(ToNumber(lhs, t), t);
  Factory* f = t->zone()->isolate()->factory();
  if (lhs->Min() >= 0) {
    // Right-shifting a non-negative value cannot make it negative, nor larger.
    Handle<Object> min = f->NewNumber(0);
    Handle<Object> max = f->NewNumber(lhs->Max());
    return Type::Range(min, max, t->zone());
  }
  if (lhs->Max() < 0) {
    // Right-shifting a negative value cannot make it non-negative, nor smaller.
    Handle<Object> min = f->NewNumber(lhs->Min());
    Handle<Object> max = f->NewNumber(-1);
    return Type::Range(min, max, t->zone());
  }
  return Type::Signed32();
}


Type* Typer::Visitor::JSShiftRightLogicalTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = NumberToUint32(ToNumber(lhs, t), t);
  Factory* f = t->zone()->isolate()->factory();
  // Logical right-shifting any value cannot make it larger.
  Handle<Object> min = f->NewNumber(0);
  Handle<Object> max = f->NewNumber(lhs->Max());
  return Type::Range(min, max, t->zone());
}


// JS arithmetic operators.


Type* Typer::Visitor::JSAddTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToPrimitive(lhs, t);
  rhs = ToPrimitive(rhs, t);
  if (lhs->Maybe(Type::String()) || rhs->Maybe(Type::String())) {
    if (lhs->Is(Type::String()) || rhs->Is(Type::String())) {
      return Type::String();
    } else {
      return Type::NumberOrString();
    }
  }
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // TODO(neis): Do some analysis.
  // TODO(neis): Deal with numeric bitsets here and elsewhere.
  return Type::Number();
}


Type* Typer::Visitor::JSSubtractTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // TODO(neis): Do some analysis.
  return Type::Number();
}


Type* Typer::Visitor::JSMultiplyTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // TODO(neis): Do some analysis.
  return Type::Number();
}


Type* Typer::Visitor::JSDivideTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // TODO(neis): Do some analysis.
  return Type::Number();
}


Type* Typer::Visitor::JSModulusTyper(Type* lhs, Type* rhs, Typer* t) {
  lhs = ToNumber(lhs, t);
  rhs = ToNumber(rhs, t);
  if (lhs->Is(Type::NaN()) || rhs->Is(Type::NaN())) return Type::NaN();
  // TODO(neis): Do some analysis.
  return Type::Number();
}


// JS unary operators.


Type* Typer::Visitor::JSUnaryNotTyper(Type* type, Typer* t) {
  return Invert(ToBoolean(type, t), t);
}


Bounds Typer::Visitor::TypeJSUnaryNot(Node* node) {
  return TypeUnaryOp(node, JSUnaryNotTyper);
}


Bounds Typer::Visitor::TypeJSTypeOf(Node* node) {
  return Bounds(Type::InternalizedString());
}


// JS conversion operators.


Bounds Typer::Visitor::TypeJSToBoolean(Node* node) {
  return TypeUnaryOp(node, ToBoolean);
}


Bounds Typer::Visitor::TypeJSToNumber(Node* node) {
  return TypeUnaryOp(node, ToNumber);
}


Bounds Typer::Visitor::TypeJSToString(Node* node) {
  return TypeUnaryOp(node, ToString);
}


Bounds Typer::Visitor::TypeJSToName(Node* node) {
  return Bounds(Type::None(), Type::Name());
}


Bounds Typer::Visitor::TypeJSToObject(Node* node) {
  return Bounds(Type::None(), Type::Receiver());
}


// JS object operators.


Bounds Typer::Visitor::TypeJSCreate(Node* node) {
  return Bounds(Type::None(), Type::Object());
}


Type* Typer::Visitor::JSLoadPropertyTyper(Type* object, Type* name, Typer* t) {
  // TODO(rossberg): Use range types and sized array types to filter undefined.
  if (object->IsArray() && name->Is(Type::Integral32())) {
    return Type::Union(
        object->AsArray()->Element(), Type::Undefined(), t->zone());
  }
  return Type::Any();
}


Bounds Typer::Visitor::TypeJSLoadProperty(Node* node) {
  return TypeBinaryOp(node, JSLoadPropertyTyper);
}


Bounds Typer::Visitor::TypeJSLoadNamed(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeJSStoreProperty(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeJSStoreNamed(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeJSDeleteProperty(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeJSHasProperty(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeJSInstanceOf(Node* node) {
  return Bounds(Type::Boolean());
}


// JS context operators.


Bounds Typer::Visitor::TypeJSLoadContext(Node* node) {
  Bounds outer = OperandType(node, 0);
  DCHECK(outer.upper->Maybe(Type::Internal()));
  // TODO(rossberg): More precisely, instead of the above assertion, we should
  // back-propagate the constraint that it has to be a subtype of Internal.

  ContextAccess access = OpParameter<ContextAccess>(node);
  Type* context_type = outer.upper;
  MaybeHandle<Context> context;
  if (context_type->IsConstant()) {
    context = Handle<Context>::cast(context_type->AsConstant()->Value());
  }
  // Walk context chain (as far as known), mirroring dynamic lookup.
  // Since contexts are mutable, the information is only useful as a lower
  // bound.
  // TODO(rossberg): Could use scope info to fix upper bounds for constant
  // bindings if we know that this code is never shared.
  for (size_t i = access.depth(); i > 0; --i) {
    if (context_type->IsContext()) {
      context_type = context_type->AsContext()->Outer();
      if (context_type->IsConstant()) {
        context = Handle<Context>::cast(context_type->AsConstant()->Value());
      }
    } else if (!context.is_null()) {
      context = handle(context.ToHandleChecked()->previous(), isolate());
    }
  }
  if (context.is_null()) {
    return Bounds::Unbounded(zone());
  } else {
    Handle<Object> value =
        handle(context.ToHandleChecked()->get(static_cast<int>(access.index())),
               isolate());
    Type* lower = TypeConstant(value);
    return Bounds(lower, Type::Any());
  }
}


Bounds Typer::Visitor::TypeJSStoreContext(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeJSCreateFunctionContext(Node* node) {
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


Bounds Typer::Visitor::TypeJSCreateCatchContext(Node* node) {
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


Bounds Typer::Visitor::TypeJSCreateWithContext(Node* node) {
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


Bounds Typer::Visitor::TypeJSCreateBlockContext(Node* node) {
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


Bounds Typer::Visitor::TypeJSCreateModuleContext(Node* node) {
  // TODO(rossberg): this is probably incorrect
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


Bounds Typer::Visitor::TypeJSCreateGlobalContext(Node* node) {
  Type* outer = ContextType(node);
  return Bounds(Type::Context(outer, zone()));
}


// JS other operators.


Bounds Typer::Visitor::TypeJSYield(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeJSCallConstruct(Node* node) {
  return Bounds(Type::None(), Type::Receiver());
}


Type* Typer::Visitor::JSCallFunctionTyper(Type* fun, Typer* t) {
  return fun->IsFunction() ? fun->AsFunction()->Result() : Type::Any();
}


Bounds Typer::Visitor::TypeJSCallFunction(Node* node) {
  return TypeUnaryOp(node, JSCallFunctionTyper);  // We ignore argument types.
}


Bounds Typer::Visitor::TypeJSCallRuntime(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeJSDebugger(Node* node) {
  return Bounds::Unbounded(zone());
}


// Simplified operators.


Bounds Typer::Visitor::TypeBooleanNot(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeBooleanToNumber(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeNumberLessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeNumberLessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeNumberAdd(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberSubtract(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberMultiply(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberDivide(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberModulus(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeNumberToInt32(Node* node) {
  return TypeUnaryOp(node, NumberToInt32);
}


Bounds Typer::Visitor::TypeNumberToUint32(Node* node) {
  return TypeUnaryOp(node, NumberToUint32);
}


Bounds Typer::Visitor::TypeReferenceEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeStringEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeStringLessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeStringLessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeStringAdd(Node* node) {
  return Bounds(Type::String());
}


static Type* ChangeRepresentation(Type* type, Type* rep, Zone* zone) {
  // TODO(neis): Enable when expressible.
  /*
  return Type::Union(
      Type::Intersect(type, Type::Semantic(), zone),
      Type::Intersect(rep, Type::Representation(), zone), zone);
  */
  return type;
}


Bounds Typer::Visitor::TypeChangeTaggedToInt32(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Signed32()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::UntaggedInt32(), zone()),
      ChangeRepresentation(arg.upper, Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeChangeTaggedToUint32(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Unsigned32()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::UntaggedInt32(), zone()),
      ChangeRepresentation(arg.upper, Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeChangeTaggedToFloat64(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Number()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::UntaggedFloat64(), zone()),
      ChangeRepresentation(arg.upper, Type::UntaggedFloat64(), zone()));
}


Bounds Typer::Visitor::TypeChangeInt32ToTagged(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Signed32()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::Tagged(), zone()),
      ChangeRepresentation(arg.upper, Type::Tagged(), zone()));
}


Bounds Typer::Visitor::TypeChangeUint32ToTagged(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Unsigned32()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::Tagged(), zone()),
      ChangeRepresentation(arg.upper, Type::Tagged(), zone()));
}


Bounds Typer::Visitor::TypeChangeFloat64ToTagged(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): CHECK(arg.upper->Is(Type::Number()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::Tagged(), zone()),
      ChangeRepresentation(arg.upper, Type::Tagged(), zone()));
}


Bounds Typer::Visitor::TypeChangeBoolToBit(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Boolean()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::UntaggedInt1(), zone()),
      ChangeRepresentation(arg.upper, Type::UntaggedInt1(), zone()));
}


Bounds Typer::Visitor::TypeChangeBitToBool(Node* node) {
  Bounds arg = OperandType(node, 0);
  // TODO(neis): DCHECK(arg.upper->Is(Type::Boolean()));
  return Bounds(
      ChangeRepresentation(arg.lower, Type::TaggedPtr(), zone()),
      ChangeRepresentation(arg.upper, Type::TaggedPtr(), zone()));
}


Bounds Typer::Visitor::TypeLoadField(Node* node) {
  return Bounds(FieldAccessOf(node->op()).type);
}


Bounds Typer::Visitor::TypeLoadElement(Node* node) {
  return Bounds(ElementAccessOf(node->op()).type);
}


Bounds Typer::Visitor::TypeStoreField(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeStoreElement(Node* node) {
  UNREACHABLE();
  return Bounds();
}


// Machine operators.


Bounds Typer::Visitor::TypeLoad(Node* node) {
  return Bounds::Unbounded(zone());
}


Bounds Typer::Visitor::TypeStore(Node* node) {
  UNREACHABLE();
  return Bounds();
}


Bounds Typer::Visitor::TypeWord32And(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Or(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Xor(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Shl(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Shr(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Sar(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Ror(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeWord32Equal(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeWord64And(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Or(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Xor(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Shl(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Shr(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Sar(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Ror(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeWord64Equal(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeInt32Add(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32AddWithOverflow(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt32Sub(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32SubWithOverflow(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt32Mul(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32MulHigh(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32Div(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32Mod(Node* node) {
  return Bounds(Type::Integral32());
}


Bounds Typer::Visitor::TypeInt32LessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeInt32LessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeUint32Div(Node* node) {
  return Bounds(Type::Unsigned32());
}


Bounds Typer::Visitor::TypeUint32LessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeUint32LessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeUint32Mod(Node* node) {
  return Bounds(Type::Unsigned32());
}


Bounds Typer::Visitor::TypeInt64Add(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt64Sub(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt64Mul(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt64Div(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt64Mod(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeInt64LessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeInt64LessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeUint64Div(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeUint64LessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeUint64Mod(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeChangeFloat32ToFloat64(Node* node) {
  return Bounds(Type::Intersect(
      Type::Number(), Type::UntaggedFloat64(), zone()));
}


Bounds Typer::Visitor::TypeChangeFloat64ToInt32(Node* node) {
  return Bounds(Type::Intersect(
      Type::Signed32(), Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeChangeFloat64ToUint32(Node* node) {
  return Bounds(Type::Intersect(
      Type::Unsigned32(), Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeChangeInt32ToFloat64(Node* node) {
  return Bounds(Type::Intersect(
      Type::Signed32(), Type::UntaggedFloat64(), zone()));
}


Bounds Typer::Visitor::TypeChangeInt32ToInt64(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeChangeUint32ToFloat64(Node* node) {
  return Bounds(Type::Intersect(
      Type::Unsigned32(), Type::UntaggedFloat64(), zone()));
}


Bounds Typer::Visitor::TypeChangeUint32ToUint64(Node* node) {
  return Bounds(Type::Internal());
}


Bounds Typer::Visitor::TypeTruncateFloat64ToFloat32(Node* node) {
  return Bounds(Type::Intersect(
      Type::Number(), Type::UntaggedFloat32(), zone()));
}


Bounds Typer::Visitor::TypeTruncateFloat64ToInt32(Node* node) {
  return Bounds(Type::Intersect(
      Type::Signed32(), Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeTruncateInt64ToInt32(Node* node) {
  return Bounds(Type::Intersect(
      Type::Signed32(), Type::UntaggedInt32(), zone()));
}


Bounds Typer::Visitor::TypeFloat64Add(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Sub(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Mul(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Div(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Mod(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Sqrt(Node* node) {
  return Bounds(Type::Number());
}


Bounds Typer::Visitor::TypeFloat64Equal(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeFloat64LessThan(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeFloat64LessThanOrEqual(Node* node) {
  return Bounds(Type::Boolean());
}


Bounds Typer::Visitor::TypeLoadStackPointer(Node* node) {
  return Bounds(Type::Internal());
}


// Heap constants.


Type* Typer::Visitor::TypeConstant(Handle<Object> value) {
  if (value->IsJSFunction()) {
    if (JSFunction::cast(*value)->shared()->HasBuiltinFunctionId()) {
      switch (JSFunction::cast(*value)->shared()->builtin_function_id()) {
        // TODO(rossberg): can't express overloading
        case kMathAbs:
          return typer_->number_fun1_;
        case kMathAcos:
          return typer_->number_fun1_;
        case kMathAsin:
          return typer_->number_fun1_;
        case kMathAtan:
          return typer_->number_fun1_;
        case kMathAtan2:
          return typer_->number_fun2_;
        case kMathCeil:
          return typer_->weakint_fun1_;
        case kMathCos:
          return typer_->number_fun1_;
        case kMathExp:
          return typer_->number_fun1_;
        case kMathFloor:
          return typer_->weakint_fun1_;
        case kMathImul:
          return typer_->imul_fun_;
        case kMathLog:
          return typer_->number_fun1_;
        case kMathPow:
          return typer_->number_fun2_;
        case kMathRandom:
          return typer_->random_fun_;
        case kMathRound:
          return typer_->weakint_fun1_;
        case kMathSin:
          return typer_->number_fun1_;
        case kMathSqrt:
          return typer_->number_fun1_;
        case kMathTan:
          return typer_->number_fun1_;
        default:
          break;
      }
    } else if (JSFunction::cast(*value)->IsBuiltin() && !context().is_null()) {
      Handle<Context> native =
          handle(context().ToHandleChecked()->native_context(), isolate());
      if (*value == native->array_buffer_fun()) {
        return typer_->array_buffer_fun_;
      } else if (*value == native->int8_array_fun()) {
        return typer_->int8_array_fun_;
      } else if (*value == native->int16_array_fun()) {
        return typer_->int16_array_fun_;
      } else if (*value == native->int32_array_fun()) {
        return typer_->int32_array_fun_;
      } else if (*value == native->uint8_array_fun()) {
        return typer_->uint8_array_fun_;
      } else if (*value == native->uint16_array_fun()) {
        return typer_->uint16_array_fun_;
      } else if (*value == native->uint32_array_fun()) {
        return typer_->uint32_array_fun_;
      } else if (*value == native->float32_array_fun()) {
        return typer_->float32_array_fun_;
      } else if (*value == native->float64_array_fun()) {
        return typer_->float64_array_fun_;
      }
    }
  }
  return Type::Constant(value, zone());
}


namespace {

class TyperDecorator : public GraphDecorator {
 public:
  explicit TyperDecorator(Typer* typer) : typer_(typer) {}
  virtual void Decorate(Node* node) { typer_->Init(node); }

 private:
  Typer* typer_;
};

}


void Typer::DecorateGraph(Graph* graph) {
  graph->AddDecorator(new (zone()) TyperDecorator(this));
}

}
}
}  // namespace v8::internal::compiler
