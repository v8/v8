// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-builtin-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects-inl.h"
#include "src/type-cache.h"
#include "src/types.h"

namespace v8 {
namespace internal {
namespace compiler {


// Helper class to access JSCallFunction nodes that are potential candidates
// for reduction when they have a BuiltinFunctionId associated with them.
class JSCallReduction {
 public:
  explicit JSCallReduction(Node* node) : node_(node) {}

  // Determines whether the node is a JSCallFunction operation that targets a
  // constant callee being a well-known builtin with a BuiltinFunctionId.
  bool HasBuiltinFunctionId() {
    if (node_->opcode() != IrOpcode::kJSCallFunction) return false;
    HeapObjectMatcher m(NodeProperties::GetValueInput(node_, 0));
    if (!m.HasValue() || !m.Value()->IsJSFunction()) return false;
    Handle<JSFunction> function = Handle<JSFunction>::cast(m.Value());
    return function->shared()->HasBuiltinFunctionId();
  }

  // Retrieves the BuiltinFunctionId as described above.
  BuiltinFunctionId GetBuiltinFunctionId() {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    HeapObjectMatcher m(NodeProperties::GetValueInput(node_, 0));
    Handle<JSFunction> function = Handle<JSFunction>::cast(m.Value());
    return function->shared()->builtin_function_id();
  }

  // Determines whether the call takes zero inputs.
  bool InputsMatchZero() { return GetJSCallArity() == 0; }

  // Determines whether the call takes one input of the given type.
  bool InputsMatchOne(Type* t1) {
    return GetJSCallArity() == 1 &&
           NodeProperties::GetType(GetJSCallInput(0))->Is(t1);
  }

  // Determines whether the call takes two inputs of the given types.
  bool InputsMatchTwo(Type* t1, Type* t2) {
    return GetJSCallArity() == 2 &&
           NodeProperties::GetType(GetJSCallInput(0))->Is(t1) &&
           NodeProperties::GetType(GetJSCallInput(1))->Is(t2);
  }

  // Determines whether the call takes inputs all of the given type.
  bool InputsMatchAll(Type* t) {
    for (int i = 0; i < GetJSCallArity(); i++) {
      if (!NodeProperties::GetType(GetJSCallInput(i))->Is(t)) {
        return false;
      }
    }
    return true;
  }

  Node* left() { return GetJSCallInput(0); }
  Node* right() { return GetJSCallInput(1); }

  int GetJSCallArity() {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    // Skip first (i.e. callee) and second (i.e. receiver) operand.
    return node_->op()->ValueInputCount() - 2;
  }

  Node* GetJSCallInput(int index) {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    DCHECK_LT(index, GetJSCallArity());
    // Skip first (i.e. callee) and second (i.e. receiver) operand.
    return NodeProperties::GetValueInput(node_, index + 2);
  }

