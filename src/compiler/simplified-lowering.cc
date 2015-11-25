// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-lowering.h"

#include <limits>

#include "src/base/bits.h"
#include "src/code-factory.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/diamond.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/representation-change.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/source-position.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace compiler {

// Macro for outputting trace information from representation inference.
#define TRACE(...)                                      \
  do {                                                  \
    if (FLAG_trace_representation) PrintF(__VA_ARGS__); \
  } while (false)

// Representation selection and lowering of {Simplified} operators to machine
// operators are interwined. We use a fixpoint calculation to compute both the
// output representation and the best possible lowering for {Simplified} nodes.
// Representation change insertion ensures that all values are in the correct
// machine representation after this phase, as dictated by the machine
// operators themselves.
enum Phase {
  // 1.) PROPAGATE: Traverse the graph from the end, pushing usage information
  //     backwards from uses to definitions, around cycles in phis, according
  //     to local rules for each operator.
  //     During this phase, the usage information for a node determines the best
  //     possible lowering for each operator so far, and that in turn determines
  //     the output representation.
  //     Therefore, to be correct, this phase must iterate to a fixpoint before
  //     the next phase can begin.
  PROPAGATE,

  // 2.) LOWER: perform lowering for all {Simplified} nodes by replacing some
  //     operators for some nodes, expanding some nodes to multiple nodes, or
  //     removing some (redundant) nodes.
  //     During this phase, use the {RepresentationChanger} to insert
  //     representation changes between uses that demand a particular
  //     representation and nodes that produce a different representation.
  LOWER
};


namespace {

// The {UseInfo} class is used to describe a use of an input of a node.
//
// This information is used in two different ways, based on the phase:
//
// 1. During propagation, the use info is used to inform the input node
//    about what part of the input is used (we call this truncation) and what
//    is the preferred representation.
//
// 2. During lowering, the use info is used to properly convert the input
//    to the preferred representation. The preferred representation might be
//    insufficient to do the conversion (e.g. word32->float64 conv), so we also
//    need the signedness information to produce the correct value.
class UseInfo {
 public:
  UseInfo(MachineType preferred, Truncation truncation)
      : preferred_(preferred), truncation_(truncation) {
    DCHECK(preferred == (preferred & kRepMask));
  }
  static UseInfo TruncatingWord32() {
    return UseInfo(kRepWord32, Truncation::Word32());
  }
  static UseInfo TruncatingWord64() {
    return UseInfo(kRepWord64, Truncation::Word64());
  }
  static UseInfo Bool() { return UseInfo(kRepBit, Truncation::Bool()); }
  static UseInfo Float32() {
    return UseInfo(kRepFloat32, Truncation::Float32());
  }
  static UseInfo Float64() {
    return UseInfo(kRepFloat64, Truncation::Float64());
  }
  static UseInfo PointerInt() {
    return kPointerSize == 4 ? TruncatingWord32() : TruncatingWord64();
  }
  static UseInfo AnyTagged() { return UseInfo(kRepTagged, Truncation::Any()); }

  // Undetermined representation.
  static UseInfo Any() { return UseInfo(kMachNone, Truncation::Any()); }
  static UseInfo None() { return UseInfo(kMachNone, Truncation::None()); }

  // Truncation to a representation that is smaller than the preferred
  // one.
  static UseInfo Float64TruncatingToWord32() {
    return UseInfo(kRepFloat64, Truncation::Word32());
  }
  static UseInfo Word64TruncatingToWord32() {
    return UseInfo(kRepWord64, Truncation::Word32());
  }
  static UseInfo AnyTruncatingToBool() {
    return UseInfo(kMachNone, Truncation::Bool());
  }

  MachineType preferred() const { return preferred_; }
  Truncation truncation() const { return truncation_; }

 private:
  MachineType preferred_;
  Truncation truncation_;
};


UseInfo UseInfoFromMachineType(MachineType type) {
  MachineTypeUnion rep = RepresentationOf(type);
  DCHECK((rep & kTypeMask) == 0);

  if (rep & kRepTagged) return UseInfo::AnyTagged();
  if (rep & kRepFloat64) {
    DCHECK((rep & kRepWord64) == 0);
    return UseInfo::Float64();
  }
  if (rep & kRepFloat32) {
    if (rep == kRepFloat32) return UseInfo::Float32();
    return UseInfo::AnyTagged();
  }
  if (rep & kRepWord64) {
    return UseInfo::TruncatingWord64();
  }
  if (rep & (kRepWord32 | kRepWord16 | kRepWord8)) {
    CHECK(!(rep & kRepBit));
    return UseInfo::TruncatingWord32();
  }
  DCHECK(rep & kRepBit);
  return UseInfo::Bool();
}


UseInfo UseInfoForBasePointer(const FieldAccess& access) {
  return access.tag() != 0 ? UseInfo::AnyTagged() : UseInfo::PointerInt();
}


UseInfo UseInfoForBasePointer(const ElementAccess& access) {
  return access.tag() != 0 ? UseInfo::AnyTagged() : UseInfo::PointerInt();
}

}  // namespace


class RepresentationSelector {
 public:
  // Information for each node tracked during the fixpoint.
  class NodeInfo {
   public:
    // Adds new use to the node. Returns true if something has changed
    // and the node has to be requeued.
    bool AddUse(UseInfo info) {
      Truncation old_truncation = truncation_;
      truncation_ = Truncation::Generalize(truncation_, info.truncation());
      return truncation_ != old_truncation;
    }

    void set_queued(bool value) { queued_ = value; }
    bool queued() const { return queued_; }
    void set_visited() { visited_ = true; }
    bool visited() const { return visited_; }
    Truncation truncation() const { return truncation_; }
    void set_output_type(MachineTypeUnion type) { output_ = type; }
    MachineTypeUnion output_type() const { return output_; }

   private:
    bool queued_ = false;                  // Bookkeeping for the traversal.
    bool visited_ = false;                 // Bookkeeping for the traversal.
    MachineTypeUnion output_ = kMachNone;  // Output type of the node.
    Truncation truncation_ = Truncation::None();  // Information about uses.
  };

  RepresentationSelector(JSGraph* jsgraph, Zone* zone,
                         RepresentationChanger* changer,
                         SourcePositionTable* source_positions)
      : jsgraph_(jsgraph),
        count_(jsgraph->graph()->NodeCount()),
        info_(count_, zone),
        nodes_(zone),
        replacements_(zone),
        phase_(PROPAGATE),
        changer_(changer),
        queue_(zone),
        source_positions_(source_positions) {
    safe_int_additive_range_ =
        Type::Range(-std::pow(2.0, 52.0), std::pow(2.0, 52.0), zone);
  }

  void Run(SimplifiedLowering* lowering) {
    // Run propagation phase to a fixpoint.
    TRACE("--{Propagation phase}--\n");
    phase_ = PROPAGATE;
    Enqueue(jsgraph_->graph()->end());
    // Process nodes from the queue until it is empty.
    while (!queue_.empty()) {
      Node* node = queue_.front();
      NodeInfo* info = GetInfo(node);
      queue_.pop();
      info->set_queued(false);
      TRACE(" visit #%d: %s\n", node->id(), node->op()->mnemonic());
      VisitNode(node, info->truncation(), NULL);
      TRACE("  ==> output ");
      PrintInfo(info->output_type());
      TRACE("\n");
    }

    // Run lowering and change insertion phase.
    TRACE("--{Simplified lowering phase}--\n");
    phase_ = LOWER;
    // Process nodes from the collected {nodes_} vector.
    for (NodeVector::iterator i = nodes_.begin(); i != nodes_.end(); ++i) {
      Node* node = *i;
      NodeInfo* info = GetInfo(node);
      TRACE(" visit #%d: %s\n", node->id(), node->op()->mnemonic());
      // Reuse {VisitNode()} so the representation rules are in one place.
      SourcePositionTable::Scope scope(
          source_positions_, source_positions_->GetSourcePosition(node));
      VisitNode(node, info->truncation(), lowering);
    }

    // Perform the final replacements.
    for (NodeVector::iterator i = replacements_.begin();
         i != replacements_.end(); ++i) {
      Node* node = *i;
      Node* replacement = *(++i);
      node->ReplaceUses(replacement);
      // We also need to replace the node in the rest of the vector.
      for (NodeVector::iterator j = i + 1; j != replacements_.end(); ++j) {
        ++j;
        if (*j == node) *j = replacement;
      }
    }
  }

