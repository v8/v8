// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "include/libplatform/libplatform.h"
#include "include/v8-initialization.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-disassembler-impl.h"
#include "src/wasm/wasm-opcodes-inl.h"

#if V8_OS_POSIX
#include <unistd.h>
#endif

int PrintHelp(char** argv) {
  std::cerr << "Usage: Specify an action and a module name in any order.\n"
            << "The action can be any of:\n"

            << " --help\n"
            << "     Print this help and exit.\n"

            << " --list-functions\n"
            << "     List functions in the given module\n"

            << " --section-stats\n"
            << "     Show information about sections in the given module\n"

            << " --single-wat FUNC_INDEX\n"
            << "     Dump function FUNC_INDEX in .wat format\n"

            << " --full-wat\n"
            << "     Dump full module in .wat format\n"

            << " --single-hexdump FUNC_INDEX\n"
            << "     Dump function FUNC_INDEX in annotated hex format\n"

            << " --full-hexdump\n"
            << "     Dump full module in annotated hex format\n"

            << "The module name must be a file name.\n";
  return 1;
}

namespace v8 {
namespace internal {
namespace wasm {

enum class OutputMode { kWat, kHexDump };
static constexpr char kHexChars[] = "0123456789abcdef";

char* PrintHexBytesCore(char* ptr, uint32_t num_bytes, const byte* start) {
  for (uint32_t i = 0; i < num_bytes; i++) {
    byte b = *(start + i);
    *(ptr++) = '0';
    *(ptr++) = 'x';
    *(ptr++) = kHexChars[b >> 4];
    *(ptr++) = kHexChars[b & 0xF];
    *(ptr++) = ',';
    *(ptr++) = ' ';
  }
  return ptr;
}

// A variant of FunctionBodyDisassembler that can produce "annotated hex dump"
// format, e.g.:
//     0xfb, 0x07, 0x01,  // struct.new $type1
class ExtendedFunctionDis : public FunctionBodyDisassembler {
 public:
  ExtendedFunctionDis(Zone* zone, const WasmModule* module, uint32_t func_index,
                      WasmFeatures* detected, const FunctionSig* sig,
                      const byte* start, const byte* end, uint32_t offset,
                      NamesProvider* names)
      : FunctionBodyDisassembler(zone, module, func_index, detected, sig, start,
                                 end, offset, names) {}

  static constexpr uint32_t kWeDontCareAboutByteCodeOffsetsHere = 0;

  void HexDump(MultiLineStringBuilder& out, FunctionHeader include_header) {
    out_ = &out;
    if (!more()) return;  // Fuzzers...
    // Print header.
    if (include_header == kPrintHeader) {
      out << "  // func ";
      names_->PrintFunctionName(out, func_index_, NamesProvider::kDevTools);
      PrintSignatureOneLine(out, sig_, func_index_, names_, true,
                            NamesProvider::kIndexAsComment);
      out.NextLine(kWeDontCareAboutByteCodeOffsetsHere);
    }

    // Decode and print locals.
    uint32_t locals_length;
    InitializeLocalsFromSig();
    DecodeLocals(pc_, &locals_length);
    if (failed()) {
      // TODO(jkummerow): Better error handling.
      out << "Failed to decode locals";
      return;
    }
    uint32_t total_length = 0;
    uint32_t length;
    uint32_t entries = read_u32v<validate>(pc_, &length);
    PrintHexBytes(out, length, pc_, 4);
    out << " // " << entries << " entries in locals list";
    out.NextLine(kWeDontCareAboutByteCodeOffsetsHere);
    total_length += length;
    while (entries-- > 0) {
      uint32_t count_length;
      uint32_t count = read_u32v<validate>(pc_ + total_length, &count_length);
      uint32_t type_length;
      ValueType type = value_type_reader::read_value_type<validate>(
          this, pc_ + total_length + count_length, &type_length, nullptr,
          WasmFeatures::All());
      PrintHexBytes(out, count_length + type_length, pc_ + total_length, 4);
      out << " // " << count << (count != 1 ? " locals" : " local")
          << " of type ";
      names_->PrintValueType(out, type);
      out.NextLine(kWeDontCareAboutByteCodeOffsetsHere);
      total_length += count_length + type_length;
    }

    consume_bytes(locals_length);

    // Main loop.
    while (pc_ < end_) {
      WasmOpcode opcode = GetOpcode();
      current_opcode_ = opcode;  // Some immediates need to know this.
      StringBuilder immediates;
      uint32_t length = PrintImmediatesAndGetLength(immediates);
      PrintHexBytes(out, length, pc_, 4);
      if (opcode == kExprEnd) {
        out << " // end";
        if (label_stack_.size() > 0) {
          const LabelInfo& label = label_stack_.back();
          if (label.start != nullptr) {
            out << " ";
            out.write(label.start, label.length);
          }
          label_stack_.pop_back();
        }
      } else {
        out << " // " << WasmOpcodes::OpcodeName(opcode);
      }
      out.write(immediates.start(), immediates.length());
      if (opcode == kExprBlock || opcode == kExprIf || opcode == kExprLoop ||
          opcode == kExprTry) {
        label_stack_.emplace_back(out.line_number(), out.length(),
                                  label_occurrence_index_++);
      }
      out.NextLine(kWeDontCareAboutByteCodeOffsetsHere);
      pc_ += length;
    }

    if (pc_ != end_) {
      // TODO(jkummerow): Better error handling.
      out << "Beyond end of code\n";
    }
  }

