// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

DecompressionElimination::DecompressionElimination(
    Editor* editor, Graph* graph, MachineOperatorBuilder* machine,
    CommonOperatorBuilder* common)
    : AdvancedReducer(editor),
      graph_(graph),
      machine_(machine),
      common_(common) {}

bool DecompressionElimination::IsReduceableConstantOpcode(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kInt64Constant:
    case IrOpcode::kHeapConstant:
      return true;
    default:
      return false;
  }
}

bool DecompressionElimination::IsValidDecompress(
    IrOpcode::Value compressOpcode, IrOpcode::Value decompressOpcode) {
  switch (compressOpcode) {
    case IrOpcode::kChangeTaggedToCompressed:
      return IrOpcode::IsDecompressOpcode(decompressOpcode);
    case IrOpcode::kChangeTaggedSignedToCompressedSigned:
      return decompressOpcode ==
                 IrOpcode::kChangeCompressedSignedToTaggedSigned ||
             decompressOpcode == IrOpcode::kChangeCompressedToTagged;
    case IrOpcode::kChangeTaggedPointerToCompressedPointer:
      return decompressOpcode ==
                 IrOpcode::kChangeCompressedPointerToTaggedPointer ||
             decompressOpcode == IrOpcode::kChangeCompressedToTagged;
    default:
      UNREACHABLE();
  }
}

Node* DecompressionElimination::GetCompressedConstant(Node* constant) {
  switch (constant->opcode()) {
    case IrOpcode::kInt64Constant:
      return graph()->NewNode(common()->Int32Constant(
          static_cast<int32_t>(OpParameter<int64_t>(constant->op()))));
      break;
    case IrOpcode::kHeapConstant:
      // TODO(v8:8977): The HeapConstant remains as 64 bits. This does not
      // affect the comparison and it will still work correctly. However, we are
      // introducing a 64 bit value in the stream where a 32 bit one will
      // suffice. Currently there is no "CompressedHeapConstant", and
      // introducing a new opcode and handling it correctly throught the
      // pipeline seems that it will involve quite a bit of work.
      return constant;
    default:
      UNREACHABLE();
  }
}

Reduction DecompressionElimination::ReduceCompress(Node* node) {
  DCHECK(IrOpcode::IsCompressOpcode(node->opcode()));

  DCHECK_EQ(node->InputCount(), 1);
  Node* input_node = node->InputAt(0);
  if (IrOpcode::IsDecompressOpcode(input_node->opcode())) {
    DCHECK(IsValidDecompress(node->opcode(), input_node->opcode()));
    DCHECK_EQ(input_node->InputCount(), 1);
    return Replace(input_node->InputAt(0));
  } else {
    return NoChange();
  }
}

Reduction DecompressionElimination::ReduceTypedStateValues(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kTypedStateValues);

  bool any_change = false;
  for (int i = 0; i < node->InputCount(); ++i) {
    Node* input = node->InputAt(i);
    if (IrOpcode::IsDecompressOpcode(input->opcode())) {
      DCHECK_EQ(input->InputCount(), 1);
      node->ReplaceInput(i, input->InputAt(0));
      any_change = true;
    }
  }
  return any_change ? Changed(node) : NoChange();
}

Reduction DecompressionElimination::ReduceWord64Equal(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWord64Equal);

  DCHECK_EQ(node->InputCount(), 2);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  bool lhs_is_decompress = IrOpcode::IsDecompressOpcode(lhs->opcode());
  bool rhs_is_decompress = IrOpcode::IsDecompressOpcode(rhs->opcode());

  // Case where both of its inputs are Decompress nodes.
  if (lhs_is_decompress && rhs_is_decompress) {
    DCHECK_EQ(lhs->InputCount(), 1);
    node->ReplaceInput(0, lhs->InputAt(0));
    DCHECK_EQ(rhs->InputCount(), 1);
    node->ReplaceInput(1, rhs->InputAt(0));
    NodeProperties::ChangeOp(node, machine()->Word32Equal());
    return Changed(node);
  }

  bool lhs_is_constant = IsReduceableConstantOpcode(lhs->opcode());
  bool rhs_is_constant = IsReduceableConstantOpcode(rhs->opcode());

  // Case where one input is a Decompress node and the other a constant.
  if ((lhs_is_decompress && rhs_is_constant) ||
      (lhs_is_constant && rhs_is_decompress)) {
    node->ReplaceInput(
        0, lhs_is_decompress ? lhs->InputAt(0) : GetCompressedConstant(lhs));
    node->ReplaceInput(
        1, lhs_is_decompress ? GetCompressedConstant(rhs) : rhs->InputAt(0));
    NodeProperties::ChangeOp(node, machine()->Word32Equal());
    return Changed(node);
  }

  return NoChange();
}

Reduction DecompressionElimination::Reduce(Node* node) {
  DisallowHeapAccess no_heap_access;

  switch (node->opcode()) {
    case IrOpcode::kChangeTaggedToCompressed:
    case IrOpcode::kChangeTaggedSignedToCompressedSigned:
    case IrOpcode::kChangeTaggedPointerToCompressedPointer:
      return ReduceCompress(node);
    case IrOpcode::kTypedStateValues:
      return ReduceTypedStateValues(node);
    case IrOpcode::kWord64Equal:
      return ReduceWord64Equal(node);
    default:
      break;
  }

  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
