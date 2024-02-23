// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/use-map.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8::internal::compiler::turboshaft {

#define SIMD256_UNARY_OP(V)                                \
  V(S128Not, S256Not)                                      \
  V(I8x16Abs, I8x32Abs)                                    \
  V(I8x16Neg, I8x32Neg)                                    \
  V(I16x8ExtAddPairwiseI8x16S, I16x16ExtAddPairwiseI8x32S) \
  V(I16x8ExtAddPairwiseI8x16U, I16x16ExtAddPairwiseI8x32U) \
  V(I32x4ExtAddPairwiseI16x8S, I32x8ExtAddPairwiseI16x16S) \
  V(I32x4ExtAddPairwiseI16x8U, I32x8ExtAddPairwiseI16x16U) \
  V(I16x8Abs, I16x16Abs)                                   \
  V(I16x8Neg, I16x16Neg)                                   \
  V(I32x4Abs, I32x8Abs)                                    \
  V(I32x4Neg, I32x8Neg)                                    \
  V(F32x4Abs, F32x8Abs)                                    \
  V(F32x4Neg, F32x8Neg)                                    \
  V(F32x4Sqrt, F32x8Sqrt)                                  \
  V(F64x2Sqrt, F64x4Sqrt)                                  \
  V(I32x4UConvertF32x4, I32x8UConvertF32x8)                \
  V(F32x4UConvertI32x4, F32x8UConvertI32x8)

namespace {

Simd256UnaryOp::Kind GetSimd256UnaryKind(Simd128UnaryOp::Kind simd128_kind) {
  switch (simd128_kind) {
#define UNOP_KIND_MAPPING(from, to)   \
  case Simd128UnaryOp::Kind::k##from: \
    return Simd256UnaryOp::Kind::k##to;
    SIMD256_UNARY_OP(UNOP_KIND_MAPPING)
#undef UNOP_KIND_MAPPING
    default:
      UNIMPLEMENTED();
  }
}
}  // namespace

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class NodeGroup {
 public:
  // Current only support merge 2 Simd128 into Simd256
  static constexpr int kSize = kSimd256Size / kSimd128Size;
  NodeGroup(OpIndex a, OpIndex b) {
    indexes_[0] = a;
    indexes_[1] = b;
  }
  size_t size() const { return kSize; }
  OpIndex operator[](int i) const { return indexes_[i]; }

  bool operator==(const NodeGroup& other) const {
    return indexes_[0] == other.indexes_[0] && indexes_[1] == other.indexes_[1];
  }
  bool operator!=(const NodeGroup& other) const {
    return indexes_[0] != other.indexes_[0] || indexes_[1] != other.indexes_[1];
  }

  template <typename T>
  struct Iterator {
    T* p;
    T& operator*() { return *p; }
    bool operator!=(const Iterator& rhs) { return p != rhs.p; }
    void operator++() { ++p; }
  };

  auto begin() const { return Iterator<const OpIndex>{indexes_}; }
  auto end() const { return Iterator<const OpIndex>{indexes_ + kSize}; }

 private:
  OpIndex indexes_[kSize];
};

// A PackNode consists of a fixed number of isomorphic simd128 nodes which can
// execute in parallel and convert to a 256-bit simd node later. The nodes in a
// PackNode must satisfy that they can be scheduled in the same basic block and
// are mutually independent.
class PackNode : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  PackNode(const NodeGroup& node_group)
      : nodes_(node_group), revectorized_node_() {}
  NodeGroup Nodes() const { return nodes_; }
  bool IsSame(const NodeGroup& node_group) const {
    return nodes_ == node_group;
  }
  bool IsSame(const PackNode& other) const { return nodes_ == other.nodes_; }
  OpIndex RevectorizedNode() const { return revectorized_node_; }
  void SetRevectorizedNode(OpIndex node) { revectorized_node_ = node; }

  void Print(Graph* graph) const;

 private:
  NodeGroup nodes_;
  OpIndex revectorized_node_;
};

class SLPTree : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit SLPTree(Graph& graph, Zone* zone)
      : graph_(graph),
        phase_zone_(zone),
        root_(nullptr),
        node_to_packnode_(zone) {}

  PackNode* BuildTree(const NodeGroup& roots);
  void DeleteTree();

  PackNode* GetPackNode(OpIndex node);
  ZoneUnorderedMap<OpIndex, PackNode*>& GetNodeMapping() {
    return node_to_packnode_;
  }

  void Print(const char* info);

 private:
  // This is the recursive part of BuildTree.
  PackNode* BuildTreeRec(const NodeGroup& node_group, unsigned depth);

  // Baseline: create a new PackNode, and return.
  PackNode* NewPackNode(const NodeGroup& node_group);

  // Recursion: create a new PackNode and call BuildTreeRec recursively
  PackNode* NewPackNodeAndRecurs(const NodeGroup& node_group, int start_index,
                                 int count, unsigned depth);

  bool IsSideEffectFree(OpIndex first, OpIndex second);
  bool CanBePacked(const NodeGroup& node_group);

  Graph& graph() const { return graph_; }
  Zone* zone() const { return phase_zone_; }

  Graph& graph_;
  Zone* phase_zone_;
  PackNode* root_;
  // Maps a specific node to PackNode.
  ZoneUnorderedMap<OpIndex, PackNode*> node_to_packnode_;
  static constexpr size_t RecursionMaxDepth = 1000;
};

