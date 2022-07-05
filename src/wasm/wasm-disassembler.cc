// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/conversions.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/wasm-disassembler-impl.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

////////////////////////////////////////////////////////////////////////////////
// Helpers.

static constexpr char kHexChars[] = "0123456789abcdef";
static constexpr char kUpperHexChars[] = "0123456789ABCDEF";

// Returns the log2 of the alignment, e.g. "4" means 2<<4 == 16 bytes.
// This is the same format as used in .wasm binary modules.
uint32_t GetDefaultAlignment(WasmOpcode opcode) {
  switch (opcode) {
    case kExprS128LoadMem:
    case kExprS128StoreMem:
      return 4;
    case kExprS128Load8x8S:
    case kExprS128Load8x8U:
    case kExprS128Load16x4S:
    case kExprS128Load16x4U:
    case kExprS128Load32x2S:
    case kExprS128Load32x2U:
    case kExprS128Load64Splat:
    case kExprS128Load64Zero:
    case kExprS128Load64Lane:
    case kExprS128Store64Lane:
      return 3;
    case kExprS128Load32Splat:
    case kExprS128Load32Zero:
    case kExprS128Load32Lane:
    case kExprS128Store32Lane:
      return 2;
    case kExprS128Load16Splat:
    case kExprS128Load16Lane:
    case kExprS128Store16Lane:
      return 1;
    case kExprS128Load8Splat:
    case kExprS128Load8Lane:
    case kExprS128Store8Lane:
      return 0;

#define CASE(Opcode, ...) \
  case kExpr##Opcode:     \
    return GetLoadType(kExpr##Opcode).size_log_2();
      FOREACH_LOAD_MEM_OPCODE(CASE)
#undef CASE
#define CASE(Opcode, ...) \
  case kExpr##Opcode:     \
    return GetStoreType(kExpr##Opcode).size_log_2();
      FOREACH_STORE_MEM_OPCODE(CASE)
#undef CASE

#define CASE(Opcode, Type) \
  case kExpr##Opcode:      \
    return ElementSizeLog2Of(MachineType::Type().representation());
      ATOMIC_OP_LIST(CASE)
      ATOMIC_STORE_OP_LIST(CASE)
#undef CASE

    default:
      UNREACHABLE();
  }
}

StringBuilder& operator<<(StringBuilder& sb, uint64_t n) {
  if (n == 0) {
    *sb.allocate(1) = '0';
    return sb;
  }
  static constexpr size_t kBufferSize = 20;  // Just enough for a uint64.
  char buffer[kBufferSize];
  char* end = buffer + kBufferSize;
  char* out = end;
  while (n != 0) {
    *(--out) = '0' + (n % 10);
    n /= 10;
  }
  sb.write(out, static_cast<size_t>(end - out));
  return sb;
}

void PrintSignatureOneLine(StringBuilder& out, const FunctionSig* sig,
                           uint32_t func_index, NamesProvider* names,
                           bool param_names,
                           IndexAsComment indices_as_comments) {
  if (param_names) {
    for (uint32_t i = 0; i < sig->parameter_count(); i++) {
      out << " (param ";
      names->PrintLocalName(out, func_index, i, indices_as_comments);
      out << ' ';
      names->PrintValueType(out, sig->GetParam(i));
      out << ")";
    }
  } else if (sig->parameter_count() > 0) {
    out << " (param";
    for (uint32_t i = 0; i < sig->parameter_count(); i++) {
      out << " ";
      names->PrintValueType(out, sig->GetParam(i));
    }
    out << ")";
  }
  for (size_t i = 0; i < sig->return_count(); i++) {
    out << " (result ";
    names->PrintValueType(out, sig->GetReturn(i));
    out << ")";
  }
}

////////////////////////////////////////////////////////////////////////////////
// FunctionBodyDisassembler.

void FunctionBodyDisassembler::DecodeAsWat(MultiLineStringBuilder& out,
                                           Indentation indentation,
                                           FunctionHeader include_header) {
  out_ = &out;
  int base_indentation = indentation.current();
  // Print header.
  if (include_header == kPrintHeader) {
    out << indentation << "(func ";
    names_->PrintFunctionName(out, func_index_, NamesProvider::kDevTools);
    PrintSignatureOneLine(out, sig_, func_index_, names_, true,
                          kIndicesAsComments);
    out.NextLine(pc_offset());
  } else {
    out.set_current_line_bytecode_offset(pc_offset());
  }
  indentation.increase();

  // Decode and print locals.
  uint32_t locals_length;
  InitializeLocalsFromSig();
  DecodeLocals(pc_, &locals_length, static_cast<uint32_t>(num_locals()));
  if (failed()) {
    // TODO(jkummerow): Improve error handling.
    out << "Failed to decode locals\n";
    return;
  }
  for (uint32_t i = static_cast<uint32_t>(sig_->parameter_count());
       i < num_locals_; i++) {
    out << indentation << "(local ";
    names_->PrintLocalName(out, func_index_, i);
    out << " ";
    names_->PrintValueType(out, local_type(i));
    out << ")";
    out.NextLine(pc_offset());
  }
  consume_bytes(locals_length);

  // Main loop.
  while (pc_ < end_) {
    WasmOpcode opcode = GetOpcode();
    current_opcode_ = opcode;  // Some immediates need to know this.

    uint32_t length;
    // Deal with indentation.
    if (opcode == kExprEnd || opcode == kExprElse || opcode == kExprCatch ||
        opcode == kExprCatchAll || opcode == kExprDelegate) {
      indentation.decrease();
    }
    out << indentation;
    if (opcode == kExprElse || opcode == kExprCatch ||
        opcode == kExprCatchAll || opcode == kExprBlock || opcode == kExprIf ||
        opcode == kExprLoop || opcode == kExprTry) {
      indentation.increase();
    }

    // Print the opcode and its immediates.
    if (opcode == kExprEnd) {
      if (indentation.current() == base_indentation) {
        out << ")";  // End of the function.
      } else {
        out << "end";
        const LabelInfo& label = label_stack_.back();
        if (label.start != nullptr) {
          out << " ";
          out.write(label.start, label.length);
        }
        label_stack_.pop_back();
      }
    } else {
      out << WasmOpcodes::OpcodeName(opcode);
    }
    if (opcode == kExprBlock || opcode == kExprIf || opcode == kExprLoop ||
        opcode == kExprTry) {
      label_stack_.emplace_back(out.line_number(), out.length(),
                                label_occurrence_index_++);
    }
    length = PrintImmediatesAndGetLength(out);

    out.NextLine(pc_offset());
    pc_ += length;
  }

  if (pc_ != end_) {
    // TODO(jkummerow): Improve error handling.
    out << "Beyond end of code";
  }
}

void FunctionBodyDisassembler::DecodeGlobalInitializer(StringBuilder& out) {
  while (pc_ < end_) {
    WasmOpcode opcode = GetOpcode();
    current_opcode_ = opcode;  // Some immediates need to know this.
    // Don't print the final "end".
    if (opcode == kExprEnd && pc_ + 1 == end_) break;
    uint32_t length;
    out << " (" << WasmOpcodes::OpcodeName(opcode);
    length = PrintImmediatesAndGetLength(out);
    out << ")";
    pc_ += length;
  }
}

WasmOpcode FunctionBodyDisassembler::GetOpcode() {
  WasmOpcode opcode = static_cast<WasmOpcode>(*pc_);
  if (!WasmOpcodes::IsPrefixOpcode(opcode)) return opcode;
  uint32_t opcode_length = 1;
  if (opcode == kGCPrefix) {
    return read_two_byte_opcode<validate>(pc_, &opcode_length);
  }
  return read_prefixed_opcode<validate>(pc_, &opcode_length);
}

void FunctionBodyDisassembler::PrintHexNumber(StringBuilder& out,
                                              uint64_t number) {
  constexpr size_t kBufferSize = sizeof(number) * 2 + 2;  // +2 for "0x".
  char buffer[kBufferSize];
  char* end = buffer + kBufferSize;
  char* ptr = end;
  do {
    *(--ptr) = kHexChars[number & 0xF];
    number >>= 4;
  } while (number > 0);
  *(--ptr) = 'x';
  *(--ptr) = '0';
  size_t length = static_cast<size_t>(end - ptr);
  char* output = out.allocate(length);
  memcpy(output, ptr, length);
}

////////////////////////////////////////////////////////////////////////////////
// ImmediatesPrinter.

template <Decoder::ValidateFlag validate>
class ImmediatesPrinter {
 public:
  ImmediatesPrinter(StringBuilder& out, FunctionBodyDisassembler* owner)
      : out_(out), owner_(owner) {}

  void PrintDepthAsLabel(int imm_depth) {
    out_ << " ";
    const char* label_start = out_.cursor();
    int depth = imm_depth;
    if (owner_->current_opcode_ == kExprDelegate) depth++;
    // Be robust: if the module is invalid, print what we got.
    if (depth >= static_cast<int>(owner_->label_stack_.size())) {
      out_ << imm_depth;
      return;
    }
    // If the label's name has already been determined and backpatched, just
    // copy it here.
    LabelInfo& label_info = owner_->label_info(depth);
    if (label_info.start != nullptr) {
      out_.write(label_info.start, label_info.length);
      return;
    }
    // Determine the label's name and backpatch the line that opened the block.
    names()->PrintLabelName(out_, owner_->func_index_,
                            label_info.name_section_index,
                            owner_->label_generation_index_++);
    label_info.length = static_cast<size_t>(out_.cursor() - label_start);
    owner_->out_->PatchLabel(label_info, label_start);
  }

  void BlockType(BlockTypeImmediate<validate>& imm) {
    if (imm.type == kWasmBottom) {
      const FunctionSig* sig = owner_->module_->signature(imm.sig_index);
      PrintSignatureOneLine(out_, sig, 0 /* ignored */, names(), false);
    } else if (imm.type == kWasmVoid) {
      // Just be silent.
    } else {
      out_ << " (result ";
      names()->PrintValueType(out_, imm.type);
      out_ << ")";
    }
  }

  void HeapType(HeapTypeImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintHeapType(out_, imm.type);
    if (imm.type.is_index()) use_type(imm.type.ref_index());
  }

  void BranchDepth(BranchDepthImmediate<validate>& imm) {
    PrintDepthAsLabel(imm.depth);
  }

  void BranchTable(BranchTableImmediate<validate>& imm) {
    const byte* pc = imm.table;
    for (uint32_t i = 0; i <= imm.table_count; i++) {
      uint32_t length;
      uint32_t target = owner_->read_u32v<validate>(pc, &length);
      PrintDepthAsLabel(target);
      pc += length;
    }
  }

  void CallIndirect(CallIndirectImmediate<validate>& imm) {
    const FunctionSig* sig = owner_->module_->signature(imm.sig_imm.index);
    PrintSignatureOneLine(out_, sig, 0 /* ignored */, names(), false);
    if (imm.table_imm.index != 0) TableIndex(imm.table_imm);
  }

  void SelectType(SelectTypeImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintValueType(out_, imm.type);
  }

  void MemoryAccess(MemoryAccessImmediate<validate>& imm) {
    if (imm.offset != 0) out_ << " offset=" << imm.offset;
    if (imm.alignment != GetDefaultAlignment(owner_->current_opcode_)) {
      out_ << " align=" << (1u << imm.alignment);
    }
  }

  void SimdLane(SimdLaneImmediate<validate>& imm) {
    out_ << " " << uint32_t{imm.lane};
  }

  void Field(FieldImmediate<validate>& imm) {
    TypeIndex(imm.struct_imm);
    out_ << " ";
    names()->PrintFieldName(out_, imm.struct_imm.index, imm.field_imm.index);
  }

  void Length(IndexImmediate<validate>& imm) {
    out_ << " " << imm.index;  // --
  }

  void Wtf8Policy(Wtf8PolicyImmediate<validate>& imm) {
    out_ << (imm.value == kWtf8PolicyReject    ? " reject"
             : imm.value == kWtf8PolicyAccept  ? " accept"
             : imm.value == kWtf8PolicyReplace ? " replace"
                                               : " unknown-policy");
  }

  void TagIndex(TagIndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintTagName(out_, imm.index);
  }

  void FunctionIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintFunctionName(out_, imm.index, NamesProvider::kDevTools);
  }

  void TypeIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintTypeName(out_, imm.index);
    use_type(imm.index);
  }

  void LocalIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintLocalName(out_, func_index(), imm.index);
  }

  void GlobalIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintGlobalName(out_, imm.index);
  }

  void TableIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintTableName(out_, imm.index);
  }

  void MemoryIndex(MemoryIndexImmediate<validate>& imm) {
    if (imm.index == 0) return;
    out_ << " " << imm.index;
  }

  void DataSegmentIndex(IndexImmediate<validate>& imm) {
    if (kSkipDataSegmentNames) {
      out_ << " " << imm.index;
    } else {
      out_ << " ";
      names()->PrintDataSegmentName(out_, imm.index);
    }
  }

  void ElemSegmentIndex(IndexImmediate<validate>& imm) {
    out_ << " ";
    names()->PrintElementSegmentName(out_, imm.index);
  }

  void I32Const(ImmI32Immediate<validate>& imm) {
    out_ << " " << imm.value;  // --
  }

  void I64Const(ImmI64Immediate<validate>& imm) {
    if (imm.value >= 0) {
      out_ << " " << static_cast<uint64_t>(imm.value);
    } else {
      out_ << " -" << ((~static_cast<uint64_t>(imm.value)) + 1);
    }
  }

  void F32Const(ImmF32Immediate<validate>& imm) {
    float f = imm.value;
    if (f == 0) {
      out_ << (1 / f < 0 ? " -0.0" : " 0.0");
    } else if (std::isnan(f)) {
      uint32_t bits = base::bit_cast<uint32_t>(f);
      uint32_t payload = bits & 0x7F'FFFFu;
      uint32_t signbit = bits >> 31;
      if (payload == 0x40'0000u) {
        out_ << (signbit == 1 ? " -nan" : " nan");
      } else {
        out_ << (signbit == 1 ? " -nan:" : " +nan:");
        owner_->PrintHexNumber(out_, payload);
      }
    } else {
      std::ostringstream o;
      o << std::setprecision(std::numeric_limits<float>::digits10 + 1) << f;
      out_ << " " << o.str();
    }
  }

  void F64Const(ImmF64Immediate<validate>& imm) {
    double d = imm.value;
    if (d == 0) {
      out_ << (1 / d < 0 ? " -0.0" : " 0.0");
    } else if (std::isinf(d)) {
      out_ << (d > 0 ? " inf" : " -inf");
    } else if (std::isnan(d)) {
      uint64_t bits = base::bit_cast<uint64_t>(d);
      uint64_t payload = bits & 0xF'FFFF'FFFF'FFFFull;
      uint64_t signbit = bits >> 63;
      if (payload == 0x8'0000'0000'0000ull) {
        out_ << (signbit == 1 ? " -nan" : " nan");
      } else {
        out_ << (signbit == 1 ? " -nan:" : " +nan:");
        owner_->PrintHexNumber(out_, payload);
      }
    } else {
      char buffer[100];
      const char* str = DoubleToCString(d, base::VectorOf(buffer, 100u));
      out_ << " " << str;
    }
  }

  void S128Const(Simd128Immediate<validate>& imm) {
    if (owner_->current_opcode_ == kExprI8x16Shuffle) {
      for (int i = 0; i < 16; i++) {
        out_ << " " << uint32_t{imm.value[i]};
      }
    } else {
      DCHECK_EQ(owner_->current_opcode_, kExprS128Const);
      out_ << " i32x4";
      for (int i = 0; i < 4; i++) {
        out_ << " 0x";
        for (int j = 3; j >= 0; j--) {  // Little endian.
          uint8_t b = imm.value[i * 4 + j];
          out_ << kUpperHexChars[b >> 4];
          out_ << kUpperHexChars[b & 0xF];
        }
      }
    }
  }

  void StringConst(StringConstImmediate<validate>& imm) {
    // TODO(jkummerow): Print (a prefix of) the string?
    out_ << " " << imm.index;
  }

  void MemoryInit(MemoryInitImmediate<validate>& imm) {
    DataSegmentIndex(imm.data_segment);
    if (imm.memory.index != 0) out_ << " " << uint32_t{imm.memory.index};
  }

  void MemoryCopy(MemoryCopyImmediate<validate>& imm) {
    if (imm.memory_dst.index == 0 && imm.memory_src.index == 0) return;
    out_ << " " << uint32_t{imm.memory_dst.index};
    out_ << " " << uint32_t{imm.memory_src.index};
  }

  void TableInit(TableInitImmediate<validate>& imm) {
    if (imm.table.index != 0) TableIndex(imm.table);
    ElemSegmentIndex(imm.element_segment);
  }

  void TableCopy(TableCopyImmediate<validate>& imm) {
    if (imm.table_dst.index == 0 && imm.table_src.index == 0) return;
    out_ << " ";
    names()->PrintTableName(out_, imm.table_dst.index);
    out_ << " ";
    names()->PrintTableName(out_, imm.table_src.index);
  }

  void ArrayCopy(IndexImmediate<validate>& dst, IndexImmediate<validate>& src) {
    out_ << " ";
    names()->PrintTypeName(out_, dst.index);
    out_ << " ";
    names()->PrintTypeName(out_, src.index);
    use_type(dst.index);
    use_type(src.index);
  }

 private:
  void use_type(uint32_t type_index) { owner_->used_types_.insert(type_index); }

  NamesProvider* names() { return owner_->names_; }

  uint32_t func_index() { return owner_->func_index_; }

  StringBuilder& out_;
  FunctionBodyDisassembler* owner_;
};

uint32_t FunctionBodyDisassembler::PrintImmediatesAndGetLength(
    StringBuilder& out) {
  using Printer = ImmediatesPrinter<validate>;
  Printer imm_printer(out, this);
  return WasmDecoder::OpcodeLength<Printer>(this, this->pc_, &imm_printer);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