  void HexdumpConstantExpression(MultiLineStringBuilder& out) {
    while (pc_ < end_) {
      WasmOpcode opcode = GetOpcode();
      current_opcode_ = opcode;  // Some immediates need to know this.
      StringBuilder immediates;
      uint32_t length = PrintImmediatesAndGetLength(immediates);
      // Don't print the final "end" separately.
      if (pc_ + length + 1 == end_ && *(pc_ + length) == kExprEnd) {
        length++;
      }
      PrintHexBytes(out, length, pc_, 4);
      out << " // " << WasmOpcodes::OpcodeName(opcode);
      out.write(immediates.start(), immediates.length());
      out.NextLine(kWeDontCareAboutByteCodeOffsetsHere);
      pc_ += length;
    }
  }

  void PrintHexBytes(StringBuilder& out, uint32_t num_bytes, const byte* start,
                     uint32_t fill_to_minimum = 0) {
    constexpr int kCharsPerByte = 6;  // Length of "0xFF, ".
    uint32_t max = std::max(num_bytes, fill_to_minimum) * kCharsPerByte + 2;
    char* ptr = out.allocate(max);
    *(ptr++) = ' ';
    *(ptr++) = ' ';
    ptr = PrintHexBytesCore(ptr, num_bytes, start);
    if (fill_to_minimum > num_bytes) {
      memset(ptr, ' ', (fill_to_minimum - num_bytes) * kCharsPerByte);
    }
  }
};

// A variant of ModuleDisassembler that produces "annotated hex dump" format,
// e.g.:
//     0x01, 0x70, 0x00,  // table count 1: funcref no maximum
class HexDumpModuleDis {
 public:
  using DumpingModuleDecoder = ModuleDecoderTemplate<HexDumpModuleDis>;

  HexDumpModuleDis(MultiLineStringBuilder& out, const WasmModule* module,
                   NamesProvider* names, const ModuleWireBytes wire_bytes,
                   AccountingAllocator* allocator)
      : out_(out),
        module_(module),
        names_(names),
        wire_bytes_(wire_bytes),
        allocator_(allocator),
        zone_(allocator, "disassembler") {
    for (const WasmImport& import : module->import_table) {
      switch (import.kind) {
        // clang-format off
        case kExternalFunction:                       break;
        case kExternalTable:    next_table_index_++;  break;
        case kExternalMemory:                         break;
        case kExternalGlobal:   next_global_index_++; break;
        case kExternalTag:      next_tag_index_++;    break;
          // clang-format on
      }
    }
  }

