// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-inlining-into-js.h"

#include "src/compiler/wasm-compiler-definitions.h"
#include "src/compiler/wasm-compiler.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/decoder.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::compiler {

namespace {

using wasm::WasmOpcode;
using wasm::WasmOpcodes;

class WasmIntoJSInlinerImpl : private wasm::Decoder {
  using ValidationTag = NoValidationTag;

  struct Value {
    Node* node = nullptr;
    wasm::ValueType type = wasm::kWasmBottom;
  };

 public:
  WasmIntoJSInlinerImpl(Zone* zone, const wasm::WasmModule* module,
                        MachineGraph* mcgraph, const wasm::FunctionBody& body,
                        const base::Vector<const byte>& bytes)
      : wasm::Decoder(bytes.begin(), bytes.end()),
        module_(module),
        mcgraph_(mcgraph),
        body_(body),
        graph_(mcgraph->graph()),
        gasm_(mcgraph, zone) {
    // +1 for instance node.
    size_t params = body.sig->parameter_count() + 1;
    Node* start =
        graph_->NewNode(mcgraph->common()->Start(static_cast<int>(params)));
    graph_->SetStart(start);
    graph_->SetEnd(graph_->NewNode(mcgraph->common()->End(0)));
    gasm_.InitializeEffectControl(start, start);

    // Initialize parameter nodes.
    // We have to add another +1 as the minimum parameter index is actually
    // -1, not 0...
    size_t params_extended = params + 1;
    parameters_ = zone->NewArray<Node*>(params_extended);
    for (unsigned i = 0; i < params_extended; i++) {
      parameters_[i] = nullptr;
    }
    // Instance node at parameter 0.
    instance_node_ = Param(wasm::kWasmInstanceParameterIndex);
  }

  Node* Param(int index, const char* debug_name = nullptr) {
    DCHECK_NOT_NULL(graph_->start());
    // Turbofan allows negative parameter indices.
    static constexpr int kMinParameterIndex = -1;
    DCHECK_GE(index, kMinParameterIndex);
    int array_index = index - kMinParameterIndex;
    if (parameters_[array_index] == nullptr) {
      parameters_[array_index] = graph_->NewNode(
          mcgraph_->common()->Parameter(index, debug_name), graph_->start());
    }
    return parameters_[array_index];
  }

  bool TryInlining() {
    if (body_.sig->return_count() > 1) {
      return false;  // Multi-return is not supported.
    }
    // Parse locals.
    if (consume_u32v() != 0) {
      // Functions with locals are not supported.
      return false;
    }
    // Parse body.
    // TODO(mliedtke): Use zone vector?
    base::SmallVector<Value, 4> stack;
    while (is_inlineable_) {
      WasmOpcode opcode = ReadOpcode();
      switch (opcode) {
        case wasm::kExprExternInternalize:
          DCHECK(!stack.empty());
          stack.back() = ParseExternInternalize(stack.back());
          continue;
        case wasm::kExprRefCast:
          DCHECK(!stack.empty());
          stack.back() = ParseRefCast(stack.back());
          continue;
        case wasm::kExprStructGet:
          DCHECK(!stack.empty());
          stack.back() = ParseStructGet(stack.back());
          continue;
        case wasm::kExprLocalGet:
          stack.push_back(ParseLocalGet());
          continue;
        case wasm::kExprDrop:
          DCHECK(!stack.empty());
          stack.pop_back();
          continue;
        case wasm::kExprEnd: {
          DCHECK_LT(stack.size(), 2);
          int return_count = static_cast<int>(stack.size());
          base::SmallVector<Node*, 8> buf(return_count + 3);
          buf[0] = mcgraph_->Int32Constant(0);
          if (return_count) {
            buf[1] = stack.back().node;
          }
          buf[return_count + 1] = gasm_.effect();
          buf[return_count + 2] = gasm_.control();
          Node* ret = graph_->NewNode(mcgraph_->common()->Return(return_count),
                                      return_count + 3, buf.data());

          gasm_.MergeControlToEnd(ret);
          return true;
        }
        default:
          // Instruction not supported for inlining.
          return false;
      }
    }
    // The decoder found an instruction it couldn't inline successfully.
    return false;
  }

 private:
  Value ParseExternInternalize(Value input) {
    DCHECK(input.type.is_reference_to(wasm::HeapType::kExtern) ||
           input.type.is_reference_to(wasm::HeapType::kNoExtern));
    wasm::ValueType result_type = wasm::ValueType::RefMaybeNull(
        wasm::HeapType::kAny, input.type.is_nullable()
                                  ? wasm::Nullability::kNullable
                                  : wasm::Nullability::kNonNullable);
    Node* internalized = gasm_.WasmExternInternalize(input.node);
    return {internalized, result_type};
  }

  Value ParseLocalGet() {
    uint32_t index = consume_u32v();
    DCHECK_LT(index, body_.sig->parameter_count());
    return {Param(index + 1), body_.sig->GetParam(index)};
  }

  Value ParseStructGet(Value struct_val) {
    uint32_t struct_index = consume_u32v();
    DCHECK(module_->has_struct(struct_index));
    const wasm::StructType* struct_type = module_->struct_type(struct_index);
    uint32_t field_index = consume_u32v();
    DCHECK_GT(struct_type->field_count(), field_index);
    const bool is_signed = false;
    const bool null_check = true;
    Node* member = gasm_.StructGet(struct_val.node, struct_type, field_index,
                                   is_signed, null_check);
    return {member, struct_type->field(field_index)};
  }

  Value ParseRefCast(Value input) {
    auto [heap_index, length] = read_i33v<ValidationTag>(pc_);
    pc_ += length;
    if (heap_index < 0 ||
        module_->has_signature(static_cast<uint32_t>(heap_index))) {
      // Abstract and function casts are not supported.
      is_inlineable_ = false;
      return {};
    }
    wasm::ValueType target_type =
        wasm::ValueType::Ref(static_cast<uint32_t>(heap_index));
    Node* rtt = graph_->NewNode(
        gasm_.simplified()->RttCanon(target_type.ref_index()), instance_node_);
    Node* cast = gasm_.WasmTypeCast(input.node, rtt, {input.type, target_type});
    return {cast, target_type};
  }

  WasmOpcode ReadOpcode() {
    DCHECK_LT(pc_, end_);
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc_);
    if (!WasmOpcodes::IsPrefixOpcode(opcode)) {
      ++pc_;
      return opcode;
    }
    auto [opcode_with_prefix, length] =
        read_prefixed_opcode<ValidationTag>(pc_);
    pc_ += length;
    return opcode_with_prefix;
  }

  const wasm::WasmModule* module_;
  MachineGraph* mcgraph_;
  const wasm::FunctionBody& body_;
  Node** parameters_;
  Graph* graph_;
  Node* instance_node_;
  WasmGraphAssembler gasm_;
  bool is_inlineable_ = true;
};

}  // anonymous namespace

bool WasmIntoJSInliner::TryInlining(Zone* zone, const wasm::WasmModule* module,
                                    MachineGraph* mcgraph,
                                    const wasm::FunctionBody& body,
                                    const base::Vector<const byte>& bytes) {
  WasmIntoJSInlinerImpl inliner(zone, module, mcgraph, body, bytes);
  return inliner.TryInlining();
}

}  // namespace v8::internal::compiler
