// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-visualizer.h"

#include <sstream>
#include <string>

#include "src/compiler/generic-algorithm.h"
#include "src/compiler/generic-node.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/graph.h"
#include "src/compiler/graph-inl.h"
#include "src/compiler/node.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/register-allocator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/ostreams.h"

namespace v8 {
namespace internal {
namespace compiler {

#define DEAD_COLOR "#999999"

class Escaped {
 public:
  explicit Escaped(const std::ostringstream& os,
                   const char* escaped_chars = "<>|{}")
      : str_(os.str()), escaped_chars_(escaped_chars) {}

  friend std::ostream& operator<<(std::ostream& os, const Escaped& e) {
    for (std::string::const_iterator i = e.str_.begin(); i != e.str_.end();
         ++i) {
      if (e.needs_escape(*i)) os << "\\";
      os << *i;
    }
    return os;
  }

 private:
  bool needs_escape(char ch) const {
    for (size_t i = 0; i < strlen(escaped_chars_); ++i) {
      if (ch == escaped_chars_[i]) return true;
    }
    return false;
  }

  const std::string str_;
  const char* const escaped_chars_;
};

class JSONGraphNodeWriter : public NullNodeVisitor {
 public:
  JSONGraphNodeWriter(std::ostream& os, Zone* zone,
                      const Graph* graph)  // NOLINT
      : os_(os),
        graph_(graph),
        first_node_(true) {}

  void Print() { const_cast<Graph*>(graph_)->VisitNodeInputsFromEnd(this); }

  GenericGraphVisit::Control Pre(Node* node);

 private:
  std::ostream& os_;
  const Graph* const graph_;
  bool first_node_;

  DISALLOW_COPY_AND_ASSIGN(JSONGraphNodeWriter);
};


GenericGraphVisit::Control JSONGraphNodeWriter::Pre(Node* node) {
  if (first_node_) {
    first_node_ = false;
  } else {
    os_ << ",";
  }
  std::ostringstream label;
  label << *node->op();
  os_ << "{\"id\":" << node->id() << ",\"label\":\"" << Escaped(label, "\"")
      << "\"";
  IrOpcode::Value opcode = node->opcode();
  if (opcode == IrOpcode::kPhi || opcode == IrOpcode::kEffectPhi) {
    os_ << ",\"rankInputs\":[0," << NodeProperties::FirstControlIndex(node)
        << "]";
    os_ << ",\"rankWithInput\":[" << NodeProperties::FirstControlIndex(node)
        << "]";
  } else if (opcode == IrOpcode::kIfTrue || opcode == IrOpcode::kIfFalse ||
             opcode == IrOpcode::kLoop) {
    os_ << ",\"rankInputs\":[" << NodeProperties::FirstControlIndex(node)
        << "]";
  }
  if (opcode == IrOpcode::kBranch) {
    os_ << ",\"rankInputs\":[0]";
  }
  os_ << ",\"opcode\":\"" << IrOpcode::Mnemonic(node->opcode()) << "\"";
  os_ << ",\"control\":" << (NodeProperties::IsControl(node) ? "true"
                                                             : "false");
  os_ << "}";
  return GenericGraphVisit::CONTINUE;
}


class JSONGraphEdgeWriter : public NullNodeVisitor {
 public:
  JSONGraphEdgeWriter(std::ostream& os, Zone* zone,
                      const Graph* graph)  // NOLINT
      : os_(os),
        graph_(graph),
        first_edge_(true) {}

  void Print() { const_cast<Graph*>(graph_)->VisitNodeInputsFromEnd(this); }

  GenericGraphVisit::Control PreEdge(Node* from, int index, Node* to);

 private:
  std::ostream& os_;
  const Graph* const graph_;
  bool first_edge_;