  // Public entrypoint.
  void PrintModule() {
    constexpr bool verify_functions = false;
    DumpingModuleDecoder decoder(WasmFeatures::All(), wire_bytes_.start(),
                                 wire_bytes_.end(), kWasmOrigin, *this);
    decoder_ = &decoder;
    out_ << "[";
    out_.NextLine(0);
    decoder.DecodeModule(nullptr, allocator_, verify_functions);
    out_ << "]";

    if (total_bytes_ != wire_bytes_.length()) {
      std::cerr << "WARNING: OUTPUT INCOMPLETE. Disassembled " << total_bytes_
                << " out of " << wire_bytes_.length() << " bytes.\n";
      // TODO(jkummerow): Would it be helpful to DCHECK here?
    }
  }

  // Tracer hooks.
  void Bytes(const byte* start, uint32_t count) {
    if (count > kMaxBytesPerLine) {
      DCHECK_EQ(queue_, nullptr);
      queue_ = start;
      queue_length_ = count;
      total_bytes_ += count;
      return;
    }
    if (line_bytes_ == 0) out_ << "  ";
    PrintHexBytes(out_, count, start);
    line_bytes_ += count;
    total_bytes_ += count;
  }

  void Description(const char* desc) { description_ << desc; }
  void Description(const char* desc, size_t length) {
    description_.write(desc, length);
  }
  void Description(uint32_t number) {
    if (description_.length() != 0) description_ << " ";
    description_ << number;
  }
  void Description(ValueType type) {
    if (description_.length() != 0) description_ << " ";
    names_->PrintValueType(description_, type);
  }
  void Description(HeapType type) {
    if (description_.length() != 0) description_ << " ";
    names_->PrintHeapType(description_, type);
  }
  void Description(const FunctionSig* sig) {
    PrintSignatureOneLine(description_, sig, 0 /* ignored */, names_, false);
  }
  void FunctionName(uint32_t func_index) {
    description_ << func_index << " ";
    names_->PrintFunctionName(description_, func_index,
                              NamesProvider::kDevTools);
  }

  void NextLineIfFull() {
    if (queue_ || line_bytes_ >= kPadBytes) NextLine();
  }
  void NextLineIfNonEmpty() {
    if (queue_ || line_bytes_ > 0) NextLine();
  }
  void NextLine() {
    if (queue_) {
      // Print queued hex bytes first, unless there have also been unqueued
      // bytes.
      if (line_bytes_ > 0) {
        // Keep the queued bytes together on the next line.
        for (; line_bytes_ < kPadBytes; line_bytes_++) {
          out_ << "      ";
        }
        out_ << " // ";
        out_.write(description_.start(), description_.length());
        out_.NextLine(kDontCareAboutOffsets);
      }
      while (queue_length_ > kMaxBytesPerLine) {
        out_ << "  ";
        PrintHexBytes(out_, kMaxBytesPerLine, queue_);
        out_.NextLine(kDontCareAboutOffsets);
        queue_length_ -= kMaxBytesPerLine;
        queue_ += kMaxBytesPerLine;
      }
      if (queue_length_ > 0) {
        out_ << "  ";
        PrintHexBytes(out_, queue_length_, queue_);
      }
      if (line_bytes_ == 0) {
        if (queue_length_ > kPadBytes) {
          out_.NextLine(kDontCareAboutOffsets);
          out_ << "                           // ";
        } else {
          for (uint32_t i = queue_length_; i < kPadBytes; i++) {
            out_ << "      ";
          }
          out_ << " // ";
        }
        out_.write(description_.start(), description_.length());
      }
      queue_ = nullptr;
    } else {
      // No queued bytes; just write the accumulated description.
      if (description_.length() != 0) {
        if (line_bytes_ == 0) out_ << "  ";
        for (; line_bytes_ < kPadBytes; line_bytes_++) {
          out_ << "      ";
        }
        out_ << " // ";
        out_.write(description_.start(), description_.length());
      }
    }
    out_.NextLine(kDontCareAboutOffsets);
    line_bytes_ = 0;
    description_.rewind_to_start();
  }

