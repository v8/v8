// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_DISASSEMBLER_IMPL_H_
#define V8_WASM_WASM_DISASSEMBLER_IMPL_H_

#include <iomanip>

#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

template <Decoder::ValidateFlag validate>
class ImmediatesPrinter;

using IndexAsComment = NamesProvider::IndexAsComment;

////////////////////////////////////////////////////////////////////////////////
// Configuration flags for aspects of behavior where we might want to change
// our minds. {true} is the legacy DevTools behavior.
constexpr IndexAsComment kIndicesAsComments = NamesProvider::kIndexAsComment;
constexpr bool kSkipDataSegmentNames = true;

////////////////////////////////////////////////////////////////////////////////
// Helpers.

class Indentation {
 public:
  Indentation(int current, int delta) : current_(current), delta_(delta) {
    DCHECK_GE(current, 0);
    DCHECK_GE(delta, 0);
  }

  Indentation Extra(int extra) { return {current_ + extra, delta_}; }

  void increase() { current_ += delta_; }
  void decrease() {
    DCHECK_GE(current_, delta_);
    current_ -= delta_;
  }
  int current() { return current_; }

 private:
  int current_;
  int delta_;
};

inline StringBuilder& operator<<(StringBuilder& sb, Indentation indentation) {
  char* ptr = sb.allocate(indentation.current());
  memset(ptr, ' ', indentation.current());
  return sb;
}

void PrintSignatureOneLine(
    StringBuilder& out, const FunctionSig* sig, uint32_t func_index,
    NamesProvider* names, bool param_names,
    IndexAsComment indices_as_comments = NamesProvider::kDontPrintIndex);

////////////////////////////////////////////////////////////////////////////////
// FunctionBodyDisassembler.

class FunctionBodyDisassembler : public WasmDecoder<Decoder::kFullValidation> {
 public:
  static constexpr Decoder::ValidateFlag validate = Decoder::kFullValidation;
  enum FunctionHeader : bool { kSkipHeader = false, kPrintHeader = true };

  FunctionBodyDisassembler(Zone* zone, const WasmModule* module,
                           uint32_t func_index, WasmFeatures* detected,
                           const FunctionSig* sig, const byte* start,
                           const byte* end, uint32_t offset,
                           NamesProvider* names)
      : WasmDecoder<validate>(zone, module, WasmFeatures::All(), detected, sig,
                              start, end, offset),
        func_index_(func_index),
        names_(names) {}

  void DecodeAsWat(MultiLineStringBuilder& out, Indentation indentation,
                   FunctionHeader include_header = kPrintHeader);

  void DecodeGlobalInitializer(StringBuilder& out);

  std::set<uint32_t>& used_types() { return used_types_; }

 protected:
  WasmOpcode GetOpcode();

  uint32_t PrintImmediatesAndGetLength(StringBuilder& out);

  void PrintHexNumber(StringBuilder& out, uint64_t number);

  LabelInfo& label_info(int depth) {
    return label_stack_[label_stack_.size() - 1 - depth];
  }

  friend class ImmediatesPrinter<validate>;
  uint32_t func_index_;
  WasmOpcode current_opcode_ = kExprUnreachable;
  NamesProvider* names_;
  std::set<uint32_t> used_types_;
  std::vector<LabelInfo> label_stack_;
  MultiLineStringBuilder* out_;
  // Labels use two different indexing systems: for looking them up in the
  // name section, they're indexed by order of occurrence; for generating names
  // like "$label0", the order in which they show up as targets of branch
  // instructions is used for generating consecutive names.
  // (This is legacy wasmparser behavior; we could change it.)
  uint32_t label_occurrence_index_ = 0;
  uint32_t label_generation_index_ = 0;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_DISASSEMBLER_IMPL_H_