  DISALLOW_COPY_AND_ASSIGN(JSONGraphEdgeWriter);
};


GenericGraphVisit::Control JSONGraphEdgeWriter::PreEdge(Node* from, int index,
                                                        Node* to) {
  if (first_edge_) {
    first_edge_ = false;
  } else {
    os_ << ",";
  }
  const char* edge_type = NULL;
  if (index < NodeProperties::FirstValueIndex(from)) {
    edge_type = "unknown";
  } else if (index < NodeProperties::FirstContextIndex(from)) {
    edge_type = "value";
  } else if (index < NodeProperties::FirstFrameStateIndex(from)) {
    edge_type = "context";
  } else if (index < NodeProperties::FirstEffectIndex(from)) {
    edge_type = "frame-state";
  } else if (index < NodeProperties::FirstControlIndex(from)) {
    edge_type = "effect";
  } else {
    edge_type = "control";
  }
  os_ << "{\"source\":" << to->id() << ",\"target\":" << from->id()
      << ",\"index\":" << index << ",\"type\":\"" << edge_type << "\"}";
  return GenericGraphVisit::CONTINUE;
}


std::ostream& operator<<(std::ostream& os, const AsJSON& ad) {
  Zone tmp_zone(ad.graph.zone()->isolate());
  os << "{\"nodes\":[";
  JSONGraphNodeWriter(os, &tmp_zone, &ad.graph).Print();
  os << "],\"edges\":[";
  JSONGraphEdgeWriter(os, &tmp_zone, &ad.graph).Print();
  os << "]}";
  return os;
}


class GraphVisualizer : public NullNodeVisitor {
 public:
  GraphVisualizer(std::ostream& os, Zone* zone, const Graph* graph);  // NOLINT

  void Print();

  GenericGraphVisit::Control Pre(Node* node);
  GenericGraphVisit::Control PreEdge(Node* from, int index, Node* to);

 private:
  void AnnotateNode(Node* node);
  void PrintEdge(Node::Edge edge);

  Zone* zone_;
  NodeSet all_nodes_;
  NodeSet white_nodes_;
  bool use_to_def_;
  std::ostream& os_;
  const Graph* const graph_;

