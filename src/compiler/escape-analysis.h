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
class EscapeAnalysis;
class VirtualState;
class VirtualObject;


// EscapeStatusAnalysis determines for each allocation whether it escapes.
class EscapeStatusAnalysis {
 public:
  ~EscapeStatusAnalysis();

  enum EscapeStatusFlag {
    kUnknown = 0u,
    kVirtual = 1u << 0,
    kEscaped = 1u << 1,
  };
  typedef base::Flags<EscapeStatusFlag> EscapeStatusFlags;

  void Run();

  bool IsVirtual(Node* node);
  bool IsEscaped(Node* node);
  bool IsAllocation(Node* node);

  void DebugPrint();

  friend class EscapeAnalysis;

 private:
  EscapeStatusAnalysis(EscapeAnalysis* object_analysis, Graph* graph,
                       Zone* zone);
  void Process(Node* node);
  void ProcessAllocate(Node* node);
  void ProcessFinishRegion(Node* node);
  void ProcessStoreField(Node* node);
  void ProcessStoreElement(Node* node);
  bool CheckUsesForEscape(Node* node, bool phi_escaping = false) {
    return CheckUsesForEscape(node, node, phi_escaping);
  }
  bool CheckUsesForEscape(Node* node, Node* rep, bool phi_escaping = false);
  void RevisitUses(Node* node);
  void RevisitInputs(Node* node);
  bool SetEscaped(Node* node);
  bool HasEntry(Node* node);

  Graph* graph() const { return graph_; }
  Zone* zone() const { return zone_; }

  EscapeAnalysis* object_analysis_;
  Graph* const graph_;
  Zone* const zone_;
  ZoneVector<EscapeStatusFlags> info_;
  ZoneDeque<Node*> queue_;

  DISALLOW_COPY_AND_ASSIGN(EscapeStatusAnalysis);
};


DEFINE_OPERATORS_FOR_FLAGS(EscapeStatusAnalysis::EscapeStatusFlags)


class MergeCache {
 public:
  explicit MergeCache(Zone* zone)
      : states_(zone), objects_(zone), fields_(zone) {
    states_.reserve(4);
    objects_.reserve(4);
    fields_.reserve(4);
  }
  ZoneVector<VirtualState*>& states() { return states_; }
  ZoneVector<VirtualObject*>& objects() { return objects_; }
  ZoneVector<Node*>& fields() { return fields_; }
  void Clear() {
    states_.clear();
    objects_.clear();
    fields_.clear();
  }

 private:
  ZoneVector<VirtualState*> states_;
  ZoneVector<VirtualObject*> objects_;
  ZoneVector<Node*> fields_;
};


// EscapeObjectAnalysis simulates stores to determine values of loads if
// an object is virtual and eliminated.
class EscapeAnalysis {
 public:
  EscapeAnalysis(Graph* graph, CommonOperatorBuilder* common, Zone* zone);
  ~EscapeAnalysis();

  void Run();

  Node* GetReplacement(Node* node);
  bool IsVirtual(Node* node);
  bool IsEscaped(Node* node);

 private:
  void RunObjectAnalysis();
  bool Process(Node* node);
  void ProcessLoadField(Node* node);
  void ProcessStoreField(Node* node);
  void ProcessLoadElement(Node* node);
  void ProcessStoreElement(Node* node);
  void ProcessAllocationUsers(Node* node);
  void ProcessAllocation(Node* node);
  void ProcessFinishRegion(Node* node);
  void ProcessCall(Node* node);
  void ProcessStart(Node* node);
  bool ProcessEffectPhi(Node* node);
  void ProcessLoadFromPhi(int offset, Node* from, Node* node,
                          VirtualState* states);

  void ForwardVirtualState(Node* node);

  bool IsEffectBranchPoint(Node* node);
  bool IsDanglingEffectNode(Node* node);
  int OffsetFromAccess(Node* node);

  VirtualObject* GetVirtualObject(Node* at, NodeId id);
  VirtualObject* ResolveVirtualObject(VirtualState* state, Node* node);
  Node* GetReplacementIfSame(ZoneVector<VirtualObject*>& objs);

  bool SetEscaped(Node* node);
  Node* replacement(NodeId id);
  Node* replacement(Node* node);
  Node* ResolveReplacement(Node* node);
  Node* GetReplacement(NodeId id);
  bool SetReplacement(Node* node, Node* rep);
  bool UpdateReplacement(VirtualState* state, Node* node, Node* rep);

  void DebugPrint();
  void DebugPrintState(VirtualState* state);
  void DebugPrintObject(VirtualObject* state, NodeId id);

  Graph* graph() const { return graph_; }
  CommonOperatorBuilder* common() const { return common_; }
  Zone* zone() const { return zone_; }

  Graph* const graph_;
  CommonOperatorBuilder* const common_;
  Zone* const zone_;
  ZoneVector<VirtualState*> virtual_states_;
  ZoneVector<Node*> replacements_;
  EscapeStatusAnalysis escape_status_;
  MergeCache cache_;

  DISALLOW_COPY_AND_ASSIGN(EscapeAnalysis);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ESCAPE_ANALYSIS_H_
