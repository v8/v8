// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ESCAPE_ANALYSIS_H_
#define V8_COMPILER_ESCAPE_ANALYSIS_H_

#include "src/compiler/graph.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class EscapeStatusAnalysis;
class MergeCache;
class VirtualState;
class VirtualObject;

// EscapeObjectAnalysis simulates stores to determine values of loads if
// an object is virtual and eliminated.
class EscapeAnalysis {
 public:
  typedef NodeId Alias;
  EscapeAnalysis(Graph* graph, CommonOperatorBuilder* common, Zone* zone);
  ~EscapeAnalysis();

  void Run();

  Node* GetReplacement(Node* node);
  bool IsVirtual(Node* node);
  bool IsEscaped(Node* node);
  bool CompareVirtualObjects(Node* left, Node* right);
  Node* GetOrCreateObjectState(Node* effect, Node* node);
  bool ExistsVirtualAllocate();

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
  int OffsetFromAccess(Node* node);
  VirtualState* CopyForModificationAt(VirtualState* state, Node* node);
  VirtualObject* CopyForModificationAt(VirtualObject* obj, VirtualState* state,
                                       Node* node);
  VirtualObject* GetVirtualObject(Node* at, NodeId id);

  bool SetEscaped(Node* node);
  Node* replacement(NodeId id);
  Node* replacement(Node* node);
  Node* ResolveReplacement(Node* node);
  Node* GetReplacement(NodeId id);
  bool SetReplacement(Node* node, Node* rep);
  bool UpdateReplacement(VirtualState* state, Node* node, Node* rep);

  VirtualObject* GetVirtualObject(VirtualState* state, Node* node);

  void DebugPrint();
  void DebugPrintState(VirtualState* state);
  void DebugPrintObject(VirtualObject* state, Alias id);

  Alias GetAlias(NodeId id) const;
  Alias AliasCount() const;

  Graph* graph() const;
  Zone* zone() const { return zone_; }
  CommonOperatorBuilder* common() const { return common_; }

  Zone* const zone_;
  CommonOperatorBuilder* const common_;
  EscapeStatusAnalysis* status_analysis_;
  ZoneVector<VirtualState*> virtual_states_;
  ZoneVector<Node*> replacements_;
  MergeCache* cache_;

  DISALLOW_COPY_AND_ASSIGN(EscapeAnalysis);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ESCAPE_ANALYSIS_H_