class WasmRevecAnalyzer {
 public:
  WasmRevecAnalyzer(Zone* zone, Graph& graph)
      : graph_(graph),
        phase_zone_(zone),
        store_seeds_(zone),
        slp_tree_(nullptr),
        revectorizable_node_(zone),
        should_reduce_(false),
        use_map_(nullptr) {
    Run();
  }

  void Run();

  bool CanMergeSLPTrees();
  bool ShouldReduce() const { return should_reduce_; }

  PackNode* GetPackNode(const OpIndex ig_index) {
    auto itr = revectorizable_node_.find(ig_index);
    if (itr != revectorizable_node_.end()) {
      return itr->second;
    }
    return nullptr;
  }

  const OpIndex GetReduced(const OpIndex node) {
    auto pnode = GetPackNode(node);
    if (!pnode) {
      return OpIndex::Invalid();
    }
    return pnode->RevectorizedNode();
  }

  const Operation& GetStartOperation(const PackNode* pnode, const OpIndex node,
                                     const Operation& op) {
    DCHECK(pnode);
    OpIndex start = pnode->Nodes()[0];
    if (start == node) return op;
    return graph_.Get(start);
  }

  base::Vector<const OpIndex> uses(OpIndex node) {
    return use_map_->uses(node);
  }

 private:
  void ProcessBlock(const Block& block);
  bool DecideVectorize();

  Graph& graph_;
  Zone* phase_zone_;
  ZoneVector<std::pair<const StoreOp*, const StoreOp*>> store_seeds_;
  const wasm::WasmModule* module_ = PipelineData::Get().wasm_module();
  const wasm::FunctionSig* signature_ = PipelineData::Get().wasm_sig();
  SLPTree* slp_tree_;
  ZoneUnorderedMap<OpIndex, PackNode*> revectorizable_node_;
  bool should_reduce_;
  SimdUseMap* use_map_;
};

template <class Next>
class WasmRevecReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmRevec)

  OpIndex GetExtractOpIfNeeded(PackNode* pnode, OpIndex ig_index,
                               OpIndex og_index) {
    uint8_t lane = 0;
    for (; lane < static_cast<uint8_t>(pnode->Nodes().size()); lane++) {
      if (pnode->Nodes()[lane] == ig_index) break;
    }

    for (auto use : analyzer_.uses(ig_index)) {
      if (!analyzer_.GetPackNode(use)) {
        OpIndex extract_128 = __ Simd256Extract128Lane(og_index, lane);
        return extract_128;
      }
    }

    return OpIndex::Invalid();
  }

  OpIndex REDUCE_INPUT_GRAPH(Load)(OpIndex ig_index, const LoadOp& load) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();

      // Emit revectorized op.
      if (!og_index.valid()) {
        const LoadOp* start = analyzer_.GetStartOperation(pnode, ig_index, load)
                                  .TryCast<LoadOp>();
        DCHECK_EQ(start->base(), load.base());

        auto base = __ MapToNewGraph(start->base());
        auto index = __ MapToNewGraph(start->index());
        og_index = __ Load(base, index, load.kind,
                           MemoryRepresentation::Simd256(), start->offset);
        pnode->SetRevectorizedNode(og_index);
      }

      // Emit extract op if needed.
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Next::ReduceInputGraphLoad(ig_index, load);
  }

  OpIndex REDUCE_INPUT_GRAPH(Store)(OpIndex ig_index, const StoreOp& store) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();

      // Emit revectorized op.
      if (!og_index.valid()) {
        const StoreOp* start =
            (analyzer_.GetStartOperation(pnode, ig_index, store))
                .TryCast<StoreOp>();
        DCHECK_EQ(start->base(), store.base());

        auto base = __ MapToNewGraph(start->base());
        auto index = __ MapToNewGraph(start->index());
        OpIndex value = analyzer_.GetReduced(start->value());
        DCHECK(value.valid());

        __ Store(base, index, value, store.kind,
                 MemoryRepresentation::Simd256(), store.write_barrier,
                 start->offset);

        // Set an arbitrary valid opindex here to skip reduce later.
        pnode->SetRevectorizedNode(ig_index);
      }

      // No extract op needed for Store.
      return OpIndex::Invalid();
    }

    // no_change
    return Next::ReduceInputGraphStore(ig_index, store);
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Unary)(OpIndex ig_index,
                                           const Simd128UnaryOp& unary) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        auto input = analyzer_.GetReduced(unary.input());
        og_index = __ Simd256Unary(V<Simd256>::Cast(input),
                                   GetSimd256UnaryKind(unary.kind));
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }
    return Next::ReduceInputGraphSimd128Unary(ig_index, unary);
  }

 private:
  const wasm::WasmModule* module_ = PipelineData::Get().wasm_module();
  WasmRevecAnalyzer analyzer_ = *PipelineData::Get().wasm_revec_analyzer();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_