  // Enqueue {node} if the {use} contains new information for that node.
  // Add {node} to {nodes_} if this is the first time it's been visited.
  void Enqueue(Node* node, UseInfo use_info = UseInfo::None()) {
    if (phase_ != PROPAGATE) return;
    NodeInfo* info = GetInfo(node);
    if (!info->visited()) {
      // First visit of this node.
      info->set_visited();
      info->set_queued(true);
      nodes_.push_back(node);
      queue_.push(node);
      TRACE("  initial: ");
      info->AddUse(use_info);
      PrintTruncation(info->truncation());
      return;
    }
    TRACE("   queue?: ");
    PrintTruncation(info->truncation());
    if (info->AddUse(use_info)) {
      // New usage information for the node is available.
      if (!info->queued()) {
        queue_.push(node);
        info->set_queued(true);
        TRACE("   added: ");
      } else {
        TRACE(" inqueue: ");
      }
      PrintTruncation(info->truncation());
    }
  }

  bool lower() { return phase_ == LOWER; }

  void SetOutput(Node* node, MachineTypeUnion output) {
    // Every node should have at most one output representation. Note that
    // phis can have 0, if they have not been used in a representation-inducing
    // instruction.
    DCHECK((output & kRepMask) == 0 ||
           base::bits::IsPowerOfTwo32(output & kRepMask));
    GetInfo(node)->set_output_type(output);
  }

  bool BothInputsAre(Node* node, Type* type) {
    DCHECK_EQ(2, node->InputCount());
    return NodeProperties::GetType(node->InputAt(0))->Is(type) &&
           NodeProperties::GetType(node->InputAt(1))->Is(type);
  }

  void EnqueueInputUse(Node* node, int index, UseInfo use) {
    Enqueue(node->InputAt(index), use);
  }

  void ConvertInput(Node* node, int index, UseInfo use) {
    Node* input = node->InputAt(index);
    // In the change phase, insert a change before the use if necessary.
    if (use.preferred() == kMachNone)
      return;  // No input requirement on the use.
    MachineTypeUnion output = GetInfo(input)->output_type();
    if ((output & kRepMask) != use.preferred()) {
      // Output representation doesn't match usage.
      TRACE("  change: #%d:%s(@%d #%d:%s) ", node->id(), node->op()->mnemonic(),
            index, input->id(), input->op()->mnemonic());
      TRACE(" from ");
      PrintInfo(output);
      TRACE(" to ");
      PrintUseInfo(use);
      TRACE("\n");
      Node* n = changer_->GetRepresentationFor(input, output, use.preferred(),
                                               use.truncation());
      node->ReplaceInput(index, n);
    }
  }

  void ProcessInput(Node* node, int index, UseInfo use) {
    if (phase_ == PROPAGATE) {
      EnqueueInputUse(node, index, use);
    } else {
      ConvertInput(node, index, use);
    }
  }

  void ProcessRemainingInputs(Node* node, int index) {
    DCHECK_GE(index, NodeProperties::PastValueIndex(node));
    DCHECK_GE(index, NodeProperties::PastContextIndex(node));
    for (int i = std::max(index, NodeProperties::FirstEffectIndex(node));
         i < NodeProperties::PastEffectIndex(node); ++i) {
      Enqueue(node->InputAt(i));  // Effect inputs: just visit
    }
    for (int i = std::max(index, NodeProperties::FirstControlIndex(node));
         i < NodeProperties::PastControlIndex(node); ++i) {
      Enqueue(node->InputAt(i));  // Control inputs: just visit
    }
  }

  // The default, most general visitation case. For {node}, process all value,
  // context, frame state, effect, and control inputs, assuming that value
  // inputs should have {kRepTagged} representation and can observe all output
  // values {kTypeAny}.
  void VisitInputs(Node* node) {
    int tagged_count = node->op()->ValueInputCount() +
                       OperatorProperties::GetContextInputCount(node->op());
    // Visit value and context inputs as tagged.
    for (int i = 0; i < tagged_count; i++) {
      ProcessInput(node, i, UseInfo::AnyTagged());
    }
    // Only enqueue other inputs (framestates, effects, control).
    for (int i = tagged_count; i < node->InputCount(); i++) {
      Enqueue(node->InputAt(i));
    }
    // Assume the output is tagged.
    SetOutput(node, kMachAnyTagged);
  }

  // Helper for binops of the R x L -> O variety.
  void VisitBinop(Node* node, UseInfo left_use, UseInfo right_use,
                  MachineTypeUnion output) {
    DCHECK_EQ(2, node->op()->ValueInputCount());
    ProcessInput(node, 0, left_use);
    ProcessInput(node, 1, right_use);
    for (int i = 2; i < node->InputCount(); i++) {
      Enqueue(node->InputAt(i));
    }
    SetOutput(node, output);
  }

  // Helper for binops of the I x I -> O variety.
  void VisitBinop(Node* node, UseInfo input_use, MachineTypeUnion output) {
    VisitBinop(node, input_use, input_use, output);
  }

  // Helper for unops of the I -> O variety.
  void VisitUnop(Node* node, UseInfo input_use, MachineTypeUnion output) {
    DCHECK_EQ(1, node->InputCount());
    ProcessInput(node, 0, input_use);
    SetOutput(node, output);
  }

  // Helper for leaf nodes.
  void VisitLeaf(Node* node, MachineTypeUnion output) {
    DCHECK_EQ(0, node->InputCount());
    SetOutput(node, output);
  }