  DISALLOW_COPY_AND_ASSIGN(GraphVisualizer);
};


static Node* GetControlCluster(Node* node) {
  if (OperatorProperties::IsBasicBlockBegin(node->op())) {
    return node;
  } else if (OperatorProperties::GetControlInputCount(node->op()) == 1) {
    Node* control = NodeProperties::GetControlInput(node, 0);
    return OperatorProperties::IsBasicBlockBegin(control->op()) ? control
                                                                : NULL;
  } else {
    return NULL;
  }
}


GenericGraphVisit::Control GraphVisualizer::Pre(Node* node) {
  if (all_nodes_.count(node) == 0) {
    Node* control_cluster = GetControlCluster(node);
    if (control_cluster != NULL) {
      os_ << "  subgraph cluster_BasicBlock" << control_cluster->id() << " {\n";
    }
    os_ << "  ID" << node->id() << " [\n";
    AnnotateNode(node);
    os_ << "  ]\n";
    if (control_cluster != NULL) os_ << "  }\n";
    all_nodes_.insert(node);
    if (use_to_def_) white_nodes_.insert(node);
  }
  return GenericGraphVisit::CONTINUE;
}


GenericGraphVisit::Control GraphVisualizer::PreEdge(Node* from, int index,
                                                    Node* to) {
  if (use_to_def_) return GenericGraphVisit::CONTINUE;
  // When going from def to use, only consider white -> other edges, which are
  // the dead nodes that use live nodes. We're probably not interested in
  // dead nodes that only use other dead nodes.
  if (white_nodes_.count(from) > 0) return GenericGraphVisit::CONTINUE;
  return GenericGraphVisit::SKIP;
}


static bool IsLikelyBackEdge(Node* from, int index, Node* to) {
  if (from->opcode() == IrOpcode::kPhi ||
      from->opcode() == IrOpcode::kEffectPhi) {
    Node* control = NodeProperties::GetControlInput(from, 0);
    return control->opcode() != IrOpcode::kMerge && control != to && index != 0;
  } else if (from->opcode() == IrOpcode::kLoop) {
    return index != 0;
  } else {
    return false;
  }
}


void GraphVisualizer::AnnotateNode(Node* node) {
  if (!use_to_def_) {
    os_ << "    style=\"filled\"\n"
        << "    fillcolor=\"" DEAD_COLOR "\"\n";
  }

  os_ << "    shape=\"record\"\n";
  switch (node->opcode()) {
    case IrOpcode::kEnd:
    case IrOpcode::kDead:
    case IrOpcode::kStart:
      os_ << "    style=\"diagonals\"\n";
      break;
    case IrOpcode::kMerge:
    case IrOpcode::kIfTrue:
    case IrOpcode::kIfFalse:
    case IrOpcode::kLoop:
      os_ << "    style=\"rounded\"\n";
      break;
    default:
      break;
  }

  std::ostringstream label;
  label << *node->op();
  os_ << "    label=\"{{#" << node->id() << ":" << Escaped(label);

  InputIter i = node->inputs().begin();
  for (int j = OperatorProperties::GetValueInputCount(node->op()); j > 0;
       ++i, j--) {
    os_ << "|<I" << i.index() << ">#" << (*i)->id();
  }
  for (int j = OperatorProperties::GetContextInputCount(node->op()); j > 0;
       ++i, j--) {
    os_ << "|<I" << i.index() << ">X #" << (*i)->id();
  }
  for (int j = OperatorProperties::GetFrameStateInputCount(node->op()); j > 0;
       ++i, j--) {
    os_ << "|<I" << i.index() << ">F #" << (*i)->id();
  }
  for (int j = OperatorProperties::GetEffectInputCount(node->op()); j > 0;
       ++i, j--) {
    os_ << "|<I" << i.index() << ">E #" << (*i)->id();
  }

  if (!use_to_def_ || OperatorProperties::IsBasicBlockBegin(node->op()) ||
      GetControlCluster(node) == NULL) {
    for (int j = OperatorProperties::GetControlInputCount(node->op()); j > 0;
         ++i, j--) {
      os_ << "|<I" << i.index() << ">C #" << (*i)->id();
    }
  }
  os_ << "}";

  if (FLAG_trace_turbo_types && NodeProperties::IsTyped(node)) {
    Bounds bounds = NodeProperties::GetBounds(node);
    std::ostringstream upper;
    bounds.upper->PrintTo(upper);
    std::ostringstream lower;
    bounds.lower->PrintTo(lower);
    os_ << "|" << Escaped(upper) << "|" << Escaped(lower);
  }
  os_ << "}\"\n";
}


void GraphVisualizer::PrintEdge(Node::Edge edge) {
  Node* from = edge.from();
  int index = edge.index();
  Node* to = edge.to();
  bool unconstrained = IsLikelyBackEdge(from, index, to);
  os_ << "  ID" << from->id();
  if (all_nodes_.count(to) == 0) {
    os_ << ":I" << index << ":n -> DEAD_INPUT";
  } else if (OperatorProperties::IsBasicBlockBegin(from->op()) ||
             GetControlCluster(from) == NULL ||
             (OperatorProperties::GetControlInputCount(from->op()) > 0 &&
              NodeProperties::GetControlInput(from) != to)) {
    os_ << ":I" << index << ":n -> ID" << to->id() << ":s"
        << "[" << (unconstrained ? "constraint=false, " : "")
        << (NodeProperties::IsControlEdge(edge) ? "style=bold, " : "")
        << (NodeProperties::IsEffectEdge(edge) ? "style=dotted, " : "")
        << (NodeProperties::IsContextEdge(edge) ? "style=dashed, " : "") << "]";
  } else {
    os_ << " -> ID" << to->id() << ":s [color=transparent, "
        << (unconstrained ? "constraint=false, " : "")
        << (NodeProperties::IsControlEdge(edge) ? "style=dashed, " : "") << "]";
  }
  os_ << "\n";
}


void GraphVisualizer::Print() {
  os_ << "digraph D {\n"
      << "  node [fontsize=8,height=0.25]\n"
      << "  rankdir=\"BT\"\n"
      << "  ranksep=\"1.2 equally\"\n"
      << "  overlap=\"false\"\n"
      << "  splines=\"true\"\n"
      << "  concentrate=\"true\"\n"
      << "  \n";

  // Make sure all nodes have been output before writing out the edges.
  use_to_def_ = true;
  // TODO(svenpanne) Remove the need for the const_casts.
  const_cast<Graph*>(graph_)->VisitNodeInputsFromEnd(this);
  white_nodes_.insert(const_cast<Graph*>(graph_)->start());

  // Visit all uses of white nodes.
  use_to_def_ = false;
  GenericGraphVisit::Visit<GraphVisualizer, NodeUseIterationTraits<Node> >(
      const_cast<Graph*>(graph_), zone_, white_nodes_.begin(),
      white_nodes_.end(), this);

  os_ << "  DEAD_INPUT [\n"
      << "    style=\"filled\" \n"
      << "    fillcolor=\"" DEAD_COLOR "\"\n"
      << "  ]\n"
      << "\n";

  // With all the nodes written, add the edges.
  for (NodeSetIter i = all_nodes_.begin(); i != all_nodes_.end(); ++i) {
    Node::Inputs inputs = (*i)->inputs();
    for (Node::Inputs::iterator iter(inputs.begin()); iter != inputs.end();
         ++iter) {
      PrintEdge(iter.edge());
    }
  }
  os_ << "}\n";
}


GraphVisualizer::GraphVisualizer(std::ostream& os, Zone* zone,
                                 const Graph* graph)  // NOLINT
    : zone_(zone),
      all_nodes_(NodeSet::key_compare(), NodeSet::allocator_type(zone)),
      white_nodes_(NodeSet::key_compare(), NodeSet::allocator_type(zone)),
      use_to_def_(true),
      os_(os),
      graph_(graph) {}


std::ostream& operator<<(std::ostream& os, const AsDOT& ad) {
  Zone tmp_zone(ad.graph.zone()->isolate());
  GraphVisualizer(os, &tmp_zone, &ad.graph).Print();
  return os;
}


class GraphC1Visualizer {
 public:
  GraphC1Visualizer(std::ostream& os, Zone* zone);  // NOLINT