  // We don't care about offsets, but we can use these hooks to provide
  // helpful indexing comments in long lists.
  void TypeOffset(uint32_t offset) {
    if (module_->types.size() > 3) {
      description_ << "type #" << next_type_index_ << " ";
      names_->PrintTypeName(description_, next_type_index_);
      next_type_index_++;
    }
  }
  void ImportOffset(uint32_t offset) {
    description_ << "import #" << next_import_index_++;
    NextLine();
  }
  void TableOffset(uint32_t offset) {
    if (module_->tables.size() > 3) {
      description_ << "table #" << next_table_index_++;
    }
  }
  void MemoryOffset(uint32_t offset) {}
  void TagOffset(uint32_t offset) {
    if (module_->tags.size() > 3) {
      description_ << "tag #" << next_tag_index_++ << ":";
    }
  }
  void GlobalOffset(uint32_t offset) {
    description_ << "global #" << next_global_index_++ << ":";
  }
  void StartOffset(uint32_t offset) {}
  void ElementOffset(uint32_t offset) {
    if (module_->elem_segments.size() > 3) {
      description_ << "segment #" << next_segment_index_++;
      NextLine();
    }
  }
  void DataOffset(uint32_t offset) {
    if (module_->data_segments.size() > 3) {
      description_ << "data segment #" << next_data_segment_index_++;
      NextLine();
    }
  }

  // The following two hooks give us an opportunity to call the hex-dumping
  // function body disassembler for initializers and functions.
  void InitializerExpression(const byte* start, const byte* end,
                             ValueType expected_type) {
    WasmFeatures detected;
    auto sig = FixedSizeSignature<ValueType>::Returns(expected_type);
    uint32_t offset = decoder_->pc_offset();
    ExtendedFunctionDis d(&zone_, module_, 0, &detected, &sig, start, end,
                          offset, names_);
    d.HexdumpConstantExpression(out_);
    total_bytes_ += static_cast<size_t>(end - start);
  }

  void FunctionBody(const WasmFunction* func, const byte* start) {
    const byte* end = start + func->code.length();
    WasmFeatures detected;
    uint32_t offset = static_cast<uint32_t>(start - decoder_->start());
    ExtendedFunctionDis d(&zone_, module_, func->func_index, &detected,
                          func->sig, start, end, offset, names_);
    d.HexDump(out_, FunctionBodyDisassembler::kSkipHeader);
    total_bytes_ += func->code.length();
  }

  // We have to do extra work for the name section here, because the regular
  // decoder mostly just skips over it.
  void NameSection(const byte* start, const byte* end, uint32_t offset) {
    Decoder decoder(start, end, offset);
    while (decoder.ok() && decoder.more()) {
      uint8_t name_type = decoder.consume_u8("name type: ", *this);
      Description(NameTypeName(name_type));
      NextLine();
      uint32_t payload_length = decoder.consume_u32v("payload length:", *this);
      Description(payload_length);
      NextLine();
      if (!decoder.checkAvailable(payload_length)) break;
      switch (name_type) {
        case kModuleCode:
          consume_string(&decoder, unibrow::Utf8Variant::kLossyUtf8,
                         "module name", *this);
          break;
        case kFunctionCode:
        case kTypeCode:
        case kTableCode:
        case kMemoryCode:
        case kGlobalCode:
        case kElementSegmentCode:
        case kDataSegmentCode:
        case kTagCode:
          DumpNameMap(decoder);
          break;
        case kLocalCode:
        case kLabelCode:
        case kFieldCode:
          DumpIndirectNameMap(decoder);
          break;
        default:
          Bytes(decoder.pc(), payload_length);
          NextLine();
          decoder.consume_bytes(payload_length);
          break;
      }
    }
  }

  // TODO(jkummerow): Consider using an OnFirstError() override to offer
  // help when decoding fails.

 private:
  static constexpr uint32_t kDontCareAboutOffsets = 0;
  static constexpr uint32_t kMaxBytesPerLine = 8;
  static constexpr uint32_t kPadBytes = 4;

  void PrintHexBytes(StringBuilder& out, uint32_t num_bytes,
                     const byte* start) {
    char* ptr = out.allocate(num_bytes * 6);
    PrintHexBytesCore(ptr, num_bytes, start);
  }