  // Helpers for specific types of binops.
  void VisitFloat64Binop(Node* node) {
    VisitBinop(node, UseInfo::Float64(), kMachFloat64);
  }
  void VisitInt32Binop(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord32(), kMachInt32);
  }
  void VisitUint32Binop(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord32(), kMachUint32);
  }
  void VisitInt64Binop(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord64(), kMachInt64);
  }
  void VisitUint64Binop(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord64(), kMachUint64);
  }
  void VisitFloat64Cmp(Node* node) {
    VisitBinop(node, UseInfo::Float64(), kMachBool);
  }
  void VisitInt32Cmp(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord32(), kMachBool);
  }
  void VisitUint32Cmp(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord32(), kMachBool);
  }
  void VisitInt64Cmp(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord64(), kMachBool);
  }
  void VisitUint64Cmp(Node* node) {
    VisitBinop(node, UseInfo::TruncatingWord64(), kMachBool);
  }

  // Infer representation for phi-like nodes.
  static MachineType GetRepresentationForPhi(Node* node, Truncation use) {
    // Phis adapt to the output representation their uses demand.
    Type* upper = NodeProperties::GetType(node);
    if (upper->Is(Type::Signed32()) || upper->Is(Type::Unsigned32())) {
      // We are within 32 bits range => pick kRepWord32.
      return kRepWord32;
    } else if (use.TruncatesToWord32()) {
      // We only use 32 bits.
      return kRepWord32;
    } else if (upper->Is(Type::Boolean())) {
      // multiple uses => pick kRepBit.
      return kRepBit;
    } else if (upper->Is(Type::Number())) {
      // multiple uses => pick kRepFloat64.
      return kRepFloat64;
    } else if (upper->Is(Type::Internal())) {
      return kMachPtr;
    }
    return kRepTagged;
  }

  // Helper for handling selects.
  void VisitSelect(Node* node, Truncation truncation,
                   SimplifiedLowering* lowering) {
    ProcessInput(node, 0, UseInfo::Bool());
    MachineType output = GetRepresentationForPhi(node, truncation);

    Type* upper = NodeProperties::GetType(node);
    MachineType output_type =
        static_cast<MachineType>(changer_->TypeFromUpperBound(upper) | output);
    SetOutput(node, output_type);

    if (lower()) {
      // Update the select operator.
      SelectParameters p = SelectParametersOf(node->op());
      MachineType type = static_cast<MachineType>(output_type);
      if (type != p.type()) {
        NodeProperties::ChangeOp(node,
                                 lowering->common()->Select(type, p.hint()));
      }
    }
    // Convert inputs to the output representation of this phi, pass the
    // truncation truncation along.
    UseInfo input_use(output, truncation);
    ProcessInput(node, 1, input_use);
    ProcessInput(node, 2, input_use);
  }

  // Helper for handling phis.
  void VisitPhi(Node* node, Truncation truncation,
                SimplifiedLowering* lowering) {
    MachineType output = GetRepresentationForPhi(node, truncation);

    Type* upper = NodeProperties::GetType(node);
    MachineType output_type =
        static_cast<MachineType>(changer_->TypeFromUpperBound(upper) | output);
    SetOutput(node, output_type);

    int values = node->op()->ValueInputCount();

    if (lower()) {
      // Update the phi operator.
      MachineType type = static_cast<MachineType>(output_type);
      if (type != OpParameter<MachineType>(node)) {
        NodeProperties::ChangeOp(node, lowering->common()->Phi(type, values));
      }
    }

    // Convert inputs to the output representation of this phi, pass the
    // truncation truncation along.
    UseInfo input_use(output, truncation);
    for (int i = 0; i < node->InputCount(); i++) {
      ProcessInput(node, i, i < values ? input_use : UseInfo::None());
    }
  }

  void VisitCall(Node* node, SimplifiedLowering* lowering) {
    const CallDescriptor* desc = OpParameter<const CallDescriptor*>(node->op());
    const MachineSignature* sig = desc->GetMachineSignature();
    int params = static_cast<int>(sig->parameter_count());
    // Propagate representation information from call descriptor.
    for (int i = 0; i < node->InputCount(); i++) {
      if (i == 0) {
        // The target of the call.
        ProcessInput(node, i, UseInfo::None());
      } else if ((i - 1) < params) {
        ProcessInput(node, i, UseInfoFromMachineType(sig->GetParam(i - 1)));
      } else {
        ProcessInput(node, i, UseInfo::None());
      }
    }

    if (sig->return_count() > 0) {
      SetOutput(node, desc->GetMachineSignature()->GetReturn());
    } else {
      SetOutput(node, kMachAnyTagged);
    }
  }

  void VisitStateValues(Node* node) {
    if (phase_ == PROPAGATE) {
      for (int i = 0; i < node->InputCount(); i++) {
        Enqueue(node->InputAt(i), UseInfo::Any());
      }
    } else {
      Zone* zone = jsgraph_->zone();
      ZoneVector<MachineType>* types =
          new (zone->New(sizeof(ZoneVector<MachineType>)))
              ZoneVector<MachineType>(node->InputCount(), zone);
      for (int i = 0; i < node->InputCount(); i++) {
        MachineTypeUnion input_type = GetInfo(node->InputAt(i))->output_type();
        (*types)[i] = static_cast<MachineType>(input_type);
      }
      NodeProperties::ChangeOp(node,
                               jsgraph_->common()->TypedStateValues(types));
    }
    SetOutput(node, kMachAnyTagged);
  }

  const Operator* Int32Op(Node* node) {
    return changer_->Int32OperatorFor(node->opcode());
  }

  const Operator* Uint32Op(Node* node) {
    return changer_->Uint32OperatorFor(node->opcode());
  }

  const Operator* Float64Op(Node* node) {
    return changer_->Float64OperatorFor(node->opcode());
  }

  bool CanLowerToInt32Binop(Node* node, Truncation use) {
    return BothInputsAre(node, Type::Signed32()) &&
           (use.TruncatesToWord32() ||
            NodeProperties::GetType(node)->Is(Type::Signed32()));
  }

  bool CanLowerToInt32AdditiveBinop(Node* node, Truncation use) {
    // It is safe to lower to word32 operation if:
    // - the inputs are safe integers (so the low bits are not discarded), and
    // - the uses can only observe the lowest 32 bits or they can recover the
    //   the value from the type.
    // TODO(jarin): we could support the uint32 case here, but that would
    // require setting kTypeUint32 as the output type. Eventually, we will want
    // to use only the big types, then this should work automatically.
    return BothInputsAre(node, safe_int_additive_range_) &&
           (use.TruncatesToWord32() ||
            NodeProperties::GetType(node)->Is(Type::Signed32()));
  }

  // Dispatching routine for visiting the node {node} with the usage {use}.
  // Depending on the operator, propagate new usage info to the inputs.
  void VisitNode(Node* node, Truncation truncation,
                 SimplifiedLowering* lowering) {
    switch (node->opcode()) {
      //------------------------------------------------------------------
      // Common operators.
      //------------------------------------------------------------------
      case IrOpcode::kStart:
      case IrOpcode::kDead:
        return VisitLeaf(node, 0);
      case IrOpcode::kParameter: {
        // TODO(titzer): use representation from linkage.
        Type* upper = NodeProperties::GetType(node);
        ProcessInput(node, 0, UseInfo::None());
        SetOutput(node, kRepTagged | changer_->TypeFromUpperBound(upper));
        return;
      }
      case IrOpcode::kInt32Constant:
        return VisitLeaf(node, kRepWord32);
      case IrOpcode::kInt64Constant:
        return VisitLeaf(node, kRepWord64);
      case IrOpcode::kFloat32Constant:
        return VisitLeaf(node, kRepFloat32);
      case IrOpcode::kFloat64Constant:
        return VisitLeaf(node, kRepFloat64);
      case IrOpcode::kExternalConstant:
        return VisitLeaf(node, kMachPtr);
      case IrOpcode::kNumberConstant:
        return VisitLeaf(node, kRepTagged);
      case IrOpcode::kHeapConstant:
        return VisitLeaf(node, kRepTagged);

      case IrOpcode::kBranch:
        ProcessInput(node, 0, UseInfo::Bool());
        Enqueue(NodeProperties::GetControlInput(node, 0));
        break;
      case IrOpcode::kSwitch:
        ProcessInput(node, 0, UseInfo::TruncatingWord32());
        Enqueue(NodeProperties::GetControlInput(node, 0));
        break;
      case IrOpcode::kSelect:
        return VisitSelect(node, truncation, lowering);
      case IrOpcode::kPhi:
        return VisitPhi(node, truncation, lowering);
      case IrOpcode::kCall:
        return VisitCall(node, lowering);

//------------------------------------------------------------------
// JavaScript operators.
//------------------------------------------------------------------
// For now, we assume that all JS operators were too complex to lower
// to Simplified and that they will always require tagged value inputs
// and produce tagged value outputs.
// TODO(turbofan): it might be possible to lower some JSOperators here,
// but that responsibility really lies in the typed lowering phase.
#define DEFINE_JS_CASE(x) case IrOpcode::k##x:
        JS_OP_LIST(DEFINE_JS_CASE)
#undef DEFINE_JS_CASE
        VisitInputs(node);
        return SetOutput(node, kRepTagged);

      //------------------------------------------------------------------
      // Simplified operators.
      //------------------------------------------------------------------
      case IrOpcode::kBooleanNot: {
        if (lower()) {
          MachineTypeUnion input = GetInfo(node->InputAt(0))->output_type();
          if (input & kRepBit) {
            // BooleanNot(x: kRepBit) => Word32Equal(x, #0)
            node->AppendInput(jsgraph_->zone(), jsgraph_->Int32Constant(0));
            NodeProperties::ChangeOp(node, lowering->machine()->Word32Equal());
          } else {
            // BooleanNot(x: kRepTagged) => WordEqual(x, #false)
            node->AppendInput(jsgraph_->zone(), jsgraph_->FalseConstant());
            NodeProperties::ChangeOp(node, lowering->machine()->WordEqual());
          }
        } else {
          // No input representation requirement; adapt during lowering.
          ProcessInput(node, 0, UseInfo::AnyTruncatingToBool());
          SetOutput(node, kRepBit);
        }
        break;
      }
      case IrOpcode::kBooleanToNumber: {
        if (lower()) {
          MachineTypeUnion input = GetInfo(node->InputAt(0))->output_type();
          if (input & kRepBit) {
            // BooleanToNumber(x: kRepBit) => x
            DeferReplacement(node, node->InputAt(0));
          } else {
            // BooleanToNumber(x: kRepTagged) => WordEqual(x, #true)
            node->AppendInput(jsgraph_->zone(), jsgraph_->TrueConstant());
            NodeProperties::ChangeOp(node, lowering->machine()->WordEqual());
          }
        } else {
          // No input representation requirement; adapt during lowering.
          ProcessInput(node, 0, UseInfo::AnyTruncatingToBool());
          SetOutput(node, kMachInt32);
        }
        break;
      }
      case IrOpcode::kNumberEqual:
      case IrOpcode::kNumberLessThan:
      case IrOpcode::kNumberLessThanOrEqual: {
        // Number comparisons reduce to integer comparisons for integer inputs.
        if (BothInputsAre(node, Type::Signed32())) {
          // => signed Int32Cmp
          VisitInt32Cmp(node);
          if (lower()) NodeProperties::ChangeOp(node, Int32Op(node));
        } else if (BothInputsAre(node, Type::Unsigned32())) {
          // => unsigned Int32Cmp
          VisitUint32Cmp(node);
          if (lower()) NodeProperties::ChangeOp(node, Uint32Op(node));
        } else {
          // => Float64Cmp
          VisitFloat64Cmp(node);
          if (lower()) NodeProperties::ChangeOp(node, Float64Op(node));
        }
        break;
      }
      case IrOpcode::kNumberAdd:
      case IrOpcode::kNumberSubtract: {
        // Add and subtract reduce to Int32Add/Sub if the inputs
        // are already integers and all uses are truncating.
        if (CanLowerToInt32AdditiveBinop(node, truncation)) {
          // => signed Int32Add/Sub
          VisitInt32Binop(node);
          if (lower()) NodeProperties::ChangeOp(node, Int32Op(node));
        } else {
          // => Float64Add/Sub
          VisitFloat64Binop(node);
          if (lower()) NodeProperties::ChangeOp(node, Float64Op(node));
        }
        break;
      }
      case IrOpcode::kNumberMultiply: {
        NumberMatcher right(node->InputAt(1));
        if (right.IsInRange(-1048576, 1048576)) {  // must fit double mantissa.
          if (CanLowerToInt32Binop(node, truncation)) {
            // => signed Int32Mul
            VisitInt32Binop(node);
            if (lower()) NodeProperties::ChangeOp(node, Int32Op(node));
            break;
          }
        }
        // => Float64Mul
        VisitFloat64Binop(node);
        if (lower()) NodeProperties::ChangeOp(node, Float64Op(node));
        break;
      }
      case IrOpcode::kNumberDivide: {
        if (CanLowerToInt32Binop(node, truncation)) {
          // => signed Int32Div
          VisitInt32Binop(node);
          if (lower()) DeferReplacement(node, lowering->Int32Div(node));
          break;
        }
        if (BothInputsAre(node, Type::Unsigned32()) &&
            truncation.TruncatesNaNToZero()) {
          // => unsigned Uint32Div
          VisitUint32Binop(node);
          if (lower()) DeferReplacement(node, lowering->Uint32Div(node));
          break;
        }
        // => Float64Div
        VisitFloat64Binop(node);
        if (lower()) NodeProperties::ChangeOp(node, Float64Op(node));
        break;
      }
      case IrOpcode::kNumberModulus: {
        if (CanLowerToInt32Binop(node, truncation)) {
          // => signed Int32Mod
          VisitInt32Binop(node);
          if (lower()) DeferReplacement(node, lowering->Int32Mod(node));
          break;
        }
        if (BothInputsAre(node, Type::Unsigned32()) &&
            truncation.TruncatesNaNToZero()) {
          // => unsigned Uint32Mod
          VisitUint32Binop(node);
          if (lower()) DeferReplacement(node, lowering->Uint32Mod(node));
          break;
        }
        // => Float64Mod
        VisitFloat64Binop(node);
        if (lower()) NodeProperties::ChangeOp(node, Float64Op(node));
        break;
      }
      case IrOpcode::kNumberBitwiseOr:
      case IrOpcode::kNumberBitwiseXor:
      case IrOpcode::kNumberBitwiseAnd: {
        VisitInt32Binop(node);
        if (lower()) NodeProperties::ChangeOp(node, Int32Op(node));
        break;
      }
      case IrOpcode::kNumberShiftLeft: {
        VisitBinop(node, UseInfo::TruncatingWord32(),
                   UseInfo::TruncatingWord32(), kMachInt32);
        if (lower()) lowering->DoShift(node, lowering->machine()->Word32Shl());
        break;
      }
      case IrOpcode::kNumberShiftRight: {
        VisitBinop(node, UseInfo::TruncatingWord32(),
                   UseInfo::TruncatingWord32(), kMachInt32);
        if (lower()) lowering->DoShift(node, lowering->machine()->Word32Sar());
        break;
      }
      case IrOpcode::kNumberShiftRightLogical: {
        VisitBinop(node, UseInfo::TruncatingWord32(),
                   UseInfo::TruncatingWord32(), kMachUint32);
        if (lower()) lowering->DoShift(node, lowering->machine()->Word32Shr());
        break;
      }
      case IrOpcode::kNumberToInt32: {
        // Just change representation if necessary.
        VisitUnop(node, UseInfo::TruncatingWord32(), kMachInt32);
        if (lower()) DeferReplacement(node, node->InputAt(0));
        break;
      }
      case IrOpcode::kNumberToUint32: {
        // Just change representation if necessary.
        VisitUnop(node, UseInfo::TruncatingWord32(), kMachUint32);
        if (lower()) DeferReplacement(node, node->InputAt(0));
        break;
      }
      case IrOpcode::kNumberIsHoleNaN: {
        VisitUnop(node, UseInfo::Float64(), kMachBool);
        if (lower()) {
          // NumberIsHoleNaN(x) => Word32Equal(Float64ExtractLowWord32(x),
          //                                   #HoleNaNLower32)
          node->ReplaceInput(0,
                             jsgraph_->graph()->NewNode(
                                 lowering->machine()->Float64ExtractLowWord32(),
                                 node->InputAt(0)));
          node->AppendInput(jsgraph_->zone(),
                            jsgraph_->Int32Constant(kHoleNanLower32));
          NodeProperties::ChangeOp(node, jsgraph_->machine()->Word32Equal());
        }
        break;
      }
      case IrOpcode::kPlainPrimitiveToNumber: {
        VisitUnop(node, UseInfo::AnyTagged(), kTypeNumber | kRepTagged);
        if (lower()) {
          // PlainPrimitiveToNumber(x) => Call(ToNumberStub, x, no-context)
          Operator::Properties properties = node->op()->properties();
          Callable callable = CodeFactory::ToNumber(jsgraph_->isolate());
          CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
          CallDescriptor* desc = Linkage::GetStubCallDescriptor(
              jsgraph_->isolate(), jsgraph_->zone(), callable.descriptor(), 0,
              flags, properties);
          node->InsertInput(jsgraph_->zone(), 0,
                            jsgraph_->HeapConstant(callable.code()));
          node->AppendInput(jsgraph_->zone(), jsgraph_->NoContextConstant());
          NodeProperties::ChangeOp(node, jsgraph_->common()->Call(desc));
        }
        break;
      }
      case IrOpcode::kReferenceEqual: {
        VisitBinop(node, UseInfo::AnyTagged(), kMachBool);
        if (lower()) {
          NodeProperties::ChangeOp(node, lowering->machine()->WordEqual());
        }
        break;
      }
      case IrOpcode::kStringEqual: {
        VisitBinop(node, UseInfo::AnyTagged(), kMachBool);
        if (lower()) lowering->DoStringEqual(node);
        break;
      }
      case IrOpcode::kStringLessThan: {
        VisitBinop(node, UseInfo::AnyTagged(), kMachBool);
        if (lower()) lowering->DoStringLessThan(node);
        break;
      }
      case IrOpcode::kStringLessThanOrEqual: {
        VisitBinop(node, UseInfo::AnyTagged(), kMachBool);
        if (lower()) lowering->DoStringLessThanOrEqual(node);
        break;
      }
      case IrOpcode::kAllocate: {
        ProcessInput(node, 0, UseInfo::AnyTagged());
        ProcessRemainingInputs(node, 1);
        SetOutput(node, kMachAnyTagged);
        break;
      }
      case IrOpcode::kLoadField: {
        FieldAccess access = FieldAccessOf(node->op());
        ProcessInput(node, 0, UseInfoForBasePointer(access));
        ProcessRemainingInputs(node, 1);
        SetOutput(node, access.machine_type);
        break;
      }
      case IrOpcode::kStoreField: {
        FieldAccess access = FieldAccessOf(node->op());
        ProcessInput(node, 0, UseInfoForBasePointer(access));
        ProcessInput(node, 1, UseInfoFromMachineType(access.machine_type));
        ProcessRemainingInputs(node, 2);
        SetOutput(node, 0);
        break;
      }
      case IrOpcode::kLoadBuffer: {
        BufferAccess access = BufferAccessOf(node->op());
        ProcessInput(node, 0, UseInfo::PointerInt());        // buffer
        ProcessInput(node, 1, UseInfo::TruncatingWord32());  // offset
        ProcessInput(node, 2, UseInfo::TruncatingWord32());  // length
        ProcessRemainingInputs(node, 3);

        MachineType output_type;
        if (truncation.TruncatesUndefinedToZeroOrNaN()) {
          if (truncation.TruncatesNaNToZero()) {
            // If undefined is truncated to a non-NaN number, we can use
            // the load's representation.
            output_type = access.machine_type();
          } else {
            // If undefined is truncated to a number, but the use can
            // observe NaN, we need to output at least the float32
            // representation.
            if (access.machine_type() & kRepFloat32) {
              output_type = access.machine_type();
            } else {
              output_type = kMachFloat64;
            }
          }
        } else {
          // If undefined is not truncated away, we need to have the tagged
          // representation.
          output_type = kMachAnyTagged;
        }
        SetOutput(node, output_type);
        if (lower()) lowering->DoLoadBuffer(node, output_type, changer_);
        break;
      }
      case IrOpcode::kStoreBuffer: {
        BufferAccess access = BufferAccessOf(node->op());
        ProcessInput(node, 0, UseInfo::PointerInt());        // buffer
        ProcessInput(node, 1, UseInfo::TruncatingWord32());  // offset
        ProcessInput(node, 2, UseInfo::TruncatingWord32());  // length
        ProcessInput(node, 3,
                     UseInfoFromMachineType(access.machine_type()));  // value
        ProcessRemainingInputs(node, 4);
        SetOutput(node, 0);
        if (lower()) lowering->DoStoreBuffer(node);
        break;
      }
      case IrOpcode::kLoadElement: {
        ElementAccess access = ElementAccessOf(node->op());
        ProcessInput(node, 0, UseInfoForBasePointer(access));  // base
        ProcessInput(node, 1, UseInfo::TruncatingWord32());    // index
        ProcessRemainingInputs(node, 2);
        SetOutput(node, access.machine_type);
        break;
      }
      case IrOpcode::kStoreElement: {
        ElementAccess access = ElementAccessOf(node->op());
        ProcessInput(node, 0, UseInfoForBasePointer(access));  // base
        ProcessInput(node, 1, UseInfo::TruncatingWord32());    // index
        ProcessInput(node, 2,
                     UseInfoFromMachineType(access.machine_type));  // value
        ProcessRemainingInputs(node, 3);
        SetOutput(node, 0);
        break;
      }
      case IrOpcode::kObjectIsNumber: {
        ProcessInput(node, 0, UseInfo::AnyTagged());
        SetOutput(node, kMachBool);
        if (lower()) lowering->DoObjectIsNumber(node);
        break;
      }
      case IrOpcode::kObjectIsSmi: {
        ProcessInput(node, 0, UseInfo::AnyTagged());
        SetOutput(node, kMachBool);
        if (lower()) lowering->DoObjectIsSmi(node);
        break;
      }

      //------------------------------------------------------------------
      // Machine-level operators.
      //------------------------------------------------------------------
      case IrOpcode::kLoad: {
        // TODO(jarin) Eventually, we should get rid of all machine stores
        // from the high-level phases, then this becomes UNREACHABLE.
        LoadRepresentation rep = OpParameter<LoadRepresentation>(node);
        ProcessInput(node, 0, UseInfo::AnyTagged());   // tagged pointer
        ProcessInput(node, 1, UseInfo::PointerInt());  // index
        ProcessRemainingInputs(node, 2);
        SetOutput(node, rep);
        break;
      }
      case IrOpcode::kStore: {
        // TODO(jarin) Eventually, we should get rid of all machine stores
        // from the high-level phases, then this becomes UNREACHABLE.
        StoreRepresentation rep = OpParameter<StoreRepresentation>(node);
        ProcessInput(node, 0, UseInfo::AnyTagged());   // tagged pointer
        ProcessInput(node, 1, UseInfo::PointerInt());  // index
        ProcessInput(node, 2, UseInfoFromMachineType(rep.machine_type()));
        ProcessRemainingInputs(node, 3);
        SetOutput(node, 0);
        break;
      }
      case IrOpcode::kWord32Shr:
        // We output unsigned int32 for shift right because JavaScript.
        return VisitBinop(node, UseInfo::TruncatingWord32(), kMachUint32);
      case IrOpcode::kWord32And:
      case IrOpcode::kWord32Or:
      case IrOpcode::kWord32Xor:
      case IrOpcode::kWord32Shl:
      case IrOpcode::kWord32Sar:
        // We use signed int32 as the output type for these word32 operations,
        // though the machine bits are the same for either signed or unsigned,
        // because JavaScript considers the result from these operations signed.
        return VisitBinop(node, UseInfo::TruncatingWord32(),
                          kRepWord32 | kTypeInt32);
      case IrOpcode::kWord32Equal:
        return VisitBinop(node, UseInfo::TruncatingWord32(), kMachBool);

      case IrOpcode::kWord32Clz:
        return VisitUnop(node, UseInfo::TruncatingWord32(), kMachUint32);

      case IrOpcode::kInt32Add:
      case IrOpcode::kInt32Sub:
      case IrOpcode::kInt32Mul:
      case IrOpcode::kInt32MulHigh:
      case IrOpcode::kInt32Div:
      case IrOpcode::kInt32Mod:
        return VisitInt32Binop(node);
      case IrOpcode::kUint32Div:
      case IrOpcode::kUint32Mod:
      case IrOpcode::kUint32MulHigh:
        return VisitUint32Binop(node);
      case IrOpcode::kInt32LessThan:
      case IrOpcode::kInt32LessThanOrEqual:
        return VisitInt32Cmp(node);

      case IrOpcode::kUint32LessThan:
      case IrOpcode::kUint32LessThanOrEqual:
        return VisitUint32Cmp(node);

      case IrOpcode::kInt64Add:
      case IrOpcode::kInt64Sub:
      case IrOpcode::kInt64Mul:
      case IrOpcode::kInt64Div:
      case IrOpcode::kInt64Mod:
        return VisitInt64Binop(node);
      case IrOpcode::kInt64LessThan:
      case IrOpcode::kInt64LessThanOrEqual:
        return VisitInt64Cmp(node);

      case IrOpcode::kUint64LessThan:
        return VisitUint64Cmp(node);

      case IrOpcode::kUint64Div:
      case IrOpcode::kUint64Mod:
        return VisitUint64Binop(node);

      case IrOpcode::kWord64And:
      case IrOpcode::kWord64Or:
      case IrOpcode::kWord64Xor:
      case IrOpcode::kWord64Shl:
      case IrOpcode::kWord64Shr:
      case IrOpcode::kWord64Sar:
        return VisitBinop(node, UseInfo::TruncatingWord64(), kRepWord64);
      case IrOpcode::kWord64Equal:
        return VisitBinop(node, UseInfo::TruncatingWord64(), kMachBool);

      case IrOpcode::kChangeInt32ToInt64:
        return VisitUnop(node, UseInfo::TruncatingWord32(),
                         kTypeInt32 | kRepWord64);
      case IrOpcode::kChangeUint32ToUint64:
        return VisitUnop(node, UseInfo::TruncatingWord32(),
                         kTypeUint32 | kRepWord64);
      case IrOpcode::kTruncateFloat64ToFloat32:
        return VisitUnop(node, UseInfo::Float64(), kTypeNumber | kRepFloat32);
      case IrOpcode::kTruncateFloat64ToInt32:
        return VisitUnop(node, UseInfo::Float64(), kTypeInt32 | kRepWord32);
      case IrOpcode::kTruncateInt64ToInt32:
        // TODO(titzer): Is kTypeInt32 correct here?
        return VisitUnop(node, UseInfo::Word64TruncatingToWord32(),
                         kTypeInt32 | kRepWord32);

      case IrOpcode::kChangeFloat32ToFloat64:
        return VisitUnop(node, UseInfo::Float32(), kTypeNumber | kRepFloat64);
      case IrOpcode::kChangeInt32ToFloat64:
        return VisitUnop(node, UseInfo::TruncatingWord32(),
                         kTypeInt32 | kRepFloat64);
      case IrOpcode::kChangeUint32ToFloat64:
        return VisitUnop(node, UseInfo::TruncatingWord32(),
                         kTypeUint32 | kRepFloat64);
      case IrOpcode::kChangeFloat64ToInt32:
        return VisitUnop(node, UseInfo::Float64TruncatingToWord32(),
                         kTypeInt32 | kRepWord32);
      case IrOpcode::kChangeFloat64ToUint32:
        return VisitUnop(node, UseInfo::Float64TruncatingToWord32(),
                         kTypeUint32 | kRepWord32);

      case IrOpcode::kFloat64Add:
      case IrOpcode::kFloat64Sub:
      case IrOpcode::kFloat64Mul:
      case IrOpcode::kFloat64Div:
      case IrOpcode::kFloat64Mod:
      case IrOpcode::kFloat64Min:
        return VisitFloat64Binop(node);
      case IrOpcode::kFloat64Abs:
      case IrOpcode::kFloat64Sqrt:
      case IrOpcode::kFloat64RoundDown:
      case IrOpcode::kFloat64RoundTruncate:
      case IrOpcode::kFloat64RoundTiesAway:
        return VisitUnop(node, UseInfo::Float64(), kMachFloat64);
      case IrOpcode::kFloat64Equal:
      case IrOpcode::kFloat64LessThan:
      case IrOpcode::kFloat64LessThanOrEqual:
        return VisitFloat64Cmp(node);
      case IrOpcode::kFloat64ExtractLowWord32:
      case IrOpcode::kFloat64ExtractHighWord32:
        return VisitUnop(node, UseInfo::Float64(), kMachInt32);
      case IrOpcode::kFloat64InsertLowWord32:
      case IrOpcode::kFloat64InsertHighWord32:
        return VisitBinop(node, UseInfo::Float64(), UseInfo::TruncatingWord32(),
                          kMachFloat64);
      case IrOpcode::kLoadStackPointer:
      case IrOpcode::kLoadFramePointer:
        return VisitLeaf(node, kMachPtr);
      case IrOpcode::kStateValues:
        VisitStateValues(node);
        break;
      default:
        VisitInputs(node);
        break;
    }
  }

  void DeferReplacement(Node* node, Node* replacement) {
    TRACE("defer replacement #%d:%s with #%d:%s\n", node->id(),
          node->op()->mnemonic(), replacement->id(),
          replacement->op()->mnemonic());

    if (replacement->id() < count_ &&
        GetInfo(replacement)->output_type() == GetInfo(node)->output_type()) {
      // Replace with a previously existing node eagerly only if the type is the
      // same.
      node->ReplaceUses(replacement);
    } else {
      // Otherwise, we are replacing a node with a representation change.
      // Such a substitution must be done after all lowering is done, because
      // changing the type could confuse the representation change
      // insertion for uses of the node.
      replacements_.push_back(node);
      replacements_.push_back(replacement);
    }
    node->NullAllInputs();  // Node is now dead.
  }

  void PrintInfo(MachineTypeUnion info) {
    if (FLAG_trace_representation) {
      OFStream os(stdout);
      os << static_cast<MachineType>(info);
    }
  }

  void PrintTruncation(Truncation truncation) {
    if (FLAG_trace_representation) {
      OFStream os(stdout);
      os << truncation.description();
    }
  }

  void PrintUseInfo(UseInfo info) {
    if (FLAG_trace_representation) {
      OFStream os(stdout);
      os << info.preferred() << ":" << info.truncation().description();
    }
  }

 private:
  JSGraph* jsgraph_;
  size_t const count_;              // number of nodes in the graph
  ZoneVector<NodeInfo> info_;       // node id -> usage information
  NodeVector nodes_;                // collected nodes
  NodeVector replacements_;         // replacements to be done after lowering
  Phase phase_;                     // current phase of algorithm
  RepresentationChanger* changer_;  // for inserting representation changes
  ZoneQueue<Node*> queue_;          // queue for traversing the graph
  // TODO(danno): RepresentationSelector shouldn't know anything about the
  // source positions table, but must for now since there currently is no other
  // way to pass down source position information to nodes created during
  // lowering. Once this phase becomes a vanilla reducer, it should get source
  // position information via the SourcePositionWrapper like all other reducers.
  SourcePositionTable* source_positions_;
  Type* safe_int_additive_range_;

  NodeInfo* GetInfo(Node* node) {
    DCHECK(node->id() >= 0);
    DCHECK(node->id() < count_);
    return &info_[node->id()];
  }
};


