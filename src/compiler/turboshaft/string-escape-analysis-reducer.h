// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STRING_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_STRING_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/compiler/escape-analysis-reducer.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

// StringEscapeAnalysisReducer tries to remove string concatenations whose
// results are unused, or used only in FrameStates or in other string concations
// that are themselves unused.
//
// The analysis (StringEscapeAnalyzer::Run) is pretty simple: we iterate the
// graph backwards and mark all inputs of all operations as "escaping", except
// for StringLength and FrameState which don't mark their input as escaping, and
// for StringConcat, which only marks its inputs as escaping if it is itself
// marked as escaping.

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class StringEscapeAnalyzer {
 public:
  StringEscapeAnalyzer(const Graph& graph, Zone* phase_zone)
      : graph_(graph),
        zone_(phase_zone),
        escaping_operations_(graph.op_id_count(), false, zone_, &graph) {}
  void Run();

  bool IsEscaping(OpIndex idx) const { return escaping_operations_[idx]; }

 private:
  const Graph& graph_;
  Zone* zone_;

  void ProcessBlock(const Block& block);
  void MarkAllInputsAsEscaping(const Operation& op);
  void RecursivelyMarkAllStringConcatInputsAsEscaping(
      const StringConcatOp* concat);
  void ReprocessStringConcats();

  // All operations in {escaping_operations_} are definitely escaping and cannot
  // be elided.
  FixedOpIndexSidetable<bool> escaping_operations_;
  // When we visit a StringConcat for the first time and it's not already in
  // {escaping_operations_}, we can't know for sure yet that it will never be
  // escaping, because of loop phis. So, we store it in
  // {maybe_non_escaping_string_concats_}, which we revisit after having visited
  // the whole graph, and only after this revisit do we know for sure that
  // StringConcat that are not in {escaping_operations_} do not indeed escape.
  std::vector<V<String>> maybe_non_escaping_string_concats_;

  uint32_t max_frame_state_input_count_ = 0;
};