  void DumpNameMap(Decoder& decoder) {
    uint32_t count = decoder.consume_u32v("names count", *this);
    Description(count);
    NextLine();
    for (uint32_t i = 0; i < count; i++) {
      uint32_t index = decoder.consume_u32v("index", *this);
      Description(index);
      Description(" ");
      consume_string(&decoder, unibrow::Utf8Variant::kLossyUtf8, "name", *this);
      if (!decoder.ok()) break;
    }
  }

  void DumpIndirectNameMap(Decoder& decoder) {
    uint32_t outer_count = decoder.consume_u32v("outer count", *this);
    Description(outer_count);
    NextLine();
    for (uint32_t i = 0; i < outer_count; i++) {
      uint32_t outer_index = decoder.consume_u32v("outer index", *this);
      Description(outer_index);
      uint32_t inner_count = decoder.consume_u32v(" inner count", *this);
      Description(inner_count);
      NextLine();
      for (uint32_t j = 0; j < inner_count; j++) {
        uint32_t inner_index = decoder.consume_u32v("inner index", *this);
        Description(inner_index);
        Description(" ");
        consume_string(&decoder, unibrow::Utf8Variant::kLossyUtf8, "name",
                       *this);
        if (!decoder.ok()) break;
      }
      if (!decoder.ok()) break;
    }
  }

  static constexpr const char* NameTypeName(uint8_t name_type) {
    switch (name_type) {
      // clang-format off
      case kModuleCode:         return "module";
      case kFunctionCode:       return "function";
      case kTypeCode:           return "type";
      case kTableCode:          return "table";
      case kMemoryCode:         return "memory";
      case kGlobalCode:         return "global";
      case kElementSegmentCode: return "element segment";
      case kDataSegmentCode:    return "data segment";
      case kTagCode:            return "tag";
      case kLocalCode:          return "local";
      case kLabelCode:          return "label";
      case kFieldCode:          return "field";
      default:                  return "unknown";
        // clang-format on
    }
  }
  MultiLineStringBuilder& out_;
  const WasmModule* module_;
  NamesProvider* names_;
  const ModuleWireBytes wire_bytes_;
  AccountingAllocator* allocator_;
  Zone zone_;

  StringBuilder description_;
  const byte* queue_{nullptr};
  uint32_t queue_length_{0};
  uint32_t line_bytes_{0};
  size_t total_bytes_{0};
  DumpingModuleDecoder* decoder_{nullptr};

  uint32_t next_type_index_{0};
  uint32_t next_import_index_{0};
  uint32_t next_table_index_{0};
  uint32_t next_global_index_{0};
  uint32_t next_tag_index_{0};
  uint32_t next_segment_index_{0};
  uint32_t next_data_segment_index_{0};
};

////////////////////////////////////////////////////////////////////////////////

class FormatConverter {
 public:
  explicit FormatConverter(std::string path) {
    if (!LoadFile(path)) return;
    base::Vector<const byte> wire_bytes(raw_bytes_.data(), raw_bytes_.size());
    wire_bytes_ = ModuleWireBytes({raw_bytes_.data(), raw_bytes_.size()});
    ModuleResult result =
        DecodeWasmModuleForDisassembler(start(), end(), &allocator_);
    if (result.failed()) {
      WasmError error = result.error();
      std::cerr << "Decoding error: " << error.message() << " at offset "
                << error.offset() << "\n";
      // TODO(jkummerow): Show some disassembly.
      return;
    }
    ok_ = true;
    module_ = result.value();
    names_provider_ =
        std::make_unique<NamesProvider>(module_.get(), wire_bytes);
  }

  bool ok() const { return ok_; }

  void ListFunctions() {
    DCHECK(ok_);
    const WasmModule* m = module();
    uint32_t num_functions = static_cast<uint32_t>(m->functions.size());
    std::cout << "There are " << num_functions << " functions ("
              << m->num_imported_functions << " imported, "
              << m->num_declared_functions
              << " locally defined); the following have names:\n";
    for (uint32_t i = 0; i < num_functions; i++) {
      StringBuilder sb;
      names()->PrintFunctionName(sb, i);
      if (sb.length() == 0) continue;
      std::string name(sb.start(), sb.length());
      std::cout << i << " " << name << "\n";
    }
  }

