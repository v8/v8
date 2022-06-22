// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-gc-operator-reducer.h"

#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8 {
namespace internal {
namespace compiler {

WasmGCOperatorReducer::WasmGCOperatorReducer(Editor* editor,
                                             MachineGraph* mcgraph,
                                             const wasm::WasmModule* module)
    : AdvancedReducer(editor),
      mcgraph_(mcgraph),
      gasm_(mcgraph, mcgraph->zone()),
      module_(module) {}

Reduction WasmGCOperatorReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kAssertNotNull:
      return ReduceAssertNotNull(node);
    case IrOpcode::kIsNull:
      return ReduceIsNull(node);
    case IrOpcode::kWasmTypeCheck:
      return ReduceWasmTypeCheck(node);
    case IrOpcode::kWasmTypeCast:
      return ReduceWasmTypeCast(node);
    default:
      return NoChange();
  }
}

namespace {
bool InDeadBranch(Node* node) {
  return node->opcode() == IrOpcode::kDead ||
         NodeProperties::GetType(node).AsWasm().type.is_bottom();
}
}  // namespace

Node* WasmGCOperatorReducer::SetType(Node* node, wasm::ValueType type) {
  NodeProperties::SetType(node, Type::Wasm(type, module_, graph()->zone()));
  return node;
}

Reduction WasmGCOperatorReducer::ReduceAssertNotNull(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAssertNotNull);
  Node* object = NodeProperties::GetValueInput(node, 0);

  if (InDeadBranch(object)) return NoChange();

  // Optimize the check away if the argument is known to be non-null.
  if (!NodeProperties::GetType(object).AsWasm().type.is_nullable()) {
    ReplaceWithValue(node, object);
    node->Kill();
    return Replace(object);
  }

  return NoChange();
}

Reduction WasmGCOperatorReducer::ReduceIsNull(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kIsNull);
  Node* object = NodeProperties::GetValueInput(node, 0);

  if (InDeadBranch(object)) return NoChange();

  // Optimize the check away if the argument is known to be non-null.
  if (!NodeProperties::GetType(object).AsWasm().type.is_nullable()) {
    ReplaceWithValue(node, gasm_.Int32Constant(0));
    node->Kill();
    return Replace(object);  // Irrelevant replacement.
  }

  // Optimize the check away if the argument is known to be null.
  if (object->opcode() == IrOpcode::kNull) {
    ReplaceWithValue(node, gasm_.Int32Constant(1));
    node->Kill();
    return Replace(object);  // Irrelevant replacement.
  }

  return NoChange();
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCast(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCast);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* rtt = NodeProperties::GetValueInput(node, 1);

  if (InDeadBranch(object) || InDeadBranch(rtt)) return NoChange();

  wasm::TypeInModule object_type = NodeProperties::GetType(object).AsWasm();
  wasm::TypeInModule rtt_type = NodeProperties::GetType(rtt).AsWasm();

  if (object_type.type.is_bottom()) return NoChange();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(),
                            wasm::HeapType(rtt_type.type.ref_index()),
                            object_type.module, rtt_type.module)) {
    // Type cast will always succeed. Remove it.
    ReplaceWithValue(node, object);
    node->Kill();
    return Replace(object);
  }

  if (wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               wasm::HeapType(rtt_type.type.ref_index()),
                               object_type.module, rtt_type.module)) {
    gasm_.InitializeEffectControl(effect, control);
    // A cast between unrelated types can only succeed if the argument is null.
    // Otherwise, it always fails.
    Node* non_trapping_condition = object_type.type.is_nullable()
                                       ? gasm_.IsNull(object)
                                       : gasm_.Int32Constant(0);
    gasm_.TrapUnless(SetType(non_trapping_condition, wasm::kWasmI32),
                     TrapId::kTrapIllegalCast);
    // TODO(manoskouk): Improve the type when we have nullref.
    Node* null_node = gasm_.Null();
    ReplaceWithValue(
        node,
        SetType(null_node, wasm::ValueType::Ref(rtt_type.type.ref_index(),
                                                wasm::kNullable)),
        gasm_.effect(), gasm_.control());
    node->Kill();
    return Replace(null_node);
  }

  // Remove the null check from the cast if able.
  if (!object_type.type.is_nullable() &&
      OpParameter<WasmTypeCheckConfig>(node->op()).object_can_be_null) {
    uint8_t rtt_depth = OpParameter<WasmTypeCheckConfig>(node->op()).rtt_depth;
    NodeProperties::ChangeOp(
        node, gasm_.simplified()->WasmTypeCast(
                  {/* object_can_be_null = */ false, rtt_depth}));
    return Changed(node);
  }

  return NoChange();
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCheck(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCheck);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* rtt = NodeProperties::GetValueInput(node, 1);

  if (InDeadBranch(object) || InDeadBranch(rtt)) return NoChange();

  wasm::TypeInModule object_type = NodeProperties::GetType(object).AsWasm();
  wasm::TypeInModule rtt_type = NodeProperties::GetType(rtt).AsWasm();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(),
                            wasm::HeapType(rtt_type.type.ref_index()),
                            object_type.module, rtt_type.module)) {
    // Type cast will fail only on null.
    Node* condition =
        SetType(object_type.type.is_nullable() ? gasm_.IsNotNull(object)
                                               : gasm_.Int32Constant(1),
                wasm::kWasmI32);
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  if (wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               wasm::HeapType(rtt_type.type.ref_index()),
                               object_type.module, rtt_type.module)) {
    Node* condition = SetType(gasm_.Int32Constant(0), wasm::kWasmI32);
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  // Remove the null check from the typecheck if able.
  if (!object_type.type.is_nullable() &&
      OpParameter<WasmTypeCheckConfig>(node->op()).object_can_be_null) {
    uint8_t rtt_depth = OpParameter<WasmTypeCheckConfig>(node->op()).rtt_depth;
    NodeProperties::ChangeOp(
        node, gasm_.simplified()->WasmTypeCheck({false, rtt_depth}));
    return Changed(node);
  }

  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