SimplifiedLowering::SimplifiedLowering(JSGraph* jsgraph, Zone* zone,
                                       SourcePositionTable* source_positions)
    : jsgraph_(jsgraph),
      zone_(zone),
      zero_thirtyone_range_(Type::Range(0, 31, zone)),
      source_positions_(source_positions) {}


void SimplifiedLowering::LowerAllNodes() {
  RepresentationChanger changer(jsgraph(), jsgraph()->isolate());
  RepresentationSelector selector(jsgraph(), zone_, &changer,
                                  source_positions_);
  selector.Run(this);
}


void SimplifiedLowering::DoLoadBuffer(Node* node, MachineType output_type,
                                      RepresentationChanger* changer) {
  DCHECK_EQ(IrOpcode::kLoadBuffer, node->opcode());
  DCHECK_NE(kMachNone, RepresentationOf(output_type));
  MachineType const type = BufferAccessOf(node->op()).machine_type();
  if (output_type != type) {
    Node* const buffer = node->InputAt(0);
    Node* const offset = node->InputAt(1);
    Node* const length = node->InputAt(2);
    Node* const effect = node->InputAt(3);
    Node* const control = node->InputAt(4);
    Node* const index =
        machine()->Is64()
            ? graph()->NewNode(machine()->ChangeUint32ToUint64(), offset)
            : offset;

    Node* check = graph()->NewNode(machine()->Uint32LessThan(), offset, length);
    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

    Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
    Node* etrue =
        graph()->NewNode(machine()->Load(type), buffer, index, effect, if_true);
    Node* vtrue = changer->GetRepresentationFor(
        etrue, type, RepresentationOf(output_type), Truncation::None());

    Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
    Node* efalse = effect;
    Node* vfalse;
    if (output_type & kRepTagged) {
      vfalse = jsgraph()->UndefinedConstant();
    } else if (output_type & kRepFloat64) {
      vfalse =
          jsgraph()->Float64Constant(std::numeric_limits<double>::quiet_NaN());
    } else if (output_type & kRepFloat32) {
      vfalse =
          jsgraph()->Float32Constant(std::numeric_limits<float>::quiet_NaN());
    } else {
      vfalse = jsgraph()->Int32Constant(0);
    }

    Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
    Node* ephi = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, merge);

    // Replace effect uses of {node} with the {ephi}.
    NodeProperties::ReplaceUses(node, node, ephi);

    // Turn the {node} into a Phi.
    node->ReplaceInput(0, vtrue);
    node->ReplaceInput(1, vfalse);
    node->ReplaceInput(2, merge);
    node->TrimInputCount(3);
    NodeProperties::ChangeOp(node, common()->Phi(output_type, 2));
  } else {
    NodeProperties::ChangeOp(node, machine()->CheckedLoad(type));
  }
}


