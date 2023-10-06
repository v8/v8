// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-gc-type-reducer.h"

namespace v8::internal::compiler::turboshaft {

void WasmGCTypeAnalyzer::Run() {
  for (uint32_t block_index = 0; block_index < graph_.block_count();
       block_index++) {
    const Block& block = graph_.Get(BlockIndex{block_index});
    StartNewSnapshotFor(block);
    ProcessOperations(block);
    // Finish snapshot.
    block_to_snapshot_[block.index()] = MaybeSnapshot(types_table_.Seal());
  }
}

void WasmGCTypeAnalyzer::StartNewSnapshotFor(const Block& block) {
  // Start new snapshot based on predecessor information.
  if (block.HasPredecessors() == 0) {
    // The first block just starts with an empty snapshot.
    DCHECK_EQ(block.index().id(), 0);
    types_table_.StartNewSnapshot();
  } else if (block.IsLoop()) {
    // TODO(mliedtke): Once we want to propagate type information on LoopPhis,
    // we will need to revisit loops to also evaluate the backedge.
    Snapshot forward_edge_snap =
        block_to_snapshot_
            [block.LastPredecessor()->NeighboringPredecessor()->index()]
                .value();
    types_table_.StartNewSnapshot(forward_edge_snap);
  } else if (block.IsBranchTarget()) {
    DCHECK_EQ(block.PredecessorCount(), 1);
    types_table_.StartNewSnapshot(
        block_to_snapshot_[block.LastPredecessor()->index()].value());
    const BranchOp* branch =
        block.Predecessors()[0]->LastOperation(graph_).TryCast<BranchOp>();
    if (branch != nullptr) {
      ProcessBranchOnTarget(*branch, block);
    }
  } else {
    DCHECK_EQ(block.kind(), Block::Kind::kMerge);
    CreateMergeSnapshot(block);
  }
}

void WasmGCTypeAnalyzer::ProcessOperations(const Block& block) {
  for (OpIndex op_idx : graph_.OperationIndices(block)) {
    Operation& op = graph_.Get(op_idx);
    // TODO(mliedtke): We need a typeguard mechanism. Otherwise, how are we
    // going to figure out the type of an ArrayNew that already got lowered to
    // some __ Allocate?
    switch (op.opcode) {
      case Opcode::kWasmTypeCast:
        ProcessTypeCast(op.Cast<WasmTypeCastOp>());
        break;
      case Opcode::kWasmTypeCheck:
        ProcessTypeCheck(op.Cast<WasmTypeCheckOp>());
        break;
      case Opcode::kAssertNotNull:
        ProcessAssertNotNull(op.Cast<AssertNotNullOp>());
        break;
      case Opcode::kNull:
        ProcessNull(op.Cast<NullOp>());
        break;
      case Opcode::kIsNull:
        ProcessIsNull(op.Cast<IsNullOp>());
        break;
      case Opcode::kParameter:
        ProcessParameter(op.Cast<ParameterOp>());
        break;
      case Opcode::kStructGet:
        ProcessStructGet(op.Cast<StructGetOp>());
        break;
      case Opcode::kStructSet:
        ProcessStructSet(op.Cast<StructSetOp>());
        break;
      case Opcode::kArrayLength:
        ProcessArrayLength(op.Cast<ArrayLengthOp>());
        break;
      case Opcode::kGlobalGet:
        ProcessGlobalGet(op.Cast<GlobalGetOp>());
        break;
      case Opcode::kWasmRefFunc:
        ProcessRefFunc(op.Cast<WasmRefFuncOp>());
        break;
      case Opcode::kWasmAllocateArray:
        ProcessAllocateArray(op.Cast<WasmAllocateArrayOp>());
        break;
      case Opcode::kBranch:
        // Handling branch conditions implying special values is handled on the
        // beginning of the successor block.
      default:
        // TODO(mliedtke): Make sure that we handle all relevant operations
        // above.
        break;
    }
  }
}

void WasmGCTypeAnalyzer::ProcessTypeCast(const WasmTypeCastOp& type_cast) {
  OpIndex object = type_cast.object();
  wasm::ValueType target_type = type_cast.config.to;
  wasm::ValueType known_input_type = RefineTypeKnowledge(object, target_type);
  // The cast also returns the input itself, so we also need to update its
  // result type.
  RefineTypeKnowledge(graph_.Index(type_cast), target_type);
  input_type_map_[graph_.Index(type_cast)] = known_input_type;
}

void WasmGCTypeAnalyzer::ProcessTypeCheck(const WasmTypeCheckOp& type_check) {
  input_type_map_[graph_.Index(type_check)] =
      types_table_.Get(type_check.object());
}

void WasmGCTypeAnalyzer::ProcessAssertNotNull(
    const AssertNotNullOp& assert_not_null) {
  OpIndex object = assert_not_null.object();
  wasm::ValueType new_type = assert_not_null.type.AsNonNull();
  wasm::ValueType known_input_type = RefineTypeKnowledge(object, new_type);
  input_type_map_[graph_.Index(assert_not_null)] = known_input_type;
  // AssertNotNull also returns the input.
  RefineTypeKnowledge(graph_.Index(assert_not_null), new_type);
}

void WasmGCTypeAnalyzer::ProcessIsNull(const IsNullOp& is_null) {
  input_type_map_[graph_.Index(is_null)] = types_table_.Get(is_null.object());
}

void WasmGCTypeAnalyzer::ProcessParameter(const ParameterOp& parameter) {
  if (parameter.parameter_index != wasm::kWasmInstanceParameterIndex) {
    RefineTypeKnowledge(graph_.Index(parameter),
                        signature_->GetParam(parameter.parameter_index - 1));
  }
}

void WasmGCTypeAnalyzer::ProcessStructGet(const StructGetOp& struct_get) {
  // struct.get performs a null check.
  wasm::ValueType type = RefineTypeKnowledgeNotNull(struct_get.object());
  input_type_map_[graph_.Index(struct_get)] = type;
}

void WasmGCTypeAnalyzer::ProcessStructSet(const StructSetOp& struct_set) {
  // struct.set performs a null check.
  wasm::ValueType type = RefineTypeKnowledgeNotNull(struct_set.object());
  input_type_map_[graph_.Index(struct_set)] = type;
}

void WasmGCTypeAnalyzer::ProcessArrayLength(const ArrayLengthOp& array_length) {
  // array.len performs a null check.
  wasm::ValueType type = RefineTypeKnowledgeNotNull(array_length.array());
  input_type_map_[graph_.Index(array_length)] = type;
}

void WasmGCTypeAnalyzer::ProcessGlobalGet(const GlobalGetOp& global_get) {
  RefineTypeKnowledge(graph_.Index(global_get), global_get.global->type);
}

void WasmGCTypeAnalyzer::ProcessRefFunc(const WasmRefFuncOp& ref_func) {
  uint32_t sig_index = module_->functions[ref_func.function_index].sig_index;
  RefineTypeKnowledge(graph_.Index(ref_func), wasm::ValueType::Ref(sig_index));
}

void WasmGCTypeAnalyzer::ProcessAllocateArray(
    const WasmAllocateArrayOp& allocate_array) {
  uint32_t type_index =
      graph_.Get(allocate_array.rtt()).Cast<RttCanonOp>().type_index;
  RefineTypeKnowledge(graph_.Index(allocate_array),
                      wasm::ValueType::Ref(type_index));
}

void WasmGCTypeAnalyzer::ProcessBranchOnTarget(const BranchOp& branch,
                                               const Block& target) {
  const Operation& condition = graph_.Get(branch.condition());
  switch (condition.opcode) {
    case Opcode::kWasmTypeCheck: {
      if (branch.if_true == &target) {
        // It is known from now on that the type is at least the checked one.
        const WasmTypeCheckOp& check = condition.Cast<WasmTypeCheckOp>();
        wasm::ValueType known_input_type =
            RefineTypeKnowledge(check.object(), check.config.to);
        input_type_map_[branch.condition()] = known_input_type;
      }
    } break;
    case Opcode::kIsNull: {
      const IsNullOp& is_null = condition.Cast<IsNullOp>();
      if (branch.if_true == &target) {
        RefineTypeKnowledge(is_null.object(),
                            wasm::ToNullSentinel({is_null.type, module_}));
      } else {
        DCHECK_EQ(branch.if_false, &target);
        RefineTypeKnowledge(is_null.object(), is_null.type.AsNonNull());
      }
    } break;
    default:
      break;
  }
}

void WasmGCTypeAnalyzer::ProcessNull(const NullOp& null) {
  wasm::ValueType null_type = wasm::ToNullSentinel({null.type, module_});
  RefineTypeKnowledge(graph_.Index(null), null_type);
}

void WasmGCTypeAnalyzer::CreateMergeSnapshot(const Block& block) {
  DCHECK(!block_to_snapshot_[block.index()].has_value());
  base::SmallVector<Snapshot, 8> snapshots;
  NeighboringPredecessorIterable iterable = block.PredecessorsIterable();
  std::transform(iterable.begin(), iterable.end(),
                 std::back_insert_iterator(snapshots), [this](Block* pred) {
                   return block_to_snapshot_[pred->index()].value();
                 });
  types_table_.StartNewSnapshot(
      base::VectorOf(snapshots),
      [this](TypeSnapshotTable::Key,
             base::Vector<const wasm::ValueType> predecessors) {
        DCHECK_GT(predecessors.size(), 1);
        wasm::ValueType res = predecessors[0];
        if (res == wasm::ValueType()) return wasm::ValueType();
        for (auto iter = predecessors.begin() + 1; iter != predecessors.end();
             ++iter) {
          if (*iter == wasm::ValueType()) return wasm::ValueType();
          res = wasm::Union(res, *iter, module_, module_).type;
        }
        return res;
      });
}

wasm::ValueType WasmGCTypeAnalyzer::RefineTypeKnowledge(
    OpIndex object, wasm::ValueType new_type) {
  wasm::ValueType previous_value = types_table_.Get(object);
  wasm::ValueType intersection_type =
      previous_value == wasm::ValueType()
          ? new_type
          : wasm::Intersection(previous_value, new_type, module_, module_).type;
  types_table_.Set(object, intersection_type);
  return previous_value;
}

wasm::ValueType WasmGCTypeAnalyzer::RefineTypeKnowledgeNotNull(OpIndex object) {
  wasm::ValueType previous_value = types_table_.Get(object);
  types_table_.Set(object, previous_value.AsNonNull());
  return previous_value;
}

}  // namespace v8::internal::compiler::turboshaft