template <class Next>
class StringEscapeAnalysisReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(StringEscapeAnalysis)

  // ElidedStringPart is an input of a StringConcat that is getting elided. It
  // could be either a regular String that appears in the output graph
  // (kNotElided), or another StringConcat that got elided as well (kElided).
  struct ElidedStringPart {
    enum class Kind : uint8_t { kNotElided, kElided };
    union {
      V<String> og_index;
      V<String> ig_index;
    } data;

    Kind kind;

    static ElidedStringPart Elided(V<String> ig_index) {
      return ElidedStringPart(Kind::kElided, ig_index);
    }
    static ElidedStringPart NotElided(V<String> og_index) {
      return ElidedStringPart(Kind::kNotElided, og_index);
    }

    bool is_elided() const { return kind == Kind::kElided; }

    V<String> og_index() const {
      DCHECK_EQ(kind, Kind::kNotElided);
      return data.og_index;
    }
    V<String> ig_index() const {
      DCHECK_EQ(kind, Kind::kElided);
      return data.ig_index;
    }

    bool operator==(const ElidedStringPart& other) const {
      if (kind != other.kind) return false;
      switch (kind) {
        case Kind::kElided:
          return ig_index() == other.ig_index();
        case Kind::kNotElided:
          return og_index() == other.og_index();
      }
    }

   private:
    ElidedStringPart(Kind kind, V<String> index) : data(index), kind(kind) {}
  };

  void Analyze() {
    if (v8_flags.turboshaft_string_concat_escape_analysis) {
      analyzer_.Run();
    }
    Next::Analyze();
  }

  V<String> REDUCE_INPUT_GRAPH(StringConcat)(V<String> ig_index,
                                             const StringConcatOp& op) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphStringConcat(ig_index, op);
    }
    if (!v8_flags.turboshaft_string_concat_escape_analysis) goto no_change;
    if (analyzer_.IsEscaping(ig_index)) goto no_change;

    // We're eliding this StringConcat.
    ElidedStringPart left = GetElidedStringInput(op.left());
    ElidedStringPart right = GetElidedStringInput(op.right());
    elided_strings_.insert({ig_index, std::pair{left, right}});
    return V<String>::Invalid();
  }

  V<FrameState> REDUCE_INPUT_GRAPH(FrameState)(
      V<FrameState> ig_index, const FrameStateOp& frame_state) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphFrameState(ig_index, frame_state);
    }
    if (!v8_flags.turboshaft_string_concat_escape_analysis) goto no_change;

    // Note that we recreate all FrameStates from sratch, regardless of whether
    // they have an elided StringConcat as input or not, because we need the
    // Deduplicator to be initialized, in case they are later used as parent to
    // a FrameState that has an elided StringConcat as input.
    // TODO(dmercadier): during the analysis, record which FrameStates have
    // elided StringConcat as input and also record their parents, so that we
    // don't need to recreate all FrameStates from scratch and to create
    // Deduplicator objects for each FrameState.
    return BuildFrameState(frame_state, ig_index);
  }

  V<Word32> REDUCE_INPUT_GRAPH(StringLength)(V<Word32> ig_index,
                                             const StringLengthOp& op) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphStringLength(ig_index, op);
    }
    if (!v8_flags.turboshaft_string_concat_escape_analysis) goto no_change;

    V<String> input_index = op.string();
    if (const StringConcatOp* input = __ input_graph()
                                          .Get(input_index)
                                          .template TryCast<StringConcatOp>();
        input && !analyzer_.IsEscaping(input_index)) {
      return __ UntagSmi(__ MapToNewGraph(input->length()));
    } else {
      goto no_change;
    }
  }

 private:
  class Deduplicator {
   public:
    explicit Deduplicator(Zone* zone)
        : object_ids_(zone), old_to_new_ids_(zone) {}

    struct DuplicatedId {
      uint32_t id;
      bool duplicated;
    };
    DuplicatedId GetDuplicatedIdForElidedString(ElidedStringPart index) {
      // TODO(dmercadier): do better than a linear search here.
      for (uint32_t id = 0; id < object_ids_.size(); id++) {
        if (object_ids_[id] == index) {
          return {id, true};
        }
      }
      object_ids_.push_back(index);
      return {next_id_++, false};
    }

    uint32_t RecordOldId(uint32_t old_id) {
      uint32_t new_id = next_id_++;
      if (old_id >= old_to_new_ids_.size()) {
        old_to_new_ids_.resize(old_id + 1, kUndefinedId);
      }
      old_to_new_ids_[old_id] = new_id;
      return new_id;
    }
    uint32_t GetNewDuplicatedIdForOldObject(uint32_t old_id) const {
      DCHECK_LT(old_id, old_to_new_ids_.size());
      DCHECK_NE(old_to_new_ids_[old_id], kUndefinedId);
      return old_to_new_ids_[old_id];
    }

    Deduplicator* clone(Zone* zone) const {
      return zone->New<Deduplicator>(object_ids_, next_id_, old_to_new_ids_);
    }

   private:
    Deduplicator(const ZoneVector<ElidedStringPart>& object_ids,
                 uint32_t next_id, const ZoneVector<uint32_t>& old_to_new_ids)
        : object_ids_(object_ids),
          next_id_(next_id),
          old_to_new_ids_(old_to_new_ids) {}

    // TODO(dmercadier): consider using a linked list for {object_ids_} so that
    // we don't ever need to clone it.
    ZoneVector<ElidedStringPart> object_ids_;
    uint32_t next_id_ = 0;

    static constexpr uint32_t kUndefinedId = -1;
    ZoneVector<uint32_t> old_to_new_ids_;

    friend class i::Zone;  // For access to private constructor.
  };

  V<FrameState> BuildFrameState(const FrameStateOp& input_frame_state,
                                OpIndex ig_index) {
    DCHECK(v8_flags.turboshaft_string_concat_escape_analysis);

    const FrameStateInfo& info = input_frame_state.data->frame_state_info;

    FrameStateData::Builder builder;
    auto it =
        input_frame_state.data->iterator(input_frame_state.state_values());

    Deduplicator* deduplicator;
    if (input_frame_state.inlined) {
      V<FrameState> parent_ig_index = input_frame_state.parent_frame_state();
      builder.AddParentFrameState(__ MapToNewGraph(parent_ig_index));

      // The parent FrameState could contain dematerialized objects, and the
      // current FrameState could reference those. Also, new IDs created for the
      // current FrameState should not conflict with IDs in the parent frame
      // state. Thus, we need to initialize the current Deduplicator with the
      // one from the parent FrameState.
      deduplicator = deduplicators_[parent_ig_index]->clone(__ phase_zone());
    } else {
      deduplicator =
          __ phase_zone() -> template New<Deduplicator>(__ phase_zone());
    }
    deduplicators_[ig_index] = deduplicator;

    // Closure
    BuildFrameStateInput(&builder, &it, deduplicator);

    // Parameters
    for (int i = 0; i < info.parameter_count(); i++) {
      BuildFrameStateInput(&builder, &it, deduplicator);
    }

    // Context
    BuildFrameStateInput(&builder, &it, deduplicator);

    // Registers/locals
    for (int i = 0; i < info.local_count(); i++) {
      BuildFrameStateInput(&builder, &it, deduplicator);
    }

    // Accumulator
    for (int i = 0; i < info.stack_count(); i++) {
      BuildFrameStateInput(&builder, &it, deduplicator);
    }

    return __ FrameState(builder.Inputs(), builder.inlined(),
                         builder.AllocateFrameStateData(info, __ graph_zone()));
  }

  void BuildFrameStateInput(FrameStateData::Builder* builder,
                            FrameStateData::Iterator* it,
                            Deduplicator* deduplicator) {
    switch (it->current_instr()) {
      using Instr = FrameStateData::Instr;
      case Instr::kInput: {
        MachineType type;
        OpIndex input;
        it->ConsumeInput(&type, &input);
        if (elided_strings_.contains(input)) {
          DCHECK(type.IsTagged());
          BuildMaybeElidedString(builder, ElidedStringPart::Elided(input),
                                 deduplicator);
        } else {
          builder->AddInput(type, __ MapToNewGraph(input));
        }
        break;
      }
      case Instr::kDematerializedObject: {
        uint32_t old_id;
        uint32_t field_count;
        it->ConsumeDematerializedObject(&old_id, &field_count);
        uint32_t new_id = deduplicator->RecordOldId(old_id);
        builder->AddDematerializedObject(new_id, field_count);
        for (uint32_t i = 0; i < field_count; ++i) {
          BuildFrameStateInput(builder, it, deduplicator);
        }
        break;
      }
      case Instr::kDematerializedObjectReference: {
        uint32_t old_id;
        it->ConsumeDematerializedObjectReference(&old_id);
        uint32_t new_id = deduplicator->GetNewDuplicatedIdForOldObject(old_id);
        builder->AddDematerializedObjectReference(new_id);
        break;
      }
      case Instr::kArgumentsElements: {
        CreateArgumentsType type;
        it->ConsumeArgumentsElements(&type);
        builder->AddArgumentsElements(type);
        break;
      }
      case Instr::kArgumentsLength:
        it->ConsumeArgumentsLength();
        builder->AddArgumentsLength();
        break;
      case Instr::kRestLength:
        it->ConsumeRestLength();
        builder->AddRestLength();
        break;
      case Instr::kUnusedRegister:
        it->ConsumeUnusedRegister();
        builder->AddUnusedRegister();
        break;
      case FrameStateData::Instr::kDematerializedStringConcat:
        // StringConcat should not have been escaped before this point.
        UNREACHABLE();
    }
  }

  void BuildMaybeElidedString(FrameStateData::Builder* builder,
                              ElidedStringPart maybe_elided,
                              Deduplicator* deduplicator) {
    if (maybe_elided.is_elided()) {
      typename Deduplicator::DuplicatedId dup_id =
          deduplicator->GetDuplicatedIdForElidedString(maybe_elided);
      if (dup_id.duplicated) {
        // For performance reasons, we de-duplicate repeated StringConcat inputs
        // in the FrameState. Unlike for elided objects, deduplication has no
        // impact on correctness.
        builder->AddDematerializedObjectReference(dup_id.id);
        return;
      }
      builder->AddDematerializedStringConcat(dup_id.id);
      std::pair<ElidedStringPart, ElidedStringPart> inputs =
          elided_strings_.at(maybe_elided.ig_index());
      BuildMaybeElidedString(builder, inputs.first, deduplicator);
      BuildMaybeElidedString(builder, inputs.second, deduplicator);
    } else {
      builder->AddInput(MachineType::AnyTagged(), maybe_elided.og_index());
    }
  }

  ElidedStringPart GetElidedStringInput(V<String> ig_index) {
    if (elided_strings_.contains(ig_index)) {
      return ElidedStringPart::Elided(ig_index);
    } else {
      return ElidedStringPart::NotElided(__ MapToNewGraph(ig_index));
    }
  }

  StringEscapeAnalyzer analyzer_{Asm().input_graph(), Asm().phase_zone()};
  // Map from input OpIndex of elided strings to the pair of output OpIndex
  // that are their left and right sides of the concatenation.
  ZoneAbslFlatHashMap<V<String>, std::pair<ElidedStringPart, ElidedStringPart>>
      elided_strings_{Asm().phase_zone()};

  // Mapping from input-graph FrameState to the corresponding deduplicator.
  SparseOpIndexSideTable<Deduplicator*> deduplicators_{Asm().phase_zone(),
                                                       &Asm().input_graph()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STRING_ESCAPE_ANALYSIS_REDUCER_H_