void SimplifiedLowering::DoStoreBuffer(Node* node) {
  DCHECK_EQ(IrOpcode::kStoreBuffer, node->opcode());
  MachineType const type = BufferAccessOf(node->op()).machine_type();
  NodeProperties::ChangeOp(node, machine()->CheckedStore(type));
}


void SimplifiedLowering::DoObjectIsNumber(Node* node) {
  Node* input = NodeProperties::GetValueInput(node, 0);
  // TODO(bmeurer): Optimize somewhat based on input type.
  Node* check =
      graph()->NewNode(machine()->WordEqual(),
                       graph()->NewNode(machine()->WordAnd(), input,
                                        jsgraph()->IntPtrConstant(kSmiTagMask)),
                       jsgraph()->IntPtrConstant(kSmiTag));
  Node* branch = graph()->NewNode(common()->Branch(), check, graph()->start());
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* vtrue = jsgraph()->Int32Constant(1);
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse = graph()->NewNode(
      machine()->WordEqual(),
      graph()->NewNode(
          machine()->Load(kMachAnyTagged), input,
          jsgraph()->IntPtrConstant(HeapObject::kMapOffset - kHeapObjectTag),
          graph()->start(), if_false),
      jsgraph()->HeapConstant(isolate()->factory()->heap_number_map()));
  Node* control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  node->ReplaceInput(0, vtrue);
  node->AppendInput(graph()->zone(), vfalse);
  node->AppendInput(graph()->zone(), control);
  NodeProperties::ChangeOp(node, common()->Phi(kMachBool, 2));
}