 private:
  Node* node_;
};

JSBuiltinReducer::JSBuiltinReducer(Editor* editor, JSGraph* jsgraph)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      type_cache_(TypeCache::Get()) {}

// ES6 section 20.2.2.1 Math.abs ( x )
Reduction JSBuiltinReducer::ReduceMathAbs(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.abs(a:plain-primitive) -> NumberAbs(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberAbs(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.6 Math.atan ( x )
Reduction JSBuiltinReducer::ReduceMathAtan(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.atan(a:plain-primitive) -> NumberAtan(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberAtan(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.8 Math.atan2 ( y, x )
Reduction JSBuiltinReducer::ReduceMathAtan2(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchTwo(Type::PlainPrimitive(), Type::PlainPrimitive())) {
    // Math.atan2(a:plain-primitive,
    //            b:plain-primitive) -> NumberAtan2(ToNumber(a),
    //                                              ToNumber(b))
    Node* left = ToNumber(r.left());
    Node* right = ToNumber(r.right());
    Node* value = graph()->NewNode(simplified()->NumberAtan2(), left, right);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.7 Math.atanh ( x )
Reduction JSBuiltinReducer::ReduceMathAtanh(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::Number())) {
    // Math.atanh(a:number) -> NumberAtanh(a)
    Node* value = graph()->NewNode(simplified()->NumberAtanh(), r.left());
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.10 Math.ceil ( x )
Reduction JSBuiltinReducer::ReduceMathCeil(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.ceil(a:plain-primitive) -> NumberCeil(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberCeil(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.11 Math.clz32 ( x )
Reduction JSBuiltinReducer::ReduceMathClz32(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.clz32(a:plain-primitive) -> NumberClz32(ToUint32(a))
    Node* input = ToUint32(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberClz32(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.12 Math.cos ( x )
Reduction JSBuiltinReducer::ReduceMathCos(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.cos(a:plain-primitive) -> NumberCos(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberCos(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.14 Math.exp ( x )
Reduction JSBuiltinReducer::ReduceMathExp(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.exp(a:plain-primitive) -> NumberExp(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberExp(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.15 Math.expm1 ( x )
Reduction JSBuiltinReducer::ReduceMathExpm1(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::Number())) {
    // Math.expm1(a:number) -> NumberExpm1(a)
    Node* value = graph()->NewNode(simplified()->NumberExpm1(), r.left());
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.16 Math.floor ( x )
Reduction JSBuiltinReducer::ReduceMathFloor(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.floor(a:plain-primitive) -> NumberFloor(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberFloor(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.17 Math.fround ( x )
Reduction JSBuiltinReducer::ReduceMathFround(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.fround(a:plain-primitive) -> NumberFround(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberFround(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.19 Math.imul ( x, y )
Reduction JSBuiltinReducer::ReduceMathImul(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchTwo(Type::PlainPrimitive(), Type::PlainPrimitive())) {
    // Math.imul(a:plain-primitive,
    //           b:plain-primitive) -> NumberImul(ToUint32(a),
    //                                            ToUint32(b))
    Node* left = ToUint32(r.left());
    Node* right = ToUint32(r.right());
    Node* value = graph()->NewNode(simplified()->NumberImul(), left, right);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.20 Math.log ( x )
Reduction JSBuiltinReducer::ReduceMathLog(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.log(a:plain-primitive) -> NumberLog(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberLog(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.21 Math.log1p ( x )
Reduction JSBuiltinReducer::ReduceMathLog1p(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.log1p(a:plain-primitive) -> NumberLog1p(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberLog1p(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.22 Math.log10 ( x )
Reduction JSBuiltinReducer::ReduceMathLog10(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::Number())) {
    // Math.log10(a:number) -> NumberLog10(a)
    Node* value = graph()->NewNode(simplified()->NumberLog10(), r.left());
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.23 Math.log2 ( x )
Reduction JSBuiltinReducer::ReduceMathLog2(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::Number())) {
    // Math.log2(a:number) -> NumberLog(a)
    Node* value = graph()->NewNode(simplified()->NumberLog2(), r.left());
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.24 Math.max ( value1, value2, ...values )
Reduction JSBuiltinReducer::ReduceMathMax(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchZero()) {
    // Math.max() -> -Infinity
    return Replace(jsgraph()->Constant(-V8_INFINITY));
  }
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.max(a:plain-primitive) -> ToNumber(a)
    Node* value = ToNumber(r.GetJSCallInput(0));
    return Replace(value);
  }
  if (r.InputsMatchAll(Type::Integral32())) {
    // Math.max(a:int32, b:int32, ...)
    Node* value = r.GetJSCallInput(0);
    for (int i = 1; i < r.GetJSCallArity(); i++) {
      Node* const input = r.GetJSCallInput(i);
      value = graph()->NewNode(
          common()->Select(MachineRepresentation::kNone),
          graph()->NewNode(simplified()->NumberLessThan(), input, value), value,
          input);
    }
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.25 Math.min ( value1, value2, ...values )
Reduction JSBuiltinReducer::ReduceMathMin(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchZero()) {
    // Math.min() -> Infinity
    return Replace(jsgraph()->Constant(V8_INFINITY));
  }
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.min(a:plain-primitive) -> ToNumber(a)
    Node* value = ToNumber(r.GetJSCallInput(0));
    return Replace(value);
  }
  if (r.InputsMatchAll(Type::Integral32())) {
    // Math.min(a:int32, b:int32, ...)
    Node* value = r.GetJSCallInput(0);
    for (int i = 1; i < r.GetJSCallArity(); i++) {
      Node* const input = r.GetJSCallInput(i);
      value = graph()->NewNode(
          common()->Select(MachineRepresentation::kNone),
          graph()->NewNode(simplified()->NumberLessThan(), input, value), input,
          value);
    }
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.28 Math.round ( x )
Reduction JSBuiltinReducer::ReduceMathRound(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.round(a:plain-primitive) -> NumberRound(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberRound(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.9 Math.cbrt ( x )
Reduction JSBuiltinReducer::ReduceMathCbrt(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::Number())) {
    // Math.cbrt(a:number) -> NumberCbrt(a)
    Node* value = graph()->NewNode(simplified()->NumberCbrt(), r.left());
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.30 Math.sin ( x )
Reduction JSBuiltinReducer::ReduceMathSin(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.sin(a:plain-primitive) -> NumberSin(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberSin(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.32 Math.sqrt ( x )
Reduction JSBuiltinReducer::ReduceMathSqrt(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.sqrt(a:plain-primitive) -> NumberSqrt(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberSqrt(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.33 Math.tan ( x )
Reduction JSBuiltinReducer::ReduceMathTan(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.tan(a:plain-primitive) -> NumberTan(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberTan(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 20.2.2.35 Math.trunc ( x )
Reduction JSBuiltinReducer::ReduceMathTrunc(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // Math.trunc(a:plain-primitive) -> NumberTrunc(ToNumber(a))
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->NumberTrunc(), input);
    return Replace(value);
  }
  return NoChange();
}

// ES6 section 21.1.2.1 String.fromCharCode ( ...codeUnits )
Reduction JSBuiltinReducer::ReduceStringFromCharCode(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatchOne(Type::PlainPrimitive())) {
    // String.fromCharCode(a:plain-primitive) -> StringFromCharCode(a)
    Node* input = ToNumber(r.GetJSCallInput(0));
    Node* value = graph()->NewNode(simplified()->StringFromCharCode(), input);
    return Replace(value);
  }
  return NoChange();
}

Reduction JSBuiltinReducer::Reduce(Node* node) {
  Reduction reduction = NoChange();
  JSCallReduction r(node);

  // Dispatch according to the BuiltinFunctionId if present.
  if (!r.HasBuiltinFunctionId()) return NoChange();
  switch (r.GetBuiltinFunctionId()) {
    case kMathAbs:
      reduction = ReduceMathAbs(node);
      break;
    case kMathAtan:
      reduction = ReduceMathAtan(node);
      break;
    case kMathAtan2:
      reduction = ReduceMathAtan2(node);
      break;
    case kMathAtanh:
      reduction = ReduceMathAtanh(node);
      break;
    case kMathClz32:
      reduction = ReduceMathClz32(node);
      break;
    case kMathCeil:
      reduction = ReduceMathCeil(node);
      break;
    case kMathCos:
      reduction = ReduceMathCos(node);
      break;
    case kMathExp:
      reduction = ReduceMathExp(node);
      break;
    case kMathExpm1:
      reduction = ReduceMathExpm1(node);
      break;
    case kMathFloor:
      reduction = ReduceMathFloor(node);
      break;
    case kMathFround:
      reduction = ReduceMathFround(node);
      break;
    case kMathImul:
      reduction = ReduceMathImul(node);
      break;
    case kMathLog:
      reduction = ReduceMathLog(node);
      break;
    case kMathLog1p:
      reduction = ReduceMathLog1p(node);
      break;
    case kMathLog10:
      reduction = ReduceMathLog10(node);
      break;
    case kMathLog2:
      reduction = ReduceMathLog2(node);
      break;
    case kMathMax:
      reduction = ReduceMathMax(node);
      break;
    case kMathMin:
      reduction = ReduceMathMin(node);
      break;
    case kMathCbrt:
      reduction = ReduceMathCbrt(node);
      break;
    case kMathRound:
      reduction = ReduceMathRound(node);
      break;
    case kMathSin:
      reduction = ReduceMathSin(node);
      break;
    case kMathSqrt:
      reduction = ReduceMathSqrt(node);
      break;
    case kMathTan:
      reduction = ReduceMathTan(node);
      break;
    case kMathTrunc:
      reduction = ReduceMathTrunc(node);
      break;
    case kStringFromCharCode:
      reduction = ReduceStringFromCharCode(node);
      break;
    default:
      break;
  }

  // Replace builtin call assuming replacement nodes are pure values that don't
  // produce an effect. Replaces {node} with {reduction} and relaxes effects.
  if (reduction.Changed()) ReplaceWithValue(node, reduction.replacement());

  return reduction;
}

Node* JSBuiltinReducer::ToNumber(Node* input) {
  Type* input_type = NodeProperties::GetType(input);
  if (input_type->Is(Type::Number())) return input;
  return graph()->NewNode(simplified()->PlainPrimitiveToNumber(), input);
}

Node* JSBuiltinReducer::ToUint32(Node* input) {
  input = ToNumber(input);
  Type* input_type = NodeProperties::GetType(input);
  if (input_type->Is(Type::Unsigned32())) return input;
  return graph()->NewNode(simplified()->NumberToUint32(), input);
}

Graph* JSBuiltinReducer::graph() const { return jsgraph()->graph(); }


Isolate* JSBuiltinReducer::isolate() const { return jsgraph()->isolate(); }


CommonOperatorBuilder* JSBuiltinReducer::common() const {
  return jsgraph()->common();
}


SimplifiedOperatorBuilder* JSBuiltinReducer::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