  void SectionStats() {
    DCHECK(ok_);
    Decoder decoder(start(), end());
    static constexpr int kModuleHeaderSize = 8;
    decoder.consume_bytes(kModuleHeaderSize, "module header");

    uint32_t module_size = static_cast<uint32_t>(end() - start());
    int digits = 2;
    for (uint32_t comparator = 100; module_size >= comparator;
         comparator *= 10) {
      digits++;
    }
    size_t kMinNameLength = 8;
    // 18 = kMinNameLength + strlen(" section: ").
    std::cout << std::setw(18) << std::left << "Module size: ";
    std::cout << std::setw(digits) << std::right << module_size << " bytes\n";
    NoTracer no_tracer;
    for (WasmSectionIterator it(&decoder, no_tracer); it.more();
         it.advance(true)) {
      const char* name = SectionName(it.section_code());
      size_t name_len = strlen(name);
      std::cout << SectionName(it.section_code()) << " section: ";
      for (; name_len < kMinNameLength; name_len++) std::cout << " ";

      uint32_t length = it.section_length();
      std::cout << std::setw(name_len > kMinNameLength ? 0 : digits) << length
                << " bytes / ";

      std::cout << std::fixed << std::setprecision(1) << std::setw(4)
                << 100.0 * length / module_size;
      std::cout << "% of total\n";
    }
  }

  void DisassembleFunction(uint32_t func_index, MultiLineStringBuilder& out,
                           OutputMode mode) {
    DCHECK(ok_);
    if (func_index >= module()->functions.size()) {
      out << "Invalid function index!\n";
      return;
    }
    if (func_index < module()->num_imported_functions) {
      out << "Can't disassemble imported functions!\n";
      return;
    }
    const WasmFunction* func = &module()->functions[func_index];
    Zone zone(&allocator_, "disassembler");
    WasmFeatures detected;
    base::Vector<const byte> code = wire_bytes_.GetFunctionBytes(func);

    ExtendedFunctionDis d(&zone, module(), func_index, &detected, func->sig,
                          code.begin(), code.end(), func->code.offset(),
                          names());
    if (mode == OutputMode::kWat) {
      d.DecodeAsWat(out, {0, 1});
    } else if (mode == OutputMode::kHexDump) {
      d.HexDump(out, FunctionBodyDisassembler::kPrintHeader);
    }

    // Print any types that were used by the function.
    out.NextLine(0);
    ModuleDisassembler md(out, module(), names(), wire_bytes_,
                          ModuleDisassembler::kSkipByteOffsets, &allocator_);
    for (uint32_t type_index : d.used_types()) {
      md.PrintTypeDefinition(type_index, {0, 1},
                             NamesProvider::kIndexAsComment);
    }
  }

  void WatForModule(MultiLineStringBuilder& out) {
    DCHECK(ok_);
    ModuleDisassembler md(out, module(), names(), wire_bytes_,
                          ModuleDisassembler::kSkipByteOffsets, &allocator_);
    md.PrintModule({0, 2});
  }

  void HexdumpForModule(MultiLineStringBuilder& out) {
    DCHECK(ok_);
    HexDumpModuleDis md(out, module(), names(), wire_bytes_, &allocator_);
    md.PrintModule();
  }

 private:
  byte* start() { return raw_bytes_.data(); }
  byte* end() { return start() + raw_bytes_.size(); }
  const WasmModule* module() { return module_.get(); }
  NamesProvider* names() { return names_provider_.get(); }

  bool LoadFile(std::string path) {
    if (path == "-") return LoadFileFromStream(std::cin);

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      std::cerr << "Failed to open " << path << "!\n";
      return false;
    }
    return LoadFileFromStream(input);
  }

  bool LoadFileFromStream(std::istream& input) {
    int c0 = input.get();
    int c1 = input.get();
    int c2 = input.get();
    int c3 = input.peek();
    input.putback(c2);
    input.putback(c1);
    input.putback(c0);
    if (c0 == 0 && c1 == 'a' && c2 == 's' && c3 == 'm') {
      // Wasm binary module.
      raw_bytes_ = std::vector<byte>(std::istreambuf_iterator<char>(input), {});
      return true;
    }
    if (TryParseLiteral(input, raw_bytes_)) return true;
    std::cerr << "That's not a Wasm module!\n";
    return false;
  }

