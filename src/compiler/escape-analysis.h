// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ESCAPE_ANALYSIS_H_
#define V8_COMPILER_ESCAPE_ANALYSIS_H_

#include "src/base/flags.h"
#include "src/compiler/graph.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class EscapeObjectAnalysis;
class VirtualState;


// EscapeStatusAnalysis determines for each allocation whether it escapes.
class EscapeStatusAnalysis {
 public:
  EscapeStatusAnalysis(EscapeObjectAnalysis* object_analysis, Graph* graph,
                       Zone* zone);
  ~EscapeStatusAnalysis();

  enum EscapeStatusFlag {
    kUnknown = 0u,
    kVirtual = 1u << 0,
    kEscaped = 1u << 1,
  };
  typedef base::Flags<EscapeStatusFlag> EscapeStatusFlags;

  void Run();

  bool HasEntry(Node* node);
  bool IsVirtual(Node* node);
  bool IsEscaped(Node* node);

  void DebugPrint();

 private:
  void Process(Node* node);
  void ProcessAllocate(Node* node);
  void ProcessFinishRegion(Node* node);
  void ProcessStoreField(Node* node);
  bool CheckUsesForEscape(Node* node) { return CheckUsesForEscape(node, node); }
  bool CheckUsesForEscape(Node* node, Node* rep);
  void RevisitUses(Node* node);
  void RevisitInputs(Node* node);
  bool SetEscaped(Node* node);

  Graph* graph() const { return graph_; }
  Zone* zone() const { return zone_; }

  EscapeObjectAnalysis* object_analysis_;
  Graph* const graph_;
  Zone* const zone_;
  ZoneVector<EscapeStatusFlags> info_;
  ZoneDeque<Node*> queue_;

  DISALLOW_COPY_AND_ASSIGN(EscapeStatusAnalysis);
};


DEFINE_OPERATORS_FOR_FLAGS(EscapeStatusAnalysis::EscapeStatusFlags)


// EscapeObjectAnalysis simulates stores to determine values of loads if
// an object is virtual and eliminated.
class EscapeObjectAnalysis {
 public:
  EscapeObjectAnalysis(Graph* graph, CommonOperatorBuilder* common, Zone* zone);
  ~EscapeObjectAnalysis();

  void Run();

  Node* GetReplacement(Node* at, NodeId id);

 private:
  bool Process(Node* node);
  void ProcessLoadField(Node* node);
  void ProcessStoreField(Node* node);
  void ProcessAllocation(Node* node);
  void ProcessFinishRegion(Node* node);
  void ProcessCall(Node* node);
  void ProcessStart(Node* node);
  bool ProcessEffectPhi(Node* node);
  void ForwardVirtualState(Node* node);
  bool IsEffectBranchPoint(Node* node);
  bool IsDanglingEffectNode(Node* node);
  int OffsetFromAccess(Node* node);

  void DebugPrint();

  Graph* graph() const { return graph_; }
  CommonOperatorBuilder* common() const { return common_; }
  Zone* zone() const { return zone_; }

  Graph* const graph_;
  CommonOperatorBuilder* const common_;
  Zone* const zone_;
  ZoneVector<VirtualState*> virtual_states_;

  DISALLOW_COPY_AND_ASSIGN(EscapeObjectAnalysis);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ESCAPE_ANALYSIS_H_