  void PrintCompilation(const CompilationInfo* info);
  void PrintSchedule(const char* phase, const Schedule* schedule,
                     const SourcePositionTable* positions,
                     const InstructionSequence* instructions);
  void PrintAllocator(const char* phase, const RegisterAllocator* allocator);
  Zone* zone() const { return zone_; }

 private:
  void PrintIndent();
  void PrintStringProperty(const char* name, const char* value);
  void PrintLongProperty(const char* name, int64_t value);
  void PrintIntProperty(const char* name, int value);
  void PrintBlockProperty(const char* name, BasicBlock::Id block_id);
  void PrintNodeId(Node* n);
  void PrintNode(Node* n);
  void PrintInputs(Node* n);
  void PrintInputs(InputIter* i, int count, const char* prefix);
  void PrintType(Node* node);

  void PrintLiveRange(LiveRange* range, const char* type);
  class Tag FINAL BASE_EMBEDDED {
   public:
    Tag(GraphC1Visualizer* visualizer, const char* name) {
      name_ = name;
      visualizer_ = visualizer;
      visualizer->PrintIndent();
      visualizer_->os_ << "begin_" << name << "\n";
      visualizer->indent_++;
    }

    ~Tag() {
      visualizer_->indent_--;
      visualizer_->PrintIndent();
      visualizer_->os_ << "end_" << name_ << "\n";
      DCHECK(visualizer_->indent_ >= 0);
    }

