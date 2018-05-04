// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_GRAPH_H_
#define V8_COMPILER_JS_GRAPH_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/node-properties.h"
#include "src/globals.h"
#include "src/isolate.h"

namespace v8 {
namespace internal {
namespace compiler {

class SimplifiedOperatorBuilder;
class Typer;

// Implements a facade on a Graph, enhancing the graph with JS-specific
// notions, including various builders for operators, canonicalized global
// constants, and various helper methods.
class V8_EXPORT_PRIVATE JSGraph : public MachineGraph {
 public:
  JSGraph(Isolate* isolate, Graph* graph, CommonOperatorBuilder* common,
          JSOperatorBuilder* javascript, SimplifiedOperatorBuilder* simplified,
          MachineOperatorBuilder* machine)
      : MachineGraph(graph, common, machine),
        isolate_(isolate),
        javascript_(javascript),
        simplified_(simplified) {
    for (int i = 0; i < kNumCachedNodes; i++) cached_nodes_[i] = nullptr;
  }

  // Canonicalized global constants.
  Node* AllocateInNewSpaceStubConstant();
  Node* AllocateInOldSpaceStubConstant();
  Node* ArrayConstructorStubConstant();
  Node* ToNumberBuiltinConstant();
  Node* CEntryStubConstant(int result_size,
                           SaveFPRegsMode save_doubles = kDontSaveFPRegs,
                           ArgvMode argv_mode = kArgvOnStack,
                           bool builtin_exit_frame = false);
  Node* EmptyFixedArrayConstant();
  Node* EmptyStringConstant();
  Node* FixedArrayMapConstant();
  Node* PropertyArrayMapConstant();
  Node* FixedDoubleArrayMapConstant();
  Node* HeapNumberMapConstant();
  Node* OptimizedOutConstant();
  Node* StaleRegisterConstant();
  Node* UndefinedConstant();
  Node* TheHoleConstant();
  Node* TrueConstant();
  Node* FalseConstant();
  Node* NullConstant();
  Node* ZeroConstant();
  Node* OneConstant();
  Node* NaNConstant();
  Node* MinusOneConstant();

  // Used for padding frames.
  Node* PaddingConstant() { return TheHoleConstant(); }

  // Creates a HeapConstant node, possibly canonicalized, and may access the
  // heap to inspect the object.
  Node* HeapConstant(Handle<HeapObject> value);

  // Creates a Constant node of the appropriate type for the given object.
  // Accesses the heap to inspect the object and determine whether one of the
  // canonicalized globals or a number constant should be returned.
  Node* Constant(Handle<Object> value);

  // Creates a NumberConstant node, usually canonicalized.
  Node* Constant(double value);

  // Creates a NumberConstant node, usually canonicalized.
  Node* Constant(int32_t value);

  // Creates a NumberConstant node, usually canonicalized.
  Node* Constant(uint32_t value);

  // Creates a HeapConstant node for either true or false.
  Node* BooleanConstant(bool is_true) {
    return is_true ? TrueConstant() : FalseConstant();
  }

  Node* SmiConstant(int32_t immediate) {
    DCHECK(Smi::IsValid(immediate));
    return Constant(immediate);
  }

  // Creates a dummy Constant node, used to satisfy calling conventions of
  // stubs and runtime functions that do not require a context.
  Node* NoContextConstant() { return ZeroConstant(); }

  // Creates an empty StateValues node, used when we don't have any concrete
  // values for a certain part of the frame state.
  Node* EmptyStateValues();

  // Typed state values with a single dead input. This is useful to represent
  // dead accumulator.
  Node* SingleDeadTypedStateValues();

  // Create a control node that serves as dependency for dead nodes.
  Node* Dead();

  JSOperatorBuilder* javascript() const { return javascript_; }
  SimplifiedOperatorBuilder* simplified() const { return simplified_; }
  Isolate* isolate() const { return isolate_; }
  Factory* factory() const { return isolate()->factory(); }

  void GetCachedNodes(NodeVector* nodes);

 private:
  enum CachedNode {
    kAllocateInNewSpaceStubConstant,
    kAllocateInOldSpaceStubConstant,
    kArrayConstructorStubConstant,
    kToNumberBuiltinConstant,
    kCEntryStub1Constant,
    kCEntryStub2Constant,
    kCEntryStub3Constant,
    kCEntryStub1WithBuiltinExitFrameConstant,
    kEmptyFixedArrayConstant,
    kEmptyStringConstant,
    kFixedArrayMapConstant,
    kFixedDoubleArrayMapConstant,
    kPropertyArrayMapConstant,
    kHeapNumberMapConstant,
    kOptimizedOutConstant,
    kStaleRegisterConstant,
    kUndefinedConstant,
    kTheHoleConstant,
    kTrueConstant,
    kFalseConstant,
    kNullConstant,
    kZeroConstant,
    kOneConstant,
    kMinusOneConstant,
    kNaNConstant,
    kEmptyStateValues,
    kSingleDeadTypedStateValues,
    kDead,
    kNumCachedNodes  // Must remain last.
  };

  Isolate* isolate_;
  JSOperatorBuilder* javascript_;
  SimplifiedOperatorBuilder* simplified_;
  Node* cached_nodes_[kNumCachedNodes];

  Node* NumberConstant(double value);

  DISALLOW_COPY_AND_ASSIGN(JSGraph);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_GRAPH_H_
