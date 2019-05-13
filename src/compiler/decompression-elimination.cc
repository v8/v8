// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"

namespace v8 {
namespace internal {
namespace compiler {

DecompressionElimination::DecompressionElimination(Editor* editor)
    : AdvancedReducer(editor) {}

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
      break;
  }
  UNREACHABLE();
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

Reduction DecompressionElimination::Reduce(Node* node) {
  DisallowHeapAccess no_heap_access;

  switch (node->opcode()) {
    case IrOpcode::kChangeTaggedToCompressed:
    case IrOpcode::kChangeTaggedSignedToCompressedSigned:
    case IrOpcode::kChangeTaggedPointerToCompressedPointer:
      return ReduceCompress(node);
    default:
      break;
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