void SimplifiedLowering::DoObjectIsSmi(Node* node) {
  node->ReplaceInput(0,
                     graph()->NewNode(machine()->WordAnd(), node->InputAt(0),
                                      jsgraph()->IntPtrConstant(kSmiTagMask)));
  node->AppendInput(graph()->zone(), jsgraph()->IntPtrConstant(kSmiTag));
  NodeProperties::ChangeOp(node, machine()->WordEqual());
}


Node* SimplifiedLowering::StringComparison(Node* node) {
  Operator::Properties properties = node->op()->properties();
  Callable callable = CodeFactory::StringCompare(isolate());
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  CallDescriptor* desc = Linkage::GetStubCallDescriptor(
      isolate(), zone(), callable.descriptor(), 0, flags, properties);
  return graph()->NewNode(
      common()->Call(desc), jsgraph()->HeapConstant(callable.code()),
      NodeProperties::GetValueInput(node, 0),
      NodeProperties::GetValueInput(node, 1), jsgraph()->NoContextConstant(),
      NodeProperties::GetEffectInput(node),
      NodeProperties::GetControlInput(node));
}


Node* SimplifiedLowering::Int32Div(Node* const node) {
  Int32BinopMatcher m(node);
  Node* const zero = jsgraph()->Int32Constant(0);
  Node* const minus_one = jsgraph()->Int32Constant(-1);
  Node* const lhs = m.left().node();
  Node* const rhs = m.right().node();

  if (m.right().Is(-1)) {
    return graph()->NewNode(machine()->Int32Sub(), zero, lhs);
  } else if (m.right().Is(0)) {
    return rhs;
  } else if (machine()->Int32DivIsSafe() || m.right().HasValue()) {
    return graph()->NewNode(machine()->Int32Div(), lhs, rhs, graph()->start());
  }

  // General case for signed integer division.
  //
  //    if 0 < rhs then
  //      lhs / rhs
  //    else
  //      if rhs < -1 then
  //        lhs / rhs
  //      else if rhs == 0 then
  //        0
  //      else
  //        0 - lhs
  //
  // Note: We do not use the Diamond helper class here, because it really hurts
  // readability with nested diamonds.
  const Operator* const merge_op = common()->Merge(2);
  const Operator* const phi_op = common()->Phi(kMachInt32, 2);

  Node* check0 = graph()->NewNode(machine()->Int32LessThan(), zero, rhs);
  Node* branch0 = graph()->NewNode(common()->Branch(BranchHint::kTrue), check0,
                                   graph()->start());

  Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
  Node* true0 = graph()->NewNode(machine()->Int32Div(), lhs, rhs, if_true0);

  Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
  Node* false0;
  {
    Node* check1 = graph()->NewNode(machine()->Int32LessThan(), rhs, minus_one);
    Node* branch1 = graph()->NewNode(common()->Branch(), check1, if_false0);

    Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(machine()->Int32Div(), lhs, rhs, if_true1);

    Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
    Node* false1;
    {
      Node* check2 = graph()->NewNode(machine()->Word32Equal(), rhs, zero);
      Node* branch2 = graph()->NewNode(common()->Branch(), check2, if_false1);

      Node* if_true2 = graph()->NewNode(common()->IfTrue(), branch2);
      Node* true2 = zero;

      Node* if_false2 = graph()->NewNode(common()->IfFalse(), branch2);
      Node* false2 = graph()->NewNode(machine()->Int32Sub(), zero, lhs);

      if_false1 = graph()->NewNode(merge_op, if_true2, if_false2);
      false1 = graph()->NewNode(phi_op, true2, false2, if_false1);
    }

    if_false0 = graph()->NewNode(merge_op, if_true1, if_false1);
    false0 = graph()->NewNode(phi_op, true1, false1, if_false0);
  }

  Node* merge0 = graph()->NewNode(merge_op, if_true0, if_false0);
  return graph()->NewNode(phi_op, true0, false0, merge0);
}


