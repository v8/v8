// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

// LateEscapeAnalysis removes allocation that have no uses besides the stores
// initializing the object.

class LateEscapeAnalysisAnalyzer {
 public:
  LateEscapeAnalysisAnalyzer(const Graph& graph, Zone* zone)
      : graph_(graph),
        phase_zone_(zone),
        alloc_uses_(zone),
        allocs_(zone),
        operations_to_skip_(zone) {}

  void Run();

  bool ShouldSkipOperation(OpIndex index) {
    return operations_to_skip_.count(index) > 0;
  }

 private:
  void RecordAllocateUse(OpIndex alloc, OpIndex use);

  void CollectUsesAndAllocations();
  void FindRemovableAllocations();
  bool AllocationIsEscaping(OpIndex alloc);
  bool EscapesThroughUse(OpIndex alloc, OpIndex using_op_idx);
  void MarkToRemove(OpIndex alloc);

  const Graph& graph_;
  Zone* phase_zone_;

  // {alloc_uses_} records all the uses of each AllocateOp.
  ZoneUnorderedMap<OpIndex, ZoneVector<OpIndex>> alloc_uses_;
  // {allocs_} is filled with all of the AllocateOp of the graph, and then
  // iterated upon to determine which allocations can be removed and which
  // cannot.
  ZoneVector<OpIndex> allocs_;
  // {operations_to_skip_} contains all of the AllocateOp and StoreOp that can
  // be removed.
  ZoneUnorderedSet<OpIndex> operations_to_skip_;
};

template <class Next>
class LateEscapeAnalysisReducer : public Next {
 public:
  using Next::Asm;
  // We need the next line to not shadow Next's (and ReducerBase's in
  // particular) ShouldSkipOperation method.
  using Next::ShouldSkipOperation;

  template <class... Args>
  explicit LateEscapeAnalysisReducer(const std::tuple<Args...>& args)
      : Next(args), analyzer_(Asm().input_graph(), Asm().phase_zone()) {}

  void Analyze() {
    analyzer_.Run();
    Next::Analyze();
  }

  bool ShouldSkipOperation(const StoreOp& op, OpIndex old_idx) {
    return analyzer_.ShouldSkipOperation(old_idx) ||
           Next::ShouldSkipOperation(op, old_idx);
  }

  bool ShouldSkipOperation(const AllocateOp& op, OpIndex old_idx) {
    return analyzer_.ShouldSkipOperation(old_idx) ||
           Next::ShouldSkipOperation(op, old_idx);
  }

 private:
  LateEscapeAnalysisAnalyzer analyzer_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