  bool IsWhitespace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v';
  }

  // Attempts to read a module in "array literal" syntax:
  // - Bytes must be separated by ',', may be specified in decimal or hex.
  // - The whole module must be enclosed in '[]', anything outside these
  //   braces is ignored.
  // - Whitespace, line comments, and block comments are ignored.
  // So in particular, this can consume what --full-hexdump produces.
  bool TryParseLiteral(std::istream& input, std::vector<byte>& output_bytes) {
    int c = input.get();
    // Skip anything before the first opening '['.
    while (c != '[' && c != EOF) c = input.get();
    enum State { kBeforeValue = 0, kAfterValue = 1, kDecimal = 10, kHex = 16 };
    State state = kBeforeValue;
    int value = 0;
    while (true) {
      c = input.get();
      // Skip whitespace, except inside values.
      if (state < kDecimal) {
        while (IsWhitespace(c)) c = input.get();
      }
      // End of file before ']' is unexpected = invalid.
      if (c == EOF) return false;
      // Skip comments.
      if (c == '/' && input.peek() == '/') {
        // Line comment. Skip until '\n'.
        do {
          c = input.get();
        } while (c != '\n' && c != EOF);
        continue;
      }
      if (c == '/' && input.peek() == '*') {
        // Block comment. Skip until "*/".
        input.get();  // Consume '*' of opening "/*".
        do {
          c = input.get();
          if (c == '*' && input.peek() == '/') {
            input.get();  // Consume '/'.
            break;
          }
        } while (c != EOF);
        continue;
      }
      if (state == kBeforeValue) {
        if (c == '0' && (input.peek() == 'x' || input.peek() == 'X')) {
          input.get();  // Consume the 'x'.
          state = kHex;
          continue;
        }
        if (c >= '0' && c <= '9') {
          state = kDecimal;
          // Fall through to handling kDecimal below.
        } else if (c == ']') {
          return true;
        } else {
          return false;
        }
      }
      DCHECK(state == kDecimal || state == kHex || state == kAfterValue);
      if (c == ',') {
        DCHECK_LT(value, 256);
        output_bytes.push_back(static_cast<byte>(value));
        state = kBeforeValue;
        value = 0;
        continue;
      }
      if (c == ']') {
        DCHECK_LT(value, 256);
        output_bytes.push_back(static_cast<byte>(value));
        return true;
      }
      if (state == kAfterValue) {
        // Didn't take the ',' or ']' paths above, anything else is invalid.
        DCHECK(c != ',' && c != ']');
        return false;
      }
      DCHECK(state == kDecimal || state == kHex);
      if (IsWhitespace(c)) {
        state = kAfterValue;
        continue;
      }
      int v;
      if (c >= '0' && c <= '9') {
        v = c - '0';
      } else if (state == kHex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
        // Setting the "0x20" bit maps uppercase onto lowercase letters.
        v = (c | 0x20) - 'a' + 10;
      } else {
        return false;
      }
      value = value * state + v;
      if (value > 0xFF) return false;
    }
  }