Node* SimplifiedLowering::Int32Mod(Node* const node) {
  Int32BinopMatcher m(node);
  Node* const zero = jsgraph()->Int32Constant(0);
  Node* const minus_one = jsgraph()->Int32Constant(-1);
  Node* const lhs = m.left().node();
  Node* const rhs = m.right().node();

  if (m.right().Is(-1) || m.right().Is(0)) {
    return zero;
  } else if (m.right().HasValue()) {
    return graph()->NewNode(machine()->Int32Mod(), lhs, rhs, graph()->start());
  }

  // General case for signed integer modulus, with optimization for (unknown)
  // power of 2 right hand side.
  //
  //   if 0 < rhs then
  //     msk = rhs - 1
  //     if rhs & msk != 0 then
  //       lhs % rhs
  //     else
  //       if lhs < 0 then
  //         -(-lhs & msk)
  //       else
  //         lhs & msk
  //   else
  //     if rhs < -1 then
  //       lhs % rhs
  //     else
  //       zero
  //
  // Note: We do not use the Diamond helper class here, because it really hurts
  // readability with nested diamonds.
  const Operator* const merge_op = common()->Merge(2);
  const Operator* const phi_op = common()->Phi(kMachInt32, 2);

  Node* check0 = graph()->NewNode(machine()->Int32LessThan(), zero, rhs);
  Node* branch0 = graph()->NewNode(common()->Branch(BranchHint::kTrue), check0,
                                   graph()->start());

  Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
  Node* true0;
  {
    Node* msk = graph()->NewNode(machine()->Int32Add(), rhs, minus_one);

    Node* check1 = graph()->NewNode(machine()->Word32And(), rhs, msk);
    Node* branch1 = graph()->NewNode(common()->Branch(), check1, if_true0);

    Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(machine()->Int32Mod(), lhs, rhs, if_true1);

    Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
    Node* false1;
    {
      Node* check2 = graph()->NewNode(machine()->Int32LessThan(), lhs, zero);
      Node* branch2 = graph()->NewNode(common()->Branch(BranchHint::kFalse),
                                       check2, if_false1);

      Node* if_true2 = graph()->NewNode(common()->IfTrue(), branch2);
      Node* true2 = graph()->NewNode(
          machine()->Int32Sub(), zero,
          graph()->NewNode(machine()->Word32And(),
                           graph()->NewNode(machine()->Int32Sub(), zero, lhs),
                           msk));

      Node* if_false2 = graph()->NewNode(common()->IfFalse(), branch2);
      Node* false2 = graph()->NewNode(machine()->Word32And(), lhs, msk);

      if_false1 = graph()->NewNode(merge_op, if_true2, if_false2);
      false1 = graph()->NewNode(phi_op, true2, false2, if_false1);
    }

    if_true0 = graph()->NewNode(merge_op, if_true1, if_false1);
    true0 = graph()->NewNode(phi_op, true1, false1, if_true0);
  }

  Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
  Node* false0;
  {
    Node* check1 = graph()->NewNode(machine()->Int32LessThan(), rhs, minus_one);
    Node* branch1 = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                     check1, if_false0);

    Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(machine()->Int32Mod(), lhs, rhs, if_true1);

    Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
    Node* false1 = zero;

    if_false0 = graph()->NewNode(merge_op, if_true1, if_false1);
    false0 = graph()->NewNode(phi_op, true1, false1, if_false0);
  }

  Node* merge0 = graph()->NewNode(merge_op, if_true0, if_false0);
  return graph()->NewNode(phi_op, true0, false0, merge0);
}


