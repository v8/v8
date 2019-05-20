// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

DecompressionElimination::DecompressionElimination(
    Editor* editor, Graph* graph, MachineOperatorBuilder* machine)
    : AdvancedReducer(editor), graph_(graph), machine_(machine) {}

bool DecompressionElimination::IsValidDecompress(
    IrOpcode::Value compressOpcode, IrOpcode::Value decompressOpcode) {
  switch (compressOpcode) {
    case IrOpcode::kChangeTaggedToCompressed:
      return IrOpcode::IsDecompressOpcode(decompressOpcode);
    case IrOpcode::kChangeTaggedSignedToCompressedSigned:
      return decompressOpcode ==
             IrOpcode::kChangeCompressedSignedToTaggedSigned;
    case IrOpcode::kChangeTaggedPointerToCompressedPointer:
      return decompressOpcode ==
                 IrOpcode::kChangeCompressedPointerToTaggedPointer ||
             decompressOpcode == IrOpcode::kChangeCompressedToTagged;
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

Reduction DecompressionElimination::ReduceWord64Equal(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWord64Equal);

  DCHECK_EQ(node->InputCount(), 2);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  if (IrOpcode::IsDecompressOpcode(lhs->opcode()) &&
      IrOpcode::IsDecompressOpcode(rhs->opcode())) {
    // Do a Word32Equal on the two input nodes before they are decompressed.
    DCHECK_EQ(lhs->InputCount(), 1);
    node->ReplaceInput(0, lhs->InputAt(0));
    DCHECK_EQ(rhs->InputCount(), 1);
    node->ReplaceInput(1, rhs->InputAt(0));
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