  AccountingAllocator allocator_;
  bool ok_{false};
  std::vector<byte> raw_bytes_;
  ModuleWireBytes wire_bytes_{{}};
  std::shared_ptr<WasmModule> module_;
  std::unique_ptr<NamesProvider> names_provider_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

using FormatConverter = v8::internal::wasm::FormatConverter;
using OutputMode = v8::internal::wasm::OutputMode;
using MultiLineStringBuilder = v8::internal::wasm::MultiLineStringBuilder;

enum class Action {
  kUnset,
  kHelp,
  kListFunctions,
  kSectionStats,
  kFullWat,
  kFullHexdump,
  kSingleWat,
  kSingleHexdump,
};

struct Options {
  const char* filename = nullptr;
  Action action = Action::kUnset;
  int func_index = -1;
};

void ListFunctions(const Options& options) {
  FormatConverter fc(options.filename);
  if (fc.ok()) fc.ListFunctions();
}

void SectionStats(const Options& options) {
  FormatConverter fc(options.filename);
  if (fc.ok()) fc.SectionStats();
}

void WatForFunction(const Options& options) {
  FormatConverter fc(options.filename);
  if (!fc.ok()) return;
  MultiLineStringBuilder sb;
  fc.DisassembleFunction(options.func_index, sb, OutputMode::kWat);
  sb.DumpToStdout();
}

void HexdumpForFunction(const Options& options) {
  FormatConverter fc(options.filename);
  if (!fc.ok()) return;
  MultiLineStringBuilder sb;
  fc.DisassembleFunction(options.func_index, sb, OutputMode::kHexDump);
  sb.DumpToStdout();
}

void WatForModule(const Options& options) {
  FormatConverter fc(options.filename);
  if (!fc.ok()) return;
  MultiLineStringBuilder sb;
  fc.WatForModule(sb);
  sb.DumpToStdout();
}

void HexdumpForModule(const Options& options) {
  FormatConverter fc(options.filename);
  if (!fc.ok()) return;
  MultiLineStringBuilder sb;
  fc.HexdumpForModule(sb);
  sb.DumpToStdout();
}

bool ParseInt(char* s, int* out) {
  char* end;
  if (s[0] == '\0') return false;
  errno = 0;
  long l = strtol(s, &end, 10);
  if (errno != 0 || *end != '\0' || l > std::numeric_limits<int>::max() ||
      l < std::numeric_limits<int>::min()) {
    return false;
  }
  *out = static_cast<int>(l);
  return true;
}

int ParseOptions(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
        strcmp(argv[i], "help") == 0) {
      options->action = Action::kHelp;
    } else if (strcmp(argv[i], "--list-functions") == 0) {
      options->action = Action::kListFunctions;
    } else if (strcmp(argv[i], "--section-stats") == 0) {
      options->action = Action::kSectionStats;
    } else if (strcmp(argv[i], "--full-wat") == 0) {
      options->action = Action::kFullWat;
    } else if (strcmp(argv[i], "--full-hexdump") == 0) {
      options->action = Action::kFullHexdump;
    } else if (strcmp(argv[i], "--single-wat") == 0) {
      options->action = Action::kSingleWat;
      if (i == argc - 1 || !ParseInt(argv[++i], &options->func_index)) {
        return PrintHelp(argv);
      }
    } else if (strncmp(argv[i], "--single-wat=", 13) == 0) {
      options->action = Action::kSingleWat;
      if (!ParseInt(argv[i] + 13, &options->func_index)) return PrintHelp(argv);
    } else if (strcmp(argv[i], "--single-hexdump") == 0) {
      options->action = Action::kSingleHexdump;
      if (i == argc - 1 || !ParseInt(argv[++i], &options->func_index)) {
        return PrintHelp(argv);
      }
    } else if (strncmp(argv[i], "--single-hexdump=", 17) == 0) {
      if (!ParseInt(argv[i] + 17, &options->func_index)) return PrintHelp(argv);
    } else if (options->filename != nullptr) {
      return PrintHelp(argv);
    } else {
      options->filename = argv[i];
    }
  }
#if V8_OS_POSIX
  // When piping data into wami, specifying the input as "-" is optional.
  if (options->filename == nullptr && !isatty(STDIN_FILENO)) {
    options->filename = "-";
  }
#endif
  if (options->action == Action::kUnset || options->filename == nullptr) {
    return PrintHelp(argv);
  }
  return 0;
}

int main(int argc, char** argv) {
  Options options;
  if (ParseOptions(argc, argv, &options) != 0) return 1;
  // Bootstrap the basics.
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();

  switch (options.action) {
    // clang-format off
    case Action::kHelp:          PrintHelp(argv);             break;
    case Action::kListFunctions: ListFunctions(options);      break;
    case Action::kSectionStats:  SectionStats(options);       break;
    case Action::kSingleWat:     WatForFunction(options);     break;
    case Action::kSingleHexdump: HexdumpForFunction(options); break;
    case Action::kFullWat:       WatForModule(options);       break;
    case Action::kFullHexdump:   HexdumpForModule(options);   break;
    case Action::kUnset:         UNREACHABLE();
      // clang-format on
  }

  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  return 0;
}