Node* SimplifiedLowering::Uint32Div(Node* const node) {
  Uint32BinopMatcher m(node);
  Node* const zero = jsgraph()->Uint32Constant(0);
  Node* const lhs = m.left().node();
  Node* const rhs = m.right().node();

  if (m.right().Is(0)) {
    return zero;
  } else if (machine()->Uint32DivIsSafe() || m.right().HasValue()) {
    return graph()->NewNode(machine()->Uint32Div(), lhs, rhs, graph()->start());
  }

  Node* check = graph()->NewNode(machine()->Word32Equal(), rhs, zero);
  Diamond d(graph(), common(), check, BranchHint::kFalse);
  Node* div = graph()->NewNode(machine()->Uint32Div(), lhs, rhs, d.if_false);
  return d.Phi(kMachUint32, zero, div);
}


Node* SimplifiedLowering::Uint32Mod(Node* const node) {
  Uint32BinopMatcher m(node);
  Node* const minus_one = jsgraph()->Int32Constant(-1);
  Node* const zero = jsgraph()->Uint32Constant(0);
  Node* const lhs = m.left().node();
  Node* const rhs = m.right().node();

  if (m.right().Is(0)) {
    return zero;
  } else if (m.right().HasValue()) {
    return graph()->NewNode(machine()->Uint32Mod(), lhs, rhs, graph()->start());
  }

  // General case for unsigned integer modulus, with optimization for (unknown)
  // power of 2 right hand side.
  //
  //   if rhs then
  //     msk = rhs - 1
  //     if rhs & msk != 0 then
  //       lhs % rhs
  //     else
  //       lhs & msk
  //   else
  //     zero
  //
  // Note: We do not use the Diamond helper class here, because it really hurts
  // readability with nested diamonds.
  const Operator* const merge_op = common()->Merge(2);
  const Operator* const phi_op = common()->Phi(kMachInt32, 2);

  Node* branch0 = graph()->NewNode(common()->Branch(BranchHint::kTrue), rhs,
                                   graph()->start());

  Node* if_true0 = graph()->NewNode(common()->IfTrue(), branch0);
  Node* true0;
  {
    Node* msk = graph()->NewNode(machine()->Int32Add(), rhs, minus_one);

    Node* check1 = graph()->NewNode(machine()->Word32And(), rhs, msk);
    Node* branch1 = graph()->NewNode(common()->Branch(), check1, if_true0);

    Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
    Node* true1 = graph()->NewNode(machine()->Uint32Mod(), lhs, rhs, if_true1);

    Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
    Node* false1 = graph()->NewNode(machine()->Word32And(), lhs, msk);

    if_true0 = graph()->NewNode(merge_op, if_true1, if_false1);
    true0 = graph()->NewNode(phi_op, true1, false1, if_true0);
  }

  Node* if_false0 = graph()->NewNode(common()->IfFalse(), branch0);
  Node* false0 = zero;

  Node* merge0 = graph()->NewNode(merge_op, if_true0, if_false0);
  return graph()->NewNode(phi_op, true0, false0, merge0);
}


void SimplifiedLowering::DoShift(Node* node, Operator const* op) {
  Node* const rhs = NodeProperties::GetValueInput(node, 1);
  Type* const rhs_type = NodeProperties::GetType(rhs);
  if (!rhs_type->Is(zero_thirtyone_range_)) {
    node->ReplaceInput(1, graph()->NewNode(machine()->Word32And(), rhs,
                                           jsgraph()->Int32Constant(0x1f)));
  }
  NodeProperties::ChangeOp(node, op);
}


namespace {

void ReplaceEffectUses(Node* node, Node* replacement) {
  // Requires distinguishing between value and effect edges.
  DCHECK(replacement->op()->EffectOutputCount() > 0);
  for (Edge edge : node->use_edges()) {
    if (NodeProperties::IsEffectEdge(edge)) {
      edge.UpdateTo(replacement);
    } else {
      DCHECK(NodeProperties::IsValueEdge(edge));
    }
  }
}

}  // namespace


void SimplifiedLowering::DoStringEqual(Node* node) {
  Node* comparison = StringComparison(node);
  ReplaceEffectUses(node, comparison);
  node->ReplaceInput(0, comparison);
  node->ReplaceInput(1, jsgraph()->SmiConstant(EQUAL));
  node->TrimInputCount(2);
  NodeProperties::ChangeOp(node, machine()->WordEqual());
}


void SimplifiedLowering::DoStringLessThan(Node* node) {
  Node* comparison = StringComparison(node);
  ReplaceEffectUses(node, comparison);
  node->ReplaceInput(0, comparison);
  node->ReplaceInput(1, jsgraph()->SmiConstant(EQUAL));
  node->TrimInputCount(2);
  NodeProperties::ChangeOp(node, machine()->IntLessThan());
}


void SimplifiedLowering::DoStringLessThanOrEqual(Node* node) {
  Node* comparison = StringComparison(node);
  ReplaceEffectUses(node, comparison);
  node->ReplaceInput(0, comparison);
  node->ReplaceInput(1, jsgraph()->SmiConstant(EQUAL));
  node->TrimInputCount(2);
  NodeProperties::ChangeOp(node, machine()->IntLessThanOrEqual());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