   private:
    GraphC1Visualizer* visualizer_;
    const char* name_;
  };

  std::ostream& os_;
  int indent_;
  Zone* zone_;

  DISALLOW_COPY_AND_ASSIGN(GraphC1Visualizer);
};


void GraphC1Visualizer::PrintIndent() {
  for (int i = 0; i < indent_; i++) {
    os_ << "  ";
  }
}


GraphC1Visualizer::GraphC1Visualizer(std::ostream& os, Zone* zone)
    : os_(os), indent_(0), zone_(zone) {}


void GraphC1Visualizer::PrintStringProperty(const char* name,
                                            const char* value) {
  PrintIndent();
  os_ << name << " \"" << value << "\"\n";
}


void GraphC1Visualizer::PrintLongProperty(const char* name, int64_t value) {
  PrintIndent();
  os_ << name << " " << static_cast<int>(value / 1000) << "\n";
}


void GraphC1Visualizer::PrintBlockProperty(const char* name,
                                           BasicBlock::Id block_id) {
  PrintIndent();
  os_ << name << " \"B" << block_id << "\"\n";
}


void GraphC1Visualizer::PrintIntProperty(const char* name, int value) {
  PrintIndent();
  os_ << name << " " << value << "\n";
}


void GraphC1Visualizer::PrintCompilation(const CompilationInfo* info) {
  Tag tag(this, "compilation");
  if (info->IsOptimizing()) {
    Handle<String> name = info->function()->debug_name();
    PrintStringProperty("name", name->ToCString().get());
    PrintIndent();
    os_ << "method \"" << name->ToCString().get() << ":"
        << info->optimization_id() << "\"\n";
  } else {
    CodeStub::Major major_key = info->code_stub()->MajorKey();
    PrintStringProperty("name", CodeStub::MajorName(major_key, false));
    PrintStringProperty("method", "stub");
  }
  PrintLongProperty("date",
                    static_cast<int64_t>(base::OS::TimeCurrentMillis()));
}


void GraphC1Visualizer::PrintNodeId(Node* n) { os_ << "n" << n->id(); }


void GraphC1Visualizer::PrintNode(Node* n) {
  PrintNodeId(n);
  os_ << " " << *n->op() << " ";
  PrintInputs(n);
}


void GraphC1Visualizer::PrintInputs(InputIter* i, int count,
                                    const char* prefix) {
  if (count > 0) {
    os_ << prefix;
  }
  while (count > 0) {
    os_ << " ";
    PrintNodeId(**i);
    ++(*i);
    count--;
  }
}


void GraphC1Visualizer::PrintInputs(Node* node) {
  InputIter i = node->inputs().begin();
  PrintInputs(&i, OperatorProperties::GetValueInputCount(node->op()), " ");
  PrintInputs(&i, OperatorProperties::GetContextInputCount(node->op()),
              " Ctx:");
  PrintInputs(&i, OperatorProperties::GetFrameStateInputCount(node->op()),
              " FS:");
  PrintInputs(&i, OperatorProperties::GetEffectInputCount(node->op()), " Eff:");
  PrintInputs(&i, OperatorProperties::GetControlInputCount(node->op()),
              " Ctrl:");
}


void GraphC1Visualizer::PrintType(Node* node) {
  if (NodeProperties::IsTyped(node)) {
    Bounds bounds = NodeProperties::GetBounds(node);
    os_ << " type:";
    bounds.upper->PrintTo(os_);
    os_ << "..";
    bounds.lower->PrintTo(os_);
  }
}


void GraphC1Visualizer::PrintSchedule(const char* phase,
                                      const Schedule* schedule,
                                      const SourcePositionTable* positions,
                                      const InstructionSequence* instructions) {
  Tag tag(this, "cfg");
  PrintStringProperty("name", phase);
  const BasicBlockVector* rpo = schedule->rpo_order();
  for (size_t i = 0; i < rpo->size(); i++) {
    BasicBlock* current = (*rpo)[i];
    Tag block_tag(this, "block");
    PrintBlockProperty("name", current->id());
    PrintIntProperty("from_bci", -1);
    PrintIntProperty("to_bci", -1);

    PrintIndent();
    os_ << "predecessors";
    for (BasicBlock::Predecessors::iterator j = current->predecessors_begin();
         j != current->predecessors_end(); ++j) {
      os_ << " \"B" << (*j)->id() << "\"";
    }
    os_ << "\n";

    PrintIndent();
    os_ << "successors";
    for (BasicBlock::Successors::iterator j = current->successors_begin();
         j != current->successors_end(); ++j) {
      os_ << " \"B" << (*j)->id() << "\"";
    }
    os_ << "\n";

    PrintIndent();
    os_ << "xhandlers\n";

    PrintIndent();
    os_ << "flags\n";

    if (current->dominator() != NULL) {
      PrintBlockProperty("dominator", current->dominator()->id());
    }

    PrintIntProperty("loop_depth", current->loop_depth());

    if (instructions->code_start(current) >= 0) {
      int first_index = instructions->first_instruction_index(current);
      int last_index = instructions->last_instruction_index(current);
      PrintIntProperty("first_lir_id", LifetimePosition::FromInstructionIndex(
                                           first_index).Value());
      PrintIntProperty("last_lir_id", LifetimePosition::FromInstructionIndex(
                                          last_index).Value());
    }

    {
      Tag states_tag(this, "states");
      Tag locals_tag(this, "locals");
      int total = 0;
      for (BasicBlock::const_iterator i = current->begin(); i != current->end();
           ++i) {
        if ((*i)->opcode() == IrOpcode::kPhi) total++;
      }
      PrintIntProperty("size", total);
      PrintStringProperty("method", "None");
      int index = 0;
      for (BasicBlock::const_iterator i = current->begin(); i != current->end();
           ++i) {
        if ((*i)->opcode() != IrOpcode::kPhi) continue;
        PrintIndent();
        os_ << index << " ";
        PrintNodeId(*i);
        os_ << " [";
        PrintInputs(*i);
        os_ << "]\n";
        index++;
      }
    }

    {
      Tag HIR_tag(this, "HIR");
      for (BasicBlock::const_iterator i = current->begin(); i != current->end();
           ++i) {
        Node* node = *i;
        if (node->opcode() == IrOpcode::kPhi) continue;
        int uses = node->UseCount();
        PrintIndent();
        os_ << "0 " << uses << " ";
        PrintNode(node);
        if (FLAG_trace_turbo_types) {
          os_ << " ";
          PrintType(node);
        }
        if (positions != NULL) {
          SourcePosition position = positions->GetSourcePosition(node);
          if (!position.IsUnknown()) {
            DCHECK(!position.IsInvalid());
            os_ << " pos:" << position.raw();
          }
        }
        os_ << " <|@\n";
      }

      BasicBlock::Control control = current->control();
      if (control != BasicBlock::kNone) {
        PrintIndent();
        os_ << "0 0 ";
        if (current->control_input() != NULL) {
          PrintNode(current->control_input());
        } else {
          os_ << -1 - current->id().ToInt() << " Goto";
        }
        os_ << " ->";
        for (BasicBlock::Successors::iterator j = current->successors_begin();
             j != current->successors_end(); ++j) {
          os_ << " B" << (*j)->id();
        }
        if (FLAG_trace_turbo_types && current->control_input() != NULL) {
          os_ << " ";
          PrintType(current->control_input());
        }
        os_ << " <|@\n";
      }
    }

    if (instructions != NULL) {
      Tag LIR_tag(this, "LIR");
      for (int j = instructions->first_instruction_index(current);
           j <= instructions->last_instruction_index(current); j++) {
        PrintIndent();
        os_ << j << " " << *instructions->InstructionAt(j) << " <|@\n";
      }
    }
  }
}


void GraphC1Visualizer::PrintAllocator(const char* phase,
                                       const RegisterAllocator* allocator) {
  Tag tag(this, "intervals");
  PrintStringProperty("name", phase);

  const Vector<LiveRange*>* fixed_d = allocator->fixed_double_live_ranges();
  for (int i = 0; i < fixed_d->length(); ++i) {
    PrintLiveRange(fixed_d->at(i), "fixed");
  }

  const Vector<LiveRange*>* fixed = allocator->fixed_live_ranges();
  for (int i = 0; i < fixed->length(); ++i) {
    PrintLiveRange(fixed->at(i), "fixed");
  }

  const ZoneList<LiveRange*>* live_ranges = allocator->live_ranges();
  for (int i = 0; i < live_ranges->length(); ++i) {
    PrintLiveRange(live_ranges->at(i), "object");
  }
}


void GraphC1Visualizer::PrintLiveRange(LiveRange* range, const char* type) {
  if (range != NULL && !range->IsEmpty()) {
    PrintIndent();
    os_ << range->id() << " " << type;
    if (range->HasRegisterAssigned()) {
      InstructionOperand* op = range->CreateAssignedOperand(zone());
      int assigned_reg = op->index();
      if (op->IsDoubleRegister()) {
        os_ << " \"" << DoubleRegister::AllocationIndexToString(assigned_reg)
            << "\"";
      } else {
        DCHECK(op->IsRegister());
        os_ << " \"" << Register::AllocationIndexToString(assigned_reg) << "\"";
      }
    } else if (range->IsSpilled()) {
      InstructionOperand* op = range->TopLevel()->GetSpillOperand();
      if (op->IsDoubleStackSlot()) {
        os_ << " \"double_stack:" << op->index() << "\"";
      } else if (op->IsStackSlot()) {
        os_ << " \"stack:" << op->index() << "\"";
      } else {
        DCHECK(op->IsConstant());
        os_ << " \"const(nostack):" << op->index() << "\"";
      }
    }
    int parent_index = -1;
    if (range->IsChild()) {
      parent_index = range->parent()->id();
    } else {
      parent_index = range->id();
    }
    InstructionOperand* op = range->FirstHint();
    int hint_index = -1;
    if (op != NULL && op->IsUnallocated()) {
      hint_index = UnallocatedOperand::cast(op)->virtual_register();
    }
    os_ << " " << parent_index << " " << hint_index;
    UseInterval* cur_interval = range->first_interval();
    while (cur_interval != NULL && range->Covers(cur_interval->start())) {
      os_ << " [" << cur_interval->start().Value() << ", "
          << cur_interval->end().Value() << "[";
      cur_interval = cur_interval->next();
    }

    UsePosition* current_pos = range->first_pos();
    while (current_pos != NULL) {
      if (current_pos->RegisterIsBeneficial() || FLAG_trace_all_uses) {
        os_ << " " << current_pos->pos().Value() << " M";
      }
      current_pos = current_pos->next();
    }

    os_ << " \"\"\n";
  }
}


std::ostream& operator<<(std::ostream& os, const AsC1VCompilation& ac) {
  Zone tmp_zone(ac.info_->isolate());
  GraphC1Visualizer(os, &tmp_zone).PrintCompilation(ac.info_);
  return os;
}


std::ostream& operator<<(std::ostream& os, const AsC1V& ac) {
  Zone tmp_zone(ac.schedule_->zone()->isolate());
  GraphC1Visualizer(os, &tmp_zone)
      .PrintSchedule(ac.phase_, ac.schedule_, ac.positions_, ac.instructions_);
  return os;
}


std::ostream& operator<<(std::ostream& os, const AsC1VAllocator& ac) {
  Zone tmp_zone(ac.allocator_->code()->zone()->isolate());
  GraphC1Visualizer(os, &tmp_zone).PrintAllocator(ac.phase_, ac.allocator_);
  return os;
}
}
}
}  // namespace v8::internal::compiler
