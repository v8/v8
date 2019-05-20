// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DECOMPRESSION_ELIMINATION_H_
#define V8_COMPILER_DECOMPRESSION_ELIMINATION_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

// Performs elimination of redundant decompressions within the graph.
class V8_EXPORT_PRIVATE DecompressionElimination final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  explicit DecompressionElimination(Editor* editor, Graph* graph,
                                    MachineOperatorBuilder* machine);
  ~DecompressionElimination() final = default;

  const char* reducer_name() const override {
    return "DecompressionElimination";
  }

  Reduction Reduce(Node* node) final;

 private:
  // Removes direct Decompressions & Compressions, going from
  //     Parent <- Decompression <- Compression <- Child
  // to
  //     Parent <- Child
  // Can be used for Any, Signed, and Pointer compressions.
  Reduction ReduceCompress(Node* node);

  // Replaces a Word64Equal with a Word32Equal if both of its inputs are
  // Decompress nodes. In that case, it uses the inputs to each of the
  // Decompress nodes.
  Reduction ReduceWord64Equal(Node* node);

  // Returns true if the decompress opcode is valid for the compressed one.
  bool IsValidDecompress(IrOpcode::Value compressOpcode,
                         IrOpcode::Value decompressOpcode);

  Graph* graph() const { return graph_; }
  MachineOperatorBuilder* machine() const { return machine_; }
  Graph* const graph_;
  MachineOperatorBuilder* const machine_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DECOMPRESSION_ELIMINATION_H_
