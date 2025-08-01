// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_MODULE_DECODER_IMPL_H_
#define V8_WASM_MODULE_DECODER_IMPL_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include "src/base/platform/wrappers.h"
#include "src/logging/counters.h"
#include "src/strings/unicode.h"
#include "src/utils/ostreams.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/constant-expression-interface.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/well-known-imports.h"

namespace v8::internal::wasm {

#define TRACE(...)                                        \
  do {                                                    \
    if (v8_flags.trace_wasm_decoder) PrintF(__VA_ARGS__); \
  } while (false)

constexpr char kNameString[] = "name";
constexpr char kSourceMappingURLString[] = "sourceMappingURL";
constexpr char kInstTraceString[] = "metadata.code.trace_inst";
constexpr char kBranchHintsString[] = "metadata.code.branch_hint";
#if V8_CC_GNU
// TODO(miladfarca): remove once switched to using Clang.
__attribute__((used))
#endif
constexpr char kCompilationPriorityString[] =
    "metadata.code.compilation_priority";
constexpr char kDebugInfoString[] = ".debug_info";
constexpr char kExternalDebugInfoString[] = "external_debug_info";
constexpr char kBuildIdString[] = "build_id";
// TODO(403372470): Rename to "descriptors" when finalized.
constexpr char kDescriptorsString[] = "experimental-descriptors";

inline const char* ExternalKindName(ImportExportKindCode kind) {
  switch (kind) {
    case kExternalFunction:
      return "function";
    case kExternalTable:
      return "table";
    case kExternalMemory:
      return "memory";
    case kExternalGlobal:
      return "global";
    case kExternalTag:
      return "tag";
  }
  return "unknown";
}

inline bool validate_utf8(Decoder* decoder, WireBytesRef string) {
  return unibrow::Utf8::ValidateEncoding(
      decoder->start() + decoder->GetBufferRelativeOffset(string.offset()),
      string.length());
}

// Reads a length-prefixed string, checking that it is within bounds. Returns
// the offset of the string, and the length as an out parameter.
inline WireBytesRef consume_string(Decoder* decoder,
                                   unibrow::Utf8Variant grammar,
                                   const char* name, ITracer* tracer) {
  if (tracer) {
    tracer->Description(name);
    tracer->Description(" ");
  }
  uint32_t length = decoder->consume_u32v("length", tracer);
  if (tracer) {
    tracer->Description(": ");
    tracer->Description(length);
    tracer->NextLine();
  }
  uint32_t offset = decoder->pc_offset();
  const uint8_t* string_start = decoder->pc();
  // Consume bytes before validation to guarantee that the string is not oob.
  if (length > 0) {
    if (tracer) {
      tracer->Bytes(decoder->pc(), length);
      tracer->Description(name);
      tracer->Description(": ");
      tracer->Description(reinterpret_cast<const char*>(decoder->pc()), length);
      tracer->NextLine();
    }
    decoder->consume_bytes(length, name);
    if (decoder->ok()) {
      switch (grammar) {
        case unibrow::Utf8Variant::kLossyUtf8:
          break;
        case unibrow::Utf8Variant::kUtf8:
          if (!unibrow::Utf8::ValidateEncoding(string_start, length)) {
            decoder->errorf(string_start, "%s: no valid UTF-8 string", name);
          }
          break;
        case unibrow::Utf8Variant::kWtf8:
          if (!unibrow::Wtf8::ValidateEncoding(string_start, length)) {
            decoder->errorf(string_start, "%s: no valid WTF-8 string", name);
          }
          break;
        case unibrow::Utf8Variant::kUtf8NoTrap:
          UNREACHABLE();
      }
    }
  }
  return {offset, decoder->failed() ? 0 : length};
}

inline WireBytesRef consume_string(Decoder* decoder,
                                   unibrow::Utf8Variant grammar,
                                   const char* name) {
  return consume_string(decoder, grammar, name, ITracer::NoTrace);
}

inline WireBytesRef consume_utf8_string(Decoder* decoder, const char* name,
                                        ITracer* tracer) {
  return consume_string(decoder, unibrow::Utf8Variant::kUtf8, name, tracer);
}

inline SectionCode IdentifyUnknownSectionInternal(Decoder* decoder,
                                                  ITracer* tracer) {
  WireBytesRef string = consume_utf8_string(decoder, "section name", tracer);
  if (decoder->failed()) {
    return kUnknownSectionCode;
  }
  const uint8_t* section_name_start =
      decoder->start() + decoder->GetBufferRelativeOffset(string.offset());

  TRACE("  +%d  section name        : \"%.*s\"\n",
        static_cast<int>(section_name_start - decoder->start()),
        string.length() < 20 ? string.length() : 20, section_name_start);

  using SpecialSectionPair = std::pair<base::Vector<const char>, SectionCode>;
  static constexpr SpecialSectionPair kSpecialSections[]{
      {base::StaticCharVector(kNameString), kNameSectionCode},
      {base::StaticCharVector(kSourceMappingURLString),
       kSourceMappingURLSectionCode},
      {base::StaticCharVector(kInstTraceString), kInstTraceSectionCode},
      {base::StaticCharVector(kBranchHintsString), kBranchHintsSectionCode},
      {base::StaticCharVector(kCompilationPriorityString),
       kCompilationPrioritySectionCode},
      {base::StaticCharVector(kDebugInfoString), kDebugInfoSectionCode},
      {base::StaticCharVector(kExternalDebugInfoString),
       kExternalDebugInfoSectionCode},
      {base::StaticCharVector(kBuildIdString), kBuildIdSectionCode},
      {base::StaticCharVector(kDescriptorsString), kDescriptorsSectionCode}};

  auto name_vec = base::Vector<const char>::cast(
      base::VectorOf(section_name_start, string.length()));
  for (auto& special_section : kSpecialSections) {
    if (name_vec == special_section.first) return special_section.second;
  }

  return kUnknownSectionCode;
}

// An iterator over the sections in a wasm binary module.
// Automatically skips all unknown sections.
class WasmSectionIterator {
 public:
  explicit WasmSectionIterator(Decoder* decoder, ITracer* tracer)
      : decoder_(decoder),
        tracer_(tracer),
        section_code_(kUnknownSectionCode),
        section_start_(decoder->pc()),
        section_end_(decoder->pc()) {
    next();
  }

  bool more() const { return decoder_->ok() && decoder_->more(); }

  SectionCode section_code() const { return section_code_; }

  const uint8_t* section_start() const { return section_start_; }

  uint32_t section_length() const {
    return static_cast<uint32_t>(section_end_ - section_start_);
  }

  base::Vector<const uint8_t> payload() const {
    return {payload_start_, payload_length()};
  }

  const uint8_t* payload_start() const { return payload_start_; }

  uint32_t payload_length() const {
    return static_cast<uint32_t>(section_end_ - payload_start_);
  }

  const uint8_t* section_end() const { return section_end_; }

  // Advances to the next section, checking that decoding the current section
  // stopped at {section_end_}.
  void advance(bool move_to_section_end = false) {
    if (move_to_section_end && decoder_->pc() < section_end_) {
      decoder_->consume_bytes(
          static_cast<uint32_t>(section_end_ - decoder_->pc()));
    }
    if (decoder_->pc() != section_end_) {
      const char* msg = decoder_->pc() < section_end_ ? "shorter" : "longer";
      decoder_->errorf(decoder_->pc(),
                       "section was %s than expected size "
                       "(%u bytes expected, %zu decoded)",
                       msg, section_length(),
                       static_cast<size_t>(decoder_->pc() - section_start_));
    }
    next();
  }

 private:
  Decoder* decoder_;
  ITracer* tracer_;
  SectionCode section_code_;
  const uint8_t* section_start_;
  const uint8_t* payload_start_;
  const uint8_t* section_end_;

  // Reads the section code/name at the current position and sets up
  // the embedder fields.
  void next() {
    if (!decoder_->more()) {
      section_code_ = kUnknownSectionCode;
      return;
    }
    section_start_ = decoder_->pc();
    // Empty line before next section.
    if (tracer_) tracer_->NextLine();
    uint8_t section_code = decoder_->consume_u8("section kind", tracer_);
    if (tracer_) {
      tracer_->Description(": ");
      tracer_->Description(SectionName(static_cast<SectionCode>(section_code)));
      tracer_->NextLine();
    }
    // Read and check the section size.
    uint32_t section_length = decoder_->consume_u32v("section length", tracer_);
    if (tracer_) {
      tracer_->Description(section_length);
      tracer_->NextLine();
    }
    payload_start_ = decoder_->pc();
    section_end_ = payload_start_ + section_length;
    if (section_length > decoder_->available_bytes()) {
      decoder_->errorf(
          section_start_,
          "section (code %u, \"%s\") extends past end of the module "
          "(length %u, remaining bytes %u)",
          section_code, SectionName(static_cast<SectionCode>(section_code)),
          section_length, decoder_->available_bytes());
      section_end_ = payload_start_;
    }

    if (section_code == kUnknownSectionCode) {
      // Check for known custom sections.
      // To identify the unknown section we set the end of the decoder bytes to
      // the end of the custom section, so that we do not read the section name
      // beyond the end of the section.
      const uint8_t* module_end = decoder_->end();
      decoder_->set_end(section_end_);
      section_code = IdentifyUnknownSectionInternal(decoder_, tracer_);
      if (decoder_->ok()) decoder_->set_end(module_end);
      // As a side effect, the above function will forward the decoder to after
      // the identifier string.
      payload_start_ = decoder_->pc();
    } else if (!IsValidSectionCode(section_code)) {
      decoder_->errorf(decoder_->pc(), "unknown section code #0x%02x",
                       section_code);
    }
    section_code_ = decoder_->failed() ? kUnknownSectionCode
                                       : static_cast<SectionCode>(section_code);

    if (section_code_ == kUnknownSectionCode && section_end_ > decoder_->pc()) {
      // Skip to the end of the unknown section.
      uint32_t remaining = static_cast<uint32_t>(section_end_ - decoder_->pc());
      decoder_->consume_bytes(remaining, "section payload", tracer_);
    }
  }
};

inline void DumpModule(const base::Vector<const uint8_t> module_bytes,
                       bool ok) {
  std::string path;
  if (v8_flags.dump_wasm_module_path) {
    path = v8_flags.dump_wasm_module_path;
    if (path.size() && !base::OS::isDirectorySeparator(path[path.size() - 1])) {
      path += base::OS::DirectorySeparator();
    }
  }
  // File are named `<hash>.{ok,failed}.wasm`.
  // Limit the hash to 8 characters (32 bits).
  uint32_t hash = static_cast<uint32_t>(GetWireBytesHash(module_bytes));
  base::EmbeddedVector<char, 32> buf;
  SNPrintF(buf, "%08x.%s.wasm", hash, ok ? "ok" : "failed");
  path += buf.begin();
  size_t rv = 0;
  if (FILE* file = base::OS::FOpen(path.c_str(), "wb")) {
    rv = fwrite(module_bytes.begin(), module_bytes.length(), 1, file);
    base::Fclose(file);
  }
  if (rv != 1) {
    OFStream os(stderr);
    os << "Error while dumping wasm file to " << path << std::endl;
  }
}

// The main logic for decoding the bytes of a module.
class ModuleDecoderImpl : public Decoder {
 public:
  ModuleDecoderImpl(WasmEnabledFeatures enabled_features,
                    base::Vector<const uint8_t> wire_bytes, ModuleOrigin origin,
                    WasmDetectedFeatures* detected_features,
                    ITracer* tracer = ITracer::NoTrace)
      : Decoder(wire_bytes),
        enabled_features_(enabled_features),
        detected_features_(detected_features),
        module_(std::make_shared<WasmModule>(origin)),
        module_start_(wire_bytes.begin()),
        module_end_(wire_bytes.end()),
        tracer_(tracer) {}

  void onFirstError() override {
    pc_ = end_;  // On error, terminate section decoding loop.
  }

  void DecodeModuleHeader(base::Vector<const uint8_t> bytes) {
    if (failed()) return;
    Reset(bytes);

    const uint8_t* pos = pc_;
    uint32_t magic_word = consume_u32("wasm magic", tracer_);
    if (tracer_) tracer_->NextLine();
#define BYTES(x) (x & 0xFF), (x >> 8) & 0xFF, (x >> 16) & 0xFF, (x >> 24) & 0xFF
    if (magic_word != kWasmMagic) {
      errorf(pos,
             "expected magic word %02x %02x %02x %02x, "
             "found %02x %02x %02x %02x",
             BYTES(kWasmMagic), BYTES(magic_word));
    }

    pos = pc_;
    {
      uint32_t magic_version = consume_u32("wasm version", tracer_);
      if (tracer_) tracer_->NextLine();
      if (magic_version != kWasmVersion) {
        errorf(pos,
               "expected version %02x %02x %02x %02x, "
               "found %02x %02x %02x %02x",
               BYTES(kWasmVersion), BYTES(magic_version));
      }
    }
#undef BYTES
  }

  bool CheckSectionOrder(SectionCode section_code) {
    // Check the order of ordered sections.
    if (section_code >= kFirstSectionInModule &&
        section_code < kFirstUnorderedSection) {
      if (section_code < next_ordered_section_) {
        errorf(pc(), "unexpected section <%s>", SectionName(section_code));
        return false;
      }
      next_ordered_section_ = section_code + 1;
      return true;
    }

    // Ignore ordering problems in unknown / custom sections. Even allow them to
    // appear multiple times. As optional sections we use them on a "best
    // effort" basis.
    if (section_code == kUnknownSectionCode) return true;
    if (section_code > kLastKnownModuleSection) return true;

    // The rest is standardized unordered sections; they are checked more
    // thoroughly..
    DCHECK_LE(kFirstUnorderedSection, section_code);
    DCHECK_GE(kLastKnownModuleSection, section_code);

    // Check that unordered sections don't appear multiple times.
    if (has_seen_unordered_section(section_code)) {
      errorf(pc(), "Multiple %s sections not allowed",
             SectionName(section_code));
      return false;
    }
    set_seen_unordered_section(section_code);

    // Define a helper to ensure that sections <= {before} appear before the
    // current unordered section, and everything >= {after} appears after it.
    auto check_order = [this, section_code](SectionCode before,
                                            SectionCode after) -> bool {
      DCHECK_LT(before, after);
      if (next_ordered_section_ > after) {
        errorf(pc(), "The %s section must appear before the %s section",
               SectionName(section_code), SectionName(after));
        return false;
      }
      if (next_ordered_section_ <= before) next_ordered_section_ = before + 1;
      return true;
    };

    // Now check the ordering constraints of specific unordered sections.
    switch (section_code) {
      case kDataCountSectionCode:
        return check_order(kElementSectionCode, kCodeSectionCode);
      case kTagSectionCode:
        return check_order(kMemorySectionCode, kGlobalSectionCode);
      case kStringRefSectionCode:
        // TODO(12868): If there's a tag section, assert that we're after the
        // tag section.
        return check_order(kMemorySectionCode, kGlobalSectionCode);
      case kInstTraceSectionCode:
        // Custom section following code.metadata tool convention containing
        // offsets specifying where trace marks should be emitted.
        // Be lenient with placement of instruction trace section. All except
        // first occurrence after function section and before code section are
        // ignored.
        return true;
      default:
        return true;
    }
  }

  void DecodeSection(SectionCode section_code,
                     base::Vector<const uint8_t> bytes, uint32_t offset) {
    if (failed()) return;
    Reset(bytes, offset);
    TRACE("Section: %s\n", SectionName(section_code));
    TRACE("Decode Section %p - %p\n", bytes.begin(), bytes.end());

    if (!CheckSectionOrder(section_code)) return;

    switch (section_code) {
      case kUnknownSectionCode:
        break;
      case kTypeSectionCode:
        DecodeTypeSection();
        break;
      case kImportSectionCode:
        DecodeImportSection();
        break;
      case kFunctionSectionCode:
        DecodeFunctionSection();
        break;
      case kTableSectionCode:
        DecodeTableSection();
        break;
      case kMemorySectionCode:
        DecodeMemorySection();
        break;
      case kGlobalSectionCode:
        DecodeGlobalSection();
        break;
      case kExportSectionCode:
        DecodeExportSection();
        break;
      case kStartSectionCode:
        DecodeStartSection();
        break;
      case kCodeSectionCode:
        DecodeCodeSection();
        break;
      case kElementSectionCode:
        DecodeElementSection();
        break;
      case kDataSectionCode:
        DecodeDataSection();
        break;
      case kNameSectionCode:
        DecodeNameSection();
        break;
      case kSourceMappingURLSectionCode:
        DecodeSourceMappingURLSection();
        break;
      case kDebugInfoSectionCode:
        module_->debug_symbols[WasmDebugSymbols::Type::EmbeddedDWARF] = {
            WasmDebugSymbols::Type::EmbeddedDWARF, {}};
        consume_bytes(static_cast<uint32_t>(end_ - start_), ".debug_info");
        break;
      case kExternalDebugInfoSectionCode:
        DecodeExternalDebugInfoSection();
        break;
      case kBuildIdSectionCode:
        DecodeBuildIdSection();
        break;
      case kInstTraceSectionCode:
        if (enabled_features_.has_instruction_tracing()) {
          DecodeInstTraceSection();
        } else {
          // Ignore this section when feature is disabled. It is an optional
          // custom section anyways.
          consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
        }
        break;
      case kBranchHintsSectionCode:
        if (enabled_features_.has_branch_hinting()) {
          DecodeBranchHintsSection();
        } else {
          // Ignore this section when feature was disabled. It is an optional
          // custom section anyways.
          consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
        }
        break;
      case kCompilationPrioritySectionCode:
        if (enabled_features_.has_compilation_hints()) {
          DecodeCompilationPrioritySection();
        } else {
          // Ignore this section when feature was disabled. It is an optional
          // custom section anyways.
          consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
        }
        break;
      case kDescriptorsSectionCode:
        DecodeDescriptorsSection();
        break;
      case kDataCountSectionCode:
        DecodeDataCountSection();
        break;
      case kTagSectionCode:
        DecodeTagSection();
        break;
      case kStringRefSectionCode:
        if (enabled_features_.has_stringref()) {
          DecodeStringRefSection();
        } else {
          errorf(pc(),
                 "unexpected section <%s> (enable with "
                 "--experimental-wasm-stringref)",
                 SectionName(section_code));
        }
        break;
      default:
        errorf(pc(), "unexpected section <%s>", SectionName(section_code));
        return;
    }

    if (pc() != bytes.end()) {
      const char* msg = pc() < bytes.end() ? "shorter" : "longer";
      errorf(pc(),
             "section was %s than expected size "
             "(%zu bytes expected, %zu decoded)",
             msg, bytes.size(), static_cast<size_t>(pc() - bytes.begin()));
    }
  }

  static constexpr const char* TypeKindName(uint8_t kind) {
    switch (kind) {
      // clang-format off
      case kWasmFunctionTypeCode:    return "func";
      case kWasmStructTypeCode:      return "struct";
      case kWasmArrayTypeCode:       return "array";
      case kWasmContTypeCode:        return "cont";
      default:                       return "unknown";
        // clang-format on
    }
  }

  TypeDefinition consume_base_type_definition(bool is_descriptor,
                                              bool is_shared) {
    const bool is_final = true;
    uint8_t kind = consume_u8(" kind", tracer_);
    if (tracer_) {
      tracer_->Description(": ");
      tracer_->Description(TypeKindName(kind));
    }
    switch (kind) {
      case kWasmFunctionTypeCode: {
        const FunctionSig* sig = consume_sig(&module_->signature_zone);
        if (sig == nullptr) {
          CHECK(!ok());
          return {};
        }

        return {sig, kNoSuperType, is_final, is_shared};
      }
      case kWasmStructTypeCode: {
        module_->is_wasm_gc = true;
        const StructType* type =
            consume_struct(&module_->signature_zone, is_descriptor, is_shared);
        if (type == nullptr) {
          CHECK(!ok());
          return {};
        }
        return {type, kNoSuperType, is_final, is_shared};
      }
      case kWasmArrayTypeCode: {
        module_->is_wasm_gc = true;
        const ArrayType* type = consume_array(&module_->signature_zone);
        if (type == nullptr) {
          CHECK(!ok());
          return {};
        }
        return {type, kNoSuperType, is_final, is_shared};
      }
      case kWasmContTypeCode: {
        if (!enabled_features_.has_wasmfx()) {
          error(pc() - 1,
                "core stack switching not enabled (enable with "
                "--experimental-wasm-wasmfx)");
          return {};
        }

        const uint8_t* pos = pc();
        HeapType hp = consume_heap_type();

        if (!hp.is_index()) {
          error(pos, "cont type must refer to a signature index");
          return {};
        }

        ContType* type = module_->signature_zone.New<ContType>(hp.ref_index());
        return {type, kNoSuperType, is_final, is_shared};
      }
      default:
        if (tracer_) tracer_->NextLine();
        errorf(pc() - 1, "unknown type form: %d", kind);
        return {};
    }
  }

  TypeDefinition consume_described_type(bool is_descriptor, bool is_shared) {
    uint8_t kind = read_u8<Decoder::FullValidationTag>(pc(), "type kind");
    if (kind == kWasmDescriptorCode) {
      if (!enabled_features_.has_custom_descriptors()) {
        error(pc(),
              "descriptor types need --experimental-wasm-custom-descriptors");
        return {};
      }
      detected_features_->add_custom_descriptors();
      consume_bytes(1, "", tracer_);
      const uint8_t* pos = pc();
      uint32_t descriptor = consume_u32v(" descriptor", tracer_);
      if (descriptor >= module_->types.size()) {
        errorf(pos, "descriptor type index %u is out of bounds", descriptor);
        return {};
      }
      if (tracer_) tracer_->Description(descriptor);
      if (tracer_) tracer_->NextLine();
      TypeDefinition type =
          consume_base_type_definition(is_descriptor, is_shared);
      if (type.kind != TypeDefinition::kStruct) {
        error(pos - 1, "'descriptor' may only be used with structs");
        return {};
      }
      type.descriptor = ModuleTypeIndex{descriptor};
      return type;
    } else {
      return consume_base_type_definition(is_descriptor, is_shared);
    }
  }

  TypeDefinition consume_describing_type(size_t current_type_index,
                                         bool is_shared) {
    uint8_t kind = read_u8<Decoder::FullValidationTag>(pc(), "type kind");
    if (kind == kWasmDescribesCode) {
      if (!enabled_features_.has_custom_descriptors()) {
        error(pc(),
              "descriptor types need --experimental-wasm-custom-descriptors");
        return {};
      }
      detected_features_->add_custom_descriptors();
      consume_bytes(1, "", tracer_);
      const uint8_t* pos = pc();
      uint32_t describes = consume_u32v(" describes", tracer_);
      if (describes >= current_type_index) {
        error(pos, "types can only describe previously-declared types");
        return {};
      }
      if (tracer_) tracer_->Description(describes);
      if (tracer_) tracer_->NextLine();
      TypeDefinition type = consume_described_type(true, is_shared);
      if (type.kind != TypeDefinition::kStruct) {
        error(pos - 1, "'describes' may only be used with structs");
        return {};
      }
      type.describes = ModuleTypeIndex{describes};
      return type;
    } else {
      return consume_described_type(false, is_shared);
    }
  }

  TypeDefinition consume_shared_type(size_t current_type_index) {
    uint8_t kind = read_u8<Decoder::FullValidationTag>(pc(), "type kind");
    if (kind == kSharedFlagCode) {
      if (!enabled_features_.has_shared()) {
        errorf(pc() - 1,
               "unknown type form: %d, enable with --experimental-wasm-shared",
               kind);
        return {};
      }
      module_->has_shared_part = true;
      consume_bytes(1, " shared", tracer_);
      TypeDefinition type = consume_describing_type(current_type_index, true);
      DCHECK(type.is_shared);
      if (type.kind == TypeDefinition::kFunction ||
          type.kind == TypeDefinition::kCont) {
        // TODO(42204563): Support shared functions/continuations.
        error(pc() - 1, "shared functions/continuations are not supported yet");
        return {};
      }
      return type;
    } else {
      return consume_describing_type(current_type_index, false);
    }
  }

  // {current_type_index} is the index of the type that's being decoded.
  // Any supertype must have a lower index.
  TypeDefinition consume_subtype_definition(size_t current_type_index) {
    uint8_t kind = read_u8<Decoder::FullValidationTag>(pc(), "type kind");
    if (kind == kWasmSubtypeCode || kind == kWasmSubtypeFinalCode) {
      module_->is_wasm_gc = true;
      bool is_final = kind == kWasmSubtypeFinalCode;
      consume_bytes(1, is_final ? " subtype final, " : " subtype extensible, ",
                    tracer_);
      constexpr uint32_t kMaximumSupertypes = 1;
      uint32_t supertype_count =
          consume_count("supertype count", kMaximumSupertypes);
      uint32_t supertype = kNoSuperType.index;
      if (supertype_count == 1) {
        supertype = consume_u32v("supertype", tracer_);
        if (supertype >= current_type_index) {
          errorf("type %u: invalid supertype %u", current_type_index,
                 supertype);
          return {};
        }
        if (tracer_) {
          tracer_->Description(supertype);
          tracer_->NextLine();
        }
      }
      TypeDefinition type = consume_shared_type(current_type_index);
      type.supertype = ModuleTypeIndex{supertype};
      type.is_final = is_final;
      return type;
    } else {
      return consume_shared_type(current_type_index);
    }
  }

  void DecodeTypeSection() {
    TypeCanonicalizer* type_canon = GetTypeCanonicalizer();
    uint32_t types_count = consume_count("types count", kV8MaxWasmTypes);

    for (uint32_t i = 0; ok() && i < types_count; ++i) {
      TRACE("DecodeType[%d] module+%d\n", i, static_cast<int>(pc_ - start_));
      uint8_t kind = read_u8<Decoder::FullValidationTag>(pc(), "type kind");
      size_t initial_size = module_->types.size();
      uint32_t group_size = 1;
      if (kind == kWasmRecursiveTypeGroupCode) {
        module_->is_wasm_gc = true;
        uint32_t rec_group_offset = pc_offset();
        consume_bytes(1, "rec. group definition", tracer_);
        if (tracer_) tracer_->NextLine();
        group_size = consume_count("recursive group size", kV8MaxWasmTypes);
        if (tracer_) tracer_->RecGroupOffset(rec_group_offset, group_size);
      }
      if (initial_size + group_size > kV8MaxWasmTypes) {
        errorf(pc(), "Type definition count exceeds maximum %zu",
               kV8MaxWasmTypes);
        return;
      }
      // We need to resize types before decoding the type definitions in this
      // group, so that the correct type size is visible to type definitions.
      module_->types.resize(initial_size + group_size);
      for (uint32_t j = 0; j < group_size; j++) {
        if (tracer_) tracer_->TypeOffset(pc_offset());
        TypeDefinition type = consume_subtype_definition(initial_size + j);
        if (failed()) return;
        module_->types[initial_size + j] = type;
      }
      FinalizeRecgroup(group_size, type_canon);
      if (kind == kWasmRecursiveTypeGroupCode && tracer_) {
        tracer_->Description("end of rec. group");
        tracer_->NextLine();
      }
    }
  }

  void FinalizeRecgroup(uint32_t group_size, TypeCanonicalizer* type_canon) {
    // Now that we have decoded the entire recgroup, check its validity,
    // initialize additional data, and canonicalize it:
    // - check supertype validity
    // - propagate subtyping depths
    // - validate is_shared bits and set up RefTypeKind fields
    WasmModule* module = module_.get();
    uint32_t end_index = static_cast<uint32_t>(module->types.size());
    uint32_t start_index = end_index - group_size;
    for (uint32_t i = start_index; ok() && i < end_index; ++i) {
      TypeDefinition& type_def = module_->types[i];
      bool is_shared = type_def.is_shared;
      switch (type_def.kind) {
        case TypeDefinition::kFunction: {
          base::Vector<const ValueType> all = type_def.function_sig->all();
          size_t count = all.size();
          ValueType* storage = const_cast<ValueType*>(all.begin());
          for (uint32_t j = 0; j < count; j++) {
            value_type_reader::Populate(&storage[j], module);
            ValueType type = storage[j];
            if (is_shared && !type.is_shared()) {
              DCHECK(enabled_features_.has_shared());
              uint32_t retcount =
                  static_cast<uint32_t>(type_def.function_sig->return_count());
              const char* param_or_return =
                  j < retcount ? "return" : "parameter";
              uint32_t index = j < retcount ? j : j - retcount;
              // {pc_} isn't very accurate, it's pointing at the end of the
              // recgroup. So to ease debugging, we print the type's index.
              errorf(pc_,
                     "Type %u: shared signature must have shared %s types, "
                     "actual type for %s %d is %s",
                     i, param_or_return, param_or_return, index,
                     type.name().c_str());
              return;
            }
          }
          break;
        }
        case TypeDefinition::kStruct: {
          size_t count = type_def.struct_type->field_count();
          ValueType* storage =
              const_cast<ValueType*>(type_def.struct_type->fields().begin());
          for (uint32_t j = 0; j < count; j++) {
            value_type_reader::Populate(&storage[j], module);
            ValueType type = storage[j];
            if (is_shared && !type.is_shared()) {
              errorf(pc_,
                     "Type %u: shared struct must have shared field types, "
                     "actual type for field %d is %s",
                     i, j, type.name().c_str());
              return;
            }
          }
          if (type_def.descriptor.valid()) {
            const TypeDefinition& descriptor =
                module->type(type_def.descriptor);
            if (descriptor.describes.index != i) {
              uint32_t d = type_def.descriptor.index;
              errorf(pc_,
                     "Type %u has descriptor %u but %u doesn't describe %u", i,
                     d, d, i);
              return;
            }
            if (descriptor.is_shared != type_def.is_shared) {
              errorf(pc_,
                     "Type %u and its descriptor %u must have same sharedness",
                     i, type_def.descriptor.index);
              return;
            }
          }
          if (type_def.describes.valid()) {
            if (module->type(type_def.describes).descriptor.index != i) {
              uint32_t d = type_def.describes.index;
              errorf(pc_,
                     "Type %u describes %u but %u isn't a descriptor for %u", i,
                     d, d, i);
              return;
            }
          }
          break;
        }
        case TypeDefinition::kArray: {
          value_type_reader::Populate(
              const_cast<ArrayType*>(type_def.array_type)
                  ->element_type_writable_ptr(),
              module);
          ValueType type = type_def.array_type->element_type();
          if (is_shared && !type.is_shared()) {
            errorf(pc_,
                   "Type %u: shared array must have shared element type, "
                   "actual element type is %s",
                   i, type.name().c_str());
            return;
          }
          break;
        }
        case TypeDefinition::kCont: {
          ModuleTypeIndex contfun_typeid =
              type_def.cont_type->contfun_typeindex();
          const TypeDefinition contfun_type =
              module_->types[contfun_typeid.index];
          if (contfun_type.kind != TypeDefinition::kFunction) {
            errorf(pc_,
                   "Type %u: cont type must refer to a signature index, "
                   "actual type is %s",
                   i, module_->heap_type(contfun_typeid).name().c_str());
            return;
          }
          if (is_shared && !contfun_type.is_shared) {
            errorf(pc_,
                   "Type %u: shared cont type must refer to a shared signature,"
                   " actual type is %s",
                   module_->heap_type(contfun_typeid).name().c_str());
            return;
          }
          break;
        }
      }
    }
    module->isorecursive_canonical_type_ids.resize(end_index);
    type_canon->AddRecursiveGroup(module, group_size);
    for (uint32_t i = start_index; ok() && i < end_index; ++i) {
      TypeDefinition& type_def = module_->types[i];
      ModuleTypeIndex explicit_super = type_def.supertype;
      if (!explicit_super.valid()) continue;  // No supertype.
      DCHECK_LT(explicit_super.index, i);  // Checked during decoding.
      uint32_t depth = module->type(explicit_super).subtyping_depth + 1;
      type_def.subtyping_depth = depth;
      DCHECK_GE(depth, 0);
      if (depth > kV8MaxRttSubtypingDepth) {
        errorf("type %u: subtyping depth is greater than allowed", i);
        return;
      }
      // This check is technically redundant; we include for the improved error
      // message.
      if (module->type(explicit_super).is_final) {
        errorf("type %u extends final type %u", i, explicit_super.index);
        return;
      }
      if (!ValidSubtypeDefinition(ModuleTypeIndex{i}, explicit_super, module)) {
        errorf("type %u has invalid explicit supertype %u", i,
               explicit_super.index);
        return;
      }
    }
  }

  void DecodeImportSection() {
    uint32_t import_table_count =
        consume_count("imports count", kV8MaxWasmImports);
    module_->import_table.reserve(import_table_count);
    for (uint32_t i = 0; ok() && i < import_table_count; ++i) {
      TRACE("DecodeImportTable[%d] module+%d\n", i,
            static_cast<int>(pc_ - start_));
      if (tracer_) tracer_->ImportOffset(pc_offset());

      const uint8_t* pos = pc_;
      WireBytesRef module_name =
          consume_utf8_string(this, "module name", tracer_);
      WireBytesRef field_name =
          consume_utf8_string(this, "field name", tracer_);
      ImportExportKindCode kind =
          static_cast<ImportExportKindCode>(consume_u8("kind", tracer_));
      if (tracer_) {
        tracer_->Description(": ");
        tracer_->Description(ExternalKindName(kind));
      }
      module_->import_table.push_back(WasmImport{
          .module_name = module_name, .field_name = field_name, .kind = kind});
      WasmImport* import = &module_->import_table.back();
      switch (kind) {
        case kExternalFunction: {
          // ===== Imported function ===========================================
          import->index = static_cast<uint32_t>(module_->functions.size());
          module_->num_imported_functions++;
          module_->functions.push_back(WasmFunction{
              .func_index = import->index,
              .imported = true,
          });
          WasmFunction* function = &module_->functions.back();
          function->sig_index =
              consume_sig_index(module_.get(), &function->sig);
          break;
        }
        case kExternalTable: {
          // ===== Imported table ==============================================
          import->index = static_cast<uint32_t>(module_->tables.size());
          const uint8_t* type_position = pc();
          ValueType type = consume_value_type(module_.get());
          if (!type.is_object_reference()) {
            errorf(type_position, "Invalid table type %s", type.name().c_str());
            break;
          }
          module_->num_imported_tables++;
          module_->tables.push_back(WasmTable{
              .type = type,
              .imported = true,
          });
          WasmTable* table = &module_->tables.back();
          consume_table_flags(table);
          DCHECK_IMPLIES(table->shared,
                         enabled_features_.has_shared() || !ok());
          if (table->shared && enabled_features_.has_shared()) {
            module_->has_shared_part = true;
            if (!type.is_shared()) {
              errorf(type_position,
                     "Shared table %i must have shared element type, actual "
                     "type %s",
                     i, type.name().c_str());
              break;
            }
          }
          // Note that we should not throw an error if the declared maximum size
          // is oob. We will instead fail when growing at runtime.
          uint64_t kNoMaximum = kMaxUInt64;
          consume_resizable_limits(
              "table", "elements", wasm::max_table_size(), &table->initial_size,
              table->has_maximum_size, kNoMaximum, &table->maximum_size,
              table->is_table64() ? k64BitLimits : k32BitLimits);
          break;
        }
        case kExternalMemory: {
          // ===== Imported memory =============================================
          static_assert(kV8MaxWasmMemories <= kMaxUInt32);
          if (module_->memories.size() >= kV8MaxWasmMemories - 1) {
            errorf("At most %u imported memories are supported",
                   kV8MaxWasmMemories);
            break;
          }
          uint32_t mem_index = static_cast<uint32_t>(module_->memories.size());
          import->index = mem_index;
          module_->memories.emplace_back();
          WasmMemory* external_memory = &module_->memories.back();
          external_memory->imported = true;
          external_memory->index = mem_index;

          consume_memory_flags(external_memory);
          uint32_t max_pages = external_memory->is_memory64()
                                   ? kSpecMaxMemory64Pages
                                   : kSpecMaxMemory32Pages;
          consume_resizable_limits(
              "memory", "pages", max_pages, &external_memory->initial_pages,
              external_memory->has_maximum_pages, max_pages,
              &external_memory->maximum_pages,
              external_memory->is_memory64() ? k64BitLimits : k32BitLimits);
          break;
        }
        case kExternalGlobal: {
          // ===== Imported global =============================================
          import->index = static_cast<uint32_t>(module_->globals.size());
          ValueType type = consume_value_type(module_.get());
          auto [mutability, shared] = consume_global_flags();
          if (V8_UNLIKELY(failed())) break;
          if (V8_UNLIKELY(shared && !type.is_shared())) {
            error("shared imported global must have shared type");
            break;
          }
          module_->globals.push_back(
              WasmGlobal{.type = type,
                         .mutability = mutability,
                         .index = 0,  // set later in CalculateGlobalOffsets
                         .shared = shared,
                         .imported = true});
          module_->num_imported_globals++;
          DCHECK_EQ(module_->globals.size(), module_->num_imported_globals);
          if (shared) module_->has_shared_part = true;
          if (mutability) module_->num_imported_mutable_globals++;
          if (tracer_) tracer_->NextLine();
          break;
        }
        case kExternalTag: {
          // ===== Imported tag ================================================
          import->index = static_cast<uint32_t>(module_->tags.size());
          module_->num_imported_tags++;
          const WasmTagSig* tag_sig = nullptr;
          consume_exception_attribute();  // Attribute ignored for now.
          ModuleTypeIndex sig_index =
              consume_tag_sig_index(module_.get(), &tag_sig);
          module_->tags.emplace_back(tag_sig, sig_index);
          break;
        }
        default:
          errorf(pos, "unknown import kind 0x%02x", kind);
          break;
      }
    }
    if (module_->memories.size() > 1) {
      detected_features_->add_multi_memory();
      if (v8_flags.wasm_jitless) {
        error("Multiple memories not supported in Wasm jitless mode");
      }
    }
    if (module_->num_imported_mutable_globals > 0) {
      detected_features_->add_mutable_globals();
    }
    UpdateComputedMemoryInformation();
    module_->type_feedback.well_known_imports.Initialize(
        module_->num_imported_functions);
    if (tracer_) tracer_->ImportsDone(module_.get());
  }

  void DecodeFunctionSection() {
    uint32_t functions_count =
        consume_count("functions count", v8_flags.max_wasm_functions);
    DCHECK_EQ(module_->functions.size(), module_->num_imported_functions);
    uint32_t total_function_count =
        module_->num_imported_functions + functions_count;
    module_->functions.resize(total_function_count);
    module_->num_declared_functions = functions_count;
    // Also initialize the {validated_functions} bitset here, now that we know
    // the number of declared functions.
    DCHECK_NULL(module_->validated_functions);
    module_->validated_functions =
        std::make_unique<std::atomic<uint8_t>[]>((functions_count + 7) / 8);
    if (is_asmjs_module(module_.get())) {
      // Mark all asm.js functions as valid by design (it's faster to do this
      // here than to check this in {WasmModule::function_was_validated}).
      std::fill_n(module_->validated_functions.get(), (functions_count + 7) / 8,
                  0xff);
    }

    for (uint32_t func_index = module_->num_imported_functions;
         func_index < total_function_count; ++func_index) {
      WasmFunction* function = &module_->functions[func_index];
      function->func_index = func_index;
      if (tracer_) tracer_->FunctionName(func_index);
      function->sig_index = consume_sig_index(module_.get(), &function->sig);
      if (!ok()) return;
    }
  }

  void DecodeTableSection() {
    static_assert(kV8MaxWasmTables <= kMaxUInt32);
    uint32_t table_count = consume_count("table count", kV8MaxWasmTables);

    for (uint32_t i = 0; ok() && i < table_count; i++) {
      if (tracer_) tracer_->TableOffset(pc_offset());
      module_->tables.emplace_back();
      WasmTable* table = &module_->tables.back();
      const uint8_t* type_position = pc();

      bool has_initializer = false;
      if (read_u8<Decoder::FullValidationTag>(
              pc(), "table-with-initializer byte") == 0x40) {
        consume_bytes(1, "with-initializer ", tracer_);
        has_initializer = true;
        type_position++;
        uint8_t reserved = consume_u8("reserved-byte", tracer_);
        if (reserved != 0) {
          error(type_position, "Reserved byte must be 0x00");
          break;
        }
        type_position++;
      }

      ValueType table_type = consume_value_type(module_.get());
      if (!table_type.is_object_reference()) {
        error(type_position, "Only reference types can be used as table types");
        break;
      }
      if (!has_initializer && !table_type.is_defaultable()) {
        errorf(type_position,
               "Table of non-defaultable table %s needs initial value",
               table_type.name().c_str());
        break;
      }
      table->type = table_type;

      consume_table_flags(table);
      DCHECK_IMPLIES(table->shared, enabled_features_.has_shared() || !ok());
      if (table->shared && enabled_features_.has_shared()) {
        module_->has_shared_part = true;
        if (!table_type.is_shared()) {
          errorf(
              type_position,
              "Shared table %i must have shared element type, actual type %s",
              i + module_->num_imported_tables, table_type.name().c_str());
          break;
        }
      }
      // Note that we should not throw an error if the declared maximum size is
      // oob. We will instead fail when growing at runtime.
      uint64_t kNoMaximum = kMaxUInt64;
      consume_resizable_limits(
          "table", "elements", wasm::max_table_size(), &table->initial_size,
          table->has_maximum_size, kNoMaximum, &table->maximum_size,
          table->is_table64() ? k64BitLimits : k32BitLimits);

      if (has_initializer) {
        table->initial_value =
            consume_init_expr(module_.get(), table_type, table->shared);
      }
    }
  }

  void DecodeMemorySection() {
    const uint8_t* mem_count_pc = pc();
    static_assert(kV8MaxWasmMemories <= kMaxUInt32);
    // Use {kV8MaxWasmMemories} here, but only allow for >1 memory if
    // multi-memory is enabled (checked below). This allows for better error
    // messages.
    uint32_t memory_count = consume_count("memory count", kV8MaxWasmMemories);
    size_t imported_memories = module_->memories.size();
    DCHECK_GE(kV8MaxWasmMemories, imported_memories);
    if (memory_count > kV8MaxWasmMemories - imported_memories) {
      errorf(mem_count_pc,
             "Exceeding maximum number of memories (%u; declared %u, "
             "imported %zu)",
             kV8MaxWasmMemories, memory_count, imported_memories);
    }
    module_->memories.resize(imported_memories + memory_count);

    for (uint32_t i = 0; ok() && i < memory_count; i++) {
      WasmMemory* memory = module_->memories.data() + imported_memories + i;
      memory->index = static_cast<uint32_t>(imported_memories + i);
      if (tracer_) tracer_->MemoryOffset(pc_offset());
      consume_memory_flags(memory);
      uint32_t max_pages =
          memory->is_memory64() ? kSpecMaxMemory64Pages : kSpecMaxMemory32Pages;
      consume_resizable_limits(
          "memory", "pages", max_pages, &memory->initial_pages,
          memory->has_maximum_pages, max_pages, &memory->maximum_pages,
          memory->is_memory64() ? k64BitLimits : k32BitLimits);
    }
    if (module_->memories.size() > 1) {
      detected_features_->add_multi_memory();
      if (v8_flags.wasm_jitless) {
        error("Multiple memories not supported in Wasm jitless mode");
      }
    }
    UpdateComputedMemoryInformation();
  }

  void UpdateComputedMemoryInformation() {
    for (WasmMemory& memory : module_->memories) {
      UpdateComputedInformation(&memory, module_->origin);
    }
  }

  void DecodeGlobalSection() {
    uint32_t globals_count = consume_count("globals count", kV8MaxWasmGlobals);
    uint32_t imported_globals = static_cast<uint32_t>(module_->globals.size());
    // It is important to not resize the globals vector from the beginning,
    // because we use its current size when decoding the initializer.
    module_->globals.reserve(imported_globals + globals_count);
    for (uint32_t i = 0; ok() && i < globals_count; ++i) {
      TRACE("DecodeGlobal[%d] module+%d\n", i, static_cast<int>(pc_ - start_));
      if (tracer_) tracer_->GlobalOffset(pc_offset());
      const uint8_t* pos = pc_;
      ValueType type = consume_value_type(module_.get());
      auto [mutability, shared] = consume_global_flags();
      if (failed()) return;
      if (shared && !type.is_shared()) {
        CHECK(enabled_features_.has_shared());
        errorf(pos, "Shared global %i must have shared type, actual type %s",
               i + imported_globals, type.name().c_str());
        return;
      }
      // Validation that {type} and {shared} are compatible will happen in
      // {consume_init_expr}.
      bool ends_with_struct_new = false;
      ConstantExpression init =
          consume_init_expr(module_.get(), type, shared, &ends_with_struct_new);
      module_->globals.push_back(
          WasmGlobal{.type = type,
                     .mutability = mutability,
                     .init = init,
                     .index = 0,  // set later in CalculateGlobalOffsets
                     .shared = shared,
                     .initializer_ends_with_struct_new = ends_with_struct_new});
      if (shared) module_->has_shared_part = true;
    }
  }

  void DecodeExportSection() {
    uint32_t export_table_count =
        consume_count("exports count", kV8MaxWasmExports);
    module_->export_table.reserve(export_table_count);
    for (uint32_t i = 0; ok() && i < export_table_count; ++i) {
      TRACE("DecodeExportTable[%d] module+%d\n", i,
            static_cast<int>(pc_ - start_));
      if (tracer_) {
        tracer_->Description("export #");
        tracer_->Description(i);
        tracer_->NextLine();
      }

      WireBytesRef name = consume_utf8_string(this, "field name", tracer_);

      const uint8_t* kind_pos = pc();
      ImportExportKindCode kind =
          static_cast<ImportExportKindCode>(consume_u8("kind", tracer_));

      module_->export_table.push_back(WasmExport{.name = name, .kind = kind});
      WasmExport* exp = &module_->export_table.back();

      if (tracer_) {
        tracer_->Description(": ");
        tracer_->Description(ExternalKindName(exp->kind));
        tracer_->Description(" ");
      }
      switch (kind) {
        case kExternalFunction: {
          WasmFunction* func = nullptr;
          exp->index = consume_func_index(module_.get(), &func);

          if (failed()) break;
          DCHECK_NOT_NULL(func);

          module_->num_exported_functions++;
          func->exported = true;
          // Exported functions are considered "declared".
          func->declared = true;
          break;
        }
        case kExternalTable: {
          WasmTable* table = nullptr;
          exp->index = consume_table_index(module_.get(), &table);
          if (table) table->exported = true;
          break;
        }
        case kExternalMemory: {
          const uint8_t* index_pos = pc();
          exp->index = consume_u32v("memory index", tracer_);
          size_t num_memories = module_->memories.size();
          if (exp->index >= module_->memories.size()) {
            errorf(index_pos,
                   "invalid exported memory index %u (having %zu memor%s)",
                   exp->index, num_memories, num_memories == 1 ? "y" : "ies");
            break;
          }
          module_->memories[exp->index].exported = true;
          break;
        }
        case kExternalGlobal: {
          WasmGlobal* global = nullptr;
          exp->index = consume_global_index(module_.get(), &global);
          if (global) {
            global->exported = true;
          }
          break;
        }
        case kExternalTag: {
          WasmTag* tag = nullptr;
          exp->index = consume_tag_index(module_.get(), &tag);
          break;
        }
        default:
          errorf(kind_pos, "invalid export kind 0x%02x", exp->kind);
          break;
      }
      if (tracer_) tracer_->NextLine();
    }
    // Check for duplicate exports (except for asm.js).
    if (ok() && module_->origin == kWasmOrigin &&
        module_->export_table.size() > 1) {
      std::vector<WasmExport> sorted_exports(module_->export_table);

      auto cmp_less = [this](const WasmExport& a, const WasmExport& b) {
        // Return true if a < b.
        if (a.name.length() != b.name.length()) {
          return a.name.length() < b.name.length();
        }
        const uint8_t* left =
            start() + GetBufferRelativeOffset(a.name.offset());
        const uint8_t* right =
            start() + GetBufferRelativeOffset(b.name.offset());
        return memcmp(left, right, a.name.length()) < 0;
      };
      std::stable_sort(sorted_exports.begin(), sorted_exports.end(), cmp_less);

      auto it = sorted_exports.begin();
      WasmExport* last = &*it++;
      for (auto end = sorted_exports.end(); it != end; last = &*it++) {
        DCHECK(!cmp_less(*it, *last));  // Vector must be sorted.
        if (!cmp_less(*last, *it)) {
          const uint8_t* pc =
              start() + GetBufferRelativeOffset(it->name.offset());
          TruncatedUserString<> name(pc, it->name.length());
          errorf(pc, "Duplicate export name '%.*s' for %s %d and %s %d",
                 name.length(), name.start(), ExternalKindName(last->kind),
                 last->index, ExternalKindName(it->kind), it->index);
          break;
        }
      }
    }
  }

  void DecodeStartSection() {
    if (tracer_) tracer_->StartOffset(pc_offset());
    WasmFunction* func;
    const uint8_t* pos = pc_;
    module_->start_function_index = consume_func_index(module_.get(), &func);
    if (tracer_) tracer_->NextLine();
    if (func &&
        (func->sig->parameter_count() > 0 || func->sig->return_count() > 0)) {
      error(pos, "invalid start function: non-zero parameter or return count");
    }
  }

  void DecodeElementSection() {
    uint32_t segment_count =
        consume_count("segment count", wasm::max_table_size());

    for (uint32_t i = 0; i < segment_count; ++i) {
      if (tracer_) tracer_->ElementOffset(pc_offset());
      WasmElemSegment segment = consume_element_segment_header();
      if (tracer_) tracer_->NextLineIfNonEmpty();
      if (failed()) return;
      DCHECK_NE(segment.type, kWasmBottom);

      for (uint32_t j = 0; j < segment.element_count; j++) {
        // Just run validation on elements; do not store them anywhere. We will
        // decode them again from wire bytes as needed.
        consume_element_segment_entry(module_.get(), segment);
        if (failed()) return;
      }
      module_->elem_segments.push_back(std::move(segment));
    }
  }

  void DecodeCodeSection() {
    // Make sure global offset were calculated before they get accessed during
    // function compilation.
    CalculateGlobalOffsets(module_.get());
    uint32_t code_section_start = pc_offset();
    uint32_t functions_count = consume_u32v("functions count", tracer_);
    if (tracer_) {
      tracer_->Description(functions_count);
      tracer_->NextLine();
    }
    CheckFunctionsCount(functions_count, code_section_start);

    auto inst_traces_it = this->inst_traces_.begin();
    std::vector<std::pair<uint32_t, uint32_t>> inst_traces;

    for (uint32_t i = 0; ok() && i < functions_count; ++i) {
      int function_index = module_->num_imported_functions + i;
      if (tracer_) {
        tracer_->Description("function #");
        tracer_->FunctionName(function_index);
        tracer_->NextLine();
      }
      const uint8_t* pos = pc();
      uint32_t size = consume_u32v("body size", tracer_);
      if (tracer_) {
        tracer_->Description(size);
        tracer_->NextLine();
      }
      if (size > kV8MaxWasmFunctionSize) {
        errorf(pos, "size %u > maximum function size %zu", size,
               kV8MaxWasmFunctionSize);
        return;
      }
      uint32_t offset = pc_offset();
      consume_bytes(size, "function body");
      if (failed()) break;
      DecodeFunctionBody(function_index, size, offset);

      // Now that the function has been decoded, we can compute module offsets.
      for (; inst_traces_it != this->inst_traces_.end() &&
             std::get<0>(*inst_traces_it) == i;
           ++inst_traces_it) {
        uint32_t trace_offset = offset + std::get<1>(*inst_traces_it);
        uint32_t mark_id = std::get<2>(*inst_traces_it);
        std::pair<uint32_t, uint32_t> trace_mark = {trace_offset, mark_id};
        inst_traces.push_back(trace_mark);
      }
    }
    // If we have actually decoded traces and they were all decoded without
    // error, then we can move them to the module. If any errors are found, it
    // is safe to throw away all traces.
    if (V8_UNLIKELY(!inst_traces.empty() &&
                    inst_traces_it == this->inst_traces_.end())) {
      // This adds an invalid entry at the end of the traces. An invalid entry
      // is defined as having an module offset of 0 and a markid of 0.
      inst_traces.push_back({0, 0});
      this->module_->inst_traces = std::move(inst_traces);
    }
    DCHECK_GE(pc_offset(), code_section_start);
    module_->code = {code_section_start, pc_offset() - code_section_start};
  }

  void StartCodeSection(WireBytesRef section_bytes) {
    CheckSectionOrder(kCodeSectionCode);
    // Make sure global offset were calculated before they get accessed during
    // function compilation.
    CalculateGlobalOffsets(module_.get());
    module_->code = section_bytes;
  }

  bool CheckFunctionsCount(uint32_t functions_count, uint32_t error_offset) {
    if (functions_count != module_->num_declared_functions) {
      errorf(error_offset, "function body count %u mismatch (%u expected)",
             functions_count, module_->num_declared_functions);
      return false;
    }
    return true;
  }

  void DecodeFunctionBody(uint32_t func_index, uint32_t length,
                          uint32_t offset) {
    WasmFunction* function = &module_->functions[func_index];
    function->code = {offset, length};
    constexpr uint32_t kSmallFunctionThreshold = 50;
    if (length < kSmallFunctionThreshold) {
      ++module_->num_small_functions;
    }
    if (tracer_) {
      tracer_->FunctionBody(function, pc_ - (pc_offset() - offset));
    }
  }

  bool CheckDataSegmentsCount(uint32_t data_segments_count) {
    if (has_seen_unordered_section(kDataCountSectionCode) &&
        data_segments_count != module_->num_declared_data_segments) {
      errorf(pc(), "data segments count %u mismatch (%u expected)",
             data_segments_count, module_->num_declared_data_segments);
      return false;
    }
    return true;
  }

  struct DataSegmentHeader {
    bool is_active;
    bool is_shared;
    uint32_t memory_index;
    ConstantExpression dest_addr;
  };

  void DecodeDataSection() {
    uint32_t data_segments_count =
        consume_count("data segments count", kV8MaxWasmDataSegments);
    if (!CheckDataSegmentsCount(data_segments_count)) return;

    module_->data_segments.reserve(data_segments_count);
    for (uint32_t i = 0; i < data_segments_count; ++i) {
      TRACE("DecodeDataSegment[%d] module+%d\n", i,
            static_cast<int>(pc_ - start_));
      if (tracer_) tracer_->DataOffset(pc_offset());

      DataSegmentHeader header = consume_data_segment_header();

      uint32_t source_length = consume_u32v("source size", tracer_);
      if (tracer_) {
        tracer_->Description(source_length);
        tracer_->NextLine();
      }
      uint32_t source_offset = pc_offset();

      if (tracer_) {
        tracer_->Bytes(pc_, source_length);
        tracer_->Description("segment data");
        tracer_->NextLine();
      }
      consume_bytes(source_length, "segment data");

      if (failed()) break;
      module_->data_segments.emplace_back(
          header.is_active, header.is_shared, header.memory_index,
          header.dest_addr, WireBytesRef{source_offset, source_length});
    }
  }

  void DecodeNameSection() {
    if (tracer_) {
      tracer_->NameSection(
          pc_, end_, buffer_offset_ + static_cast<uint32_t>(pc_ - start_));
    }
    // TODO(titzer): find a way to report name errors as warnings.
    // Ignore all but the first occurrence of name section.
    if (!has_seen_unordered_section(kNameSectionCode)) {
      set_seen_unordered_section(kNameSectionCode);
      module_->name_section = {buffer_offset_,
                               static_cast<uint32_t>(end_ - start_)};
      // Use an inner decoder so that errors don't fail the outer decoder.
      Decoder inner(start_, pc_, end_, buffer_offset_);
      // Decode all name subsections.
      // Be lenient with their order.
      while (inner.ok() && inner.more()) {
        uint8_t name_type = inner.consume_u8("name type");
        if (name_type & 0x80) inner.error("name type if not varuint7");

        uint32_t name_payload_len = inner.consume_u32v("name payload length");
        if (!inner.checkAvailable(name_payload_len)) break;

        // Decode module name, ignore the rest.
        // Function and local names will be decoded when needed.
        if (name_type == NameSectionKindCode::kModuleCode) {
          WireBytesRef name =
              consume_string(&inner, unibrow::Utf8Variant::kLossyUtf8,
                             "module name", ITracer::NoTrace);
          if (inner.ok() && validate_utf8(&inner, name)) {
            module_->name = name;
          }
        } else {
          inner.consume_bytes(name_payload_len, "name subsection payload");
        }
      }
    }
    // Skip the whole names section in the outer decoder.
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeDescriptorsSection() {
    if (enabled_features_.has_custom_descriptors() &&
        !has_seen_unordered_section(kDescriptorsSectionCode)) {
      set_seen_unordered_section(kDescriptorsSectionCode);
      module_->descriptors_section = {buffer_offset_,
                                      static_cast<uint32_t>(end_ - start_)};
    }
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeSourceMappingURLSection() {
    Decoder inner(start_, pc_, end_, buffer_offset_);
    WireBytesRef url =
        wasm::consume_utf8_string(&inner, "module name", tracer_);
    if (inner.ok() &&
        module_->debug_symbols[WasmDebugSymbols::Type::SourceMap].type ==
            WasmDebugSymbols::None) {
      module_->debug_symbols[WasmDebugSymbols::Type::SourceMap] = {
          WasmDebugSymbols::Type::SourceMap, url};
    }
    set_seen_unordered_section(kSourceMappingURLSectionCode);
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeExternalDebugInfoSection() {
    Decoder inner(start_, pc_, end_, buffer_offset_);
    WireBytesRef url =
        wasm::consume_utf8_string(&inner, "external symbol file", tracer_);
    if (inner.ok()) {
      module_->debug_symbols[WasmDebugSymbols::Type::ExternalDWARF] = {
          WasmDebugSymbols::Type::ExternalDWARF, url};
      set_seen_unordered_section(kExternalDebugInfoSectionCode);
    }
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeBuildIdSection() {
    Decoder inner(start_, pc_, end_, buffer_offset_);
    WireBytesRef build_id =
        consume_string(&inner, unibrow::Utf8Variant::kLossyUtf8, "build_id");
    if (inner.ok()) {
      module_->build_id = build_id;
      set_seen_unordered_section(kBuildIdSectionCode);
    }
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeInstTraceSection() {
    TRACE("DecodeInstTrace module+%d\n", static_cast<int>(pc_ - start_));
    if (!has_seen_unordered_section(kInstTraceSectionCode)) {
      set_seen_unordered_section(kInstTraceSectionCode);

      // Use an inner decoder so that errors don't fail the outer decoder.
      Decoder inner(start_, pc_, end_, buffer_offset_);

      std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> inst_traces;

      uint32_t func_count = inner.consume_u32v("number of functions");
      // Keep track of the previous function index to validate the ordering.
      int64_t last_func_idx = -1;
      for (uint32_t i = 0; i < func_count; i++) {
        uint32_t func_idx = inner.consume_u32v("function index");
        if (int64_t{func_idx} <= last_func_idx) {
          inner.errorf("Invalid function index: %d", func_idx);
          break;
        }
        last_func_idx = func_idx;

        uint32_t num_traces = inner.consume_u32v("number of trace marks");
        TRACE("DecodeInstTrace[%d] module+%d\n", func_idx,
              static_cast<int>(inner.pc() - inner.start()));
        // Keep track of the previous offset to validate the ordering.
        int64_t last_func_off = -1;
        for (uint32_t j = 0; j < num_traces; ++j) {
          uint32_t func_off = inner.consume_u32v("function offset");

          uint32_t mark_size = inner.consume_u32v("mark size");
          uint32_t trace_mark_id = 0;
          // Build the mark id from the individual bytes.
          for (uint32_t k = 0; k < mark_size; k++) {
            trace_mark_id |= inner.consume_u8("trace mark id") << k * 8;
          }
          if (int64_t{func_off} <= last_func_off) {
            inner.errorf("Invalid branch offset: %d", func_off);
            break;
          }
          last_func_off = func_off;
          TRACE("DecodeInstTrace[%d][%d] module+%d\n", func_idx, func_off,
                static_cast<int>(inner.pc() - inner.start()));
          // Store the function index, function offset, and mark id into a
          // temporary 3-tuple. This will later be translated to a module
          // offset and  mark id.
          std::tuple<uint32_t, uint32_t, uint32_t> mark_tuple = {
              func_idx, func_off, trace_mark_id};
          inst_traces.push_back(mark_tuple);
        }
      }
      // Extra unexpected bytes are an error.
      if (inner.more()) {
        inner.errorf("Unexpected extra bytes: %d\n",
                     static_cast<int>(inner.pc() - inner.start()));
      }
      // If everything went well, accept the traces for the module.
      if (inner.ok()) {
        this->inst_traces_ = std::move(inst_traces);
      }
    }

    // Skip the whole instruction trace section in the outer decoder.
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeBranchHintsSection() {
    TRACE("DecodeBranchHints module+%d\n", static_cast<int>(pc_ - start_));
    detected_features_->add_branch_hinting();
    if (!has_seen_unordered_section(kBranchHintsSectionCode)) {
      set_seen_unordered_section(kBranchHintsSectionCode);
      // Use an inner decoder so that errors don't fail the outer decoder.
      Decoder inner(start_, pc_, end_, buffer_offset_);
      BranchHintInfo branch_hints;

      uint32_t func_count = inner.consume_u32v("number of functions");
      // Keep track of the previous function index to validate the ordering
      int64_t last_func_idx = -1;
      for (uint32_t i = 0; i < func_count; i++) {
        uint32_t func_idx = inner.consume_u32v("function index");
        if (int64_t{func_idx} <= last_func_idx) {
          inner.errorf("Invalid function index: %d", func_idx);
          break;
        }
        last_func_idx = func_idx;
        uint32_t num_hints = inner.consume_u32v("number of hints");
        BranchHintMap func_branch_hints;
        TRACE("DecodeBranchHints[%d] module+%d\n", func_idx,
              static_cast<int>(inner.pc() - inner.start()));
        // Keep track of the previous branch offset to validate the ordering
        int64_t last_br_off = -1;
        for (uint32_t j = 0; j < num_hints; ++j) {
          uint32_t br_off = inner.consume_u32v("branch instruction offset");
          if (int64_t{br_off} <= last_br_off) {
            inner.errorf("Invalid branch offset: %d", br_off);
            break;
          }
          last_br_off = br_off;
          uint32_t data_size = inner.consume_u32v("data size");
          if (data_size != 1) {
            inner.errorf("Invalid data size: %#x. Expected 1.", data_size);
            break;
          }
          uint32_t br_dir = inner.consume_u8("branch direction");
          TRACE("DecodeBranchHints[%d][%d] module+%d\n", func_idx, br_off,
                static_cast<int>(inner.pc() - inner.start()));
          BranchHint hint;
          switch (br_dir) {
            case 0:
              hint = BranchHint::kFalse;
              break;
            case 1:
              hint = BranchHint::kTrue;
              break;
            default:
              hint = BranchHint::kNone;
              inner.errorf(inner.pc(), "Invalid branch hint %#x", br_dir);
              break;
          }
          if (!inner.ok()) {
            break;
          }
          func_branch_hints.insert(br_off, hint);
        }
        if (!inner.ok()) {
          break;
        }
        branch_hints.emplace(func_idx, std::move(func_branch_hints));
      }
      // Extra unexpected bytes are an error.
      if (inner.more()) {
        inner.errorf("Unexpected extra bytes: %d\n",
                     static_cast<int>(inner.pc() - inner.start()));
      }
      // If everything went well, accept the hints for the module.
      if (inner.ok()) {
        module_->branch_hints = std::move(branch_hints);
      } else {
        TRACE("DecodeBranchHints error: %s", inner.error().message().c_str());
      }
    }
    // Skip the whole branch hints section in the outer decoder.
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeCompilationPrioritySection() {
    TRACE("DecodeCompilationPriority module+%d\n",
          static_cast<int>(pc_ - start_));
    detected_features_->add_compilation_hints();
    if (!has_seen_unordered_section(kCompilationPrioritySectionCode)) {
      set_seen_unordered_section(kCompilationPrioritySectionCode);
      // Use an inner decoder so that errors don't fail the outer decoder.
      Decoder inner(start_, pc_, end_, buffer_offset_);
      CompilationPriorities compilation_priorities;

      uint32_t func_count = inner.consume_u32v("number of functions");

      int64_t last_func_index = -1;

      for (uint32_t i = 0; i < func_count; i++) {
        uint32_t func_index = inner.consume_u32v("function index");
        if (static_cast<int64_t>(func_index) <= last_func_index) {
          inner.error("out of order functions");
          break;
        }
        last_func_index = func_index;
        uint32_t byte_offset = inner.consume_u32v("byte offset");
        if (byte_offset != 0) {
          inner.error("byte offset has to be 0 (function level only)");
          break;
        }
        uint32_t hint_length = inner.consume_u32v("hint length");
        const uint8_t* pc_after_hint_length = inner.pc();
        uint32_t compilation_priority =
            inner.consume_u32v("compilation priority");
        if (static_cast<int64_t>(hint_length) <
            inner.pc() - pc_after_hint_length) {
          inner.error("Compilation priority longer than declared hint length");
          break;
        }
        int optimization_priority = kOptimizationPriorityNotSpecifiedSentinel;

        // If we exhausted the declared hint length, do not parse optimization
        // priority.
        if (static_cast<int64_t>(hint_length) >
            inner.pc() - pc_after_hint_length) {
          uint32_t parsed_priority =
              inner.consume_u32v("optimization priority");
          if (static_cast<int>(parsed_priority) >
              kOptimizationPriorityExecutedOnceSentinel) {
            inner.error("Optimization priority too large");
            break;
          }
          optimization_priority = static_cast<int>(parsed_priority);
          if (static_cast<int64_t>(hint_length) <
              inner.pc() - pc_after_hint_length) {
            inner.error("Optimization priority overflows declared hint length");
            break;
          }
        }

        // Ignore remaining hint bytes.
        inner.consume_bytes(
            hint_length -
            static_cast<uint32_t>(inner.pc() - pc_after_hint_length));

        if (!inner.ok()) break;

        compilation_priorities.emplace(
            func_index,
            CompilationPriority{compilation_priority, optimization_priority});
      }

      // Extra unexpected bytes are an error.
      if (inner.more()) {
        inner.errorf("Unexpected extra bytes: %d\n",
                     static_cast<int>(inner.pc() - inner.start()));
      }
      // If everything went well, accept the compilation priority hints for the
      // module.
      if (inner.ok()) {
        module_->compilation_priorities = std::move(compilation_priorities);
      } else {
        TRACE("DecodeCompilationPriority error: %s",
              inner.error().message().c_str());
      }
    }
    // Skip the whole branch hints section in the outer decoder.
    consume_bytes(static_cast<uint32_t>(end_ - start_), nullptr);
  }

  void DecodeDataCountSection() {
    module_->num_declared_data_segments =
        consume_count("data segments count", kV8MaxWasmDataSegments);
    if (tracer_) tracer_->NextLineIfNonEmpty();
  }

  void DecodeTagSection() {
    uint32_t tag_count = consume_count("tag count", kV8MaxWasmTags);
    for (uint32_t i = 0; ok() && i < tag_count; ++i) {
      TRACE("DecodeTag[%d] module+%d\n", i, static_cast<int>(pc_ - start_));
      if (tracer_) tracer_->TagOffset(pc_offset());
      const WasmTagSig* tag_sig = nullptr;
      consume_exception_attribute();  // Attribute ignored for now.
      ModuleTypeIndex sig_index =
          consume_tag_sig_index(module_.get(), &tag_sig);
      module_->tags.emplace_back(tag_sig, sig_index);
    }
  }

  void DecodeStringRefSection() {
    uint32_t deferred = consume_count("deferred string literal count",
                                      kV8MaxWasmStringLiterals);
    if (deferred) {
      errorf(pc(), "Invalid deferred string literal count %u (expected 0)",
             deferred);
    }
    uint32_t immediate = consume_count("string literal count",
                                       kV8MaxWasmStringLiterals - deferred);
    for (uint32_t i = 0; ok() && i < immediate; ++i) {
      TRACE("DecodeStringLiteral[%d] module+%d\n", i,
            static_cast<int>(pc_ - start_));
      if (tracer_) tracer_->StringOffset(pc_offset());
      // TODO(12868): Throw if the string's utf-16 length > String::kMaxLength.
      WireBytesRef pos = wasm::consume_string(this, unibrow::Utf8Variant::kWtf8,
                                              "string literal", tracer_);
      module_->stringref_literals.emplace_back(pos);
    }
  }

  bool CheckMismatchedCounts() {
    // The declared vs. defined function count is normally checked when
    // decoding the code section, but we have to check it here too in case the
    // code section is absent.
    if (module_->num_declared_functions != 0) {
      DCHECK_LT(module_->num_imported_functions, module_->functions.size());
      // We know that the code section has been decoded if the first
      // non-imported function has its code set.
      if (!module_->functions[module_->num_imported_functions].code.is_set()) {
        errorf(pc(), "function count is %u, but code section is absent",
               module_->num_declared_functions);
        return false;
      }
    }
    // Perform a similar check for the DataCount and Data sections, where data
    // segments are declared but the Data section is absent.
    if (!CheckDataSegmentsCount(
            static_cast<uint32_t>(module_->data_segments.size()))) {
      return false;
    }
    return true;
  }

  ModuleResult FinishDecoding() {
    if (ok() && CheckMismatchedCounts()) {
      // We calculate the global offsets here, because there may not be a
      // global section and code section that would have triggered the
      // calculation before. Even without the globals section the calculation
      // is needed because globals can also be defined in the import section.
      CalculateGlobalOffsets(module_.get());
    }

    if (module_->has_shared_part) detected_features_->add_shared();

    return toResult(std::move(module_));
  }

  // Decodes an entire module.
  ModuleResult DecodeModule(bool validate_functions) {
    // Keep a reference to the wire bytes, in case this decoder gets reset on
    // error.
    base::Vector<const uint8_t> wire_bytes(start_, end_ - start_);
    size_t max_size = max_module_size();
    if (wire_bytes.size() > max_size) {
      return ModuleResult{WasmError{0, "size > maximum module size (%zu): %zu",
                                    max_size, wire_bytes.size()}};
    }

    DecodeModuleHeader(wire_bytes);
    if (failed()) return toResult(nullptr);

    static constexpr uint32_t kWasmHeaderSize = 8;
    Decoder section_iterator_decoder(start_ + kWasmHeaderSize, end_,
                                     kWasmHeaderSize);
    WasmSectionIterator section_iter(&section_iterator_decoder, tracer_);

    while (ok()) {
      if (section_iter.section_code() != SectionCode::kUnknownSectionCode) {
        uint32_t offset = static_cast<uint32_t>(section_iter.payload().begin() -
                                                wire_bytes.begin());
        DecodeSection(section_iter.section_code(), section_iter.payload(),
                      offset);
        if (!ok()) break;
      }
      if (!section_iter.more()) break;
      section_iter.advance(true);
    }

    // Check for module structure errors before validating function bodies, to
    // produce consistent error message independent of whether validation
    // happens here or later.
    if (section_iterator_decoder.failed()) {
      return section_iterator_decoder.toResult(nullptr);
    }

    ModuleResult result = FinishDecoding();
    if (!result.failed() && validate_functions) {
      std::function<bool(int)> kNoFilter;
      if (WasmError validation_error =
              ValidateFunctions(module_.get(), enabled_features_, wire_bytes,
                                kNoFilter, detected_features_)) {
        result = ModuleResult{validation_error};
      }
    }

    if (v8_flags.dump_wasm_module) DumpModule(wire_bytes, result.ok());

    return result;
  }

  // Decodes a single anonymous function starting at {start_}.
  FunctionResult DecodeSingleFunctionForTesting(Zone* zone,
                                                ModuleWireBytes wire_bytes,
                                                const WasmModule* module) {
    DCHECK(ok());
    pc_ = start_;
    expect_u8("type form", kWasmFunctionTypeCode);
    WasmFunction function;
    function.sig = consume_sig(zone);
    function.code = {off(pc_), static_cast<uint32_t>(end_ - pc_)};

    if (!ok()) return FunctionResult{std::move(error_)};

    constexpr bool kShared = false;
    FunctionBody body{function.sig, off(pc_), pc_, end_, kShared};

    WasmDetectedFeatures unused_detected_features;
    DecodeResult result = ValidateFunctionBody(zone, enabled_features_, module,
                                               &unused_detected_features, body);

    if (result.failed()) return FunctionResult{std::move(result).error()};

    return FunctionResult{std::make_unique<WasmFunction>(function)};
  }

  // Decodes a single function signature at {start}.
  const FunctionSig* DecodeFunctionSignatureForTesting(Zone* zone,
                                                       const uint8_t* start) {
    pc_ = start;
    if (!expect_u8("type form", kWasmFunctionTypeCode)) return nullptr;
    const FunctionSig* result = consume_sig(zone);
    return ok() ? result : nullptr;
  }

  ConstantExpression DecodeInitExprForTesting(ValueType expected) {
    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    return consume_init_expr(module_.get(), expected, kIsShared);
  }

  // Takes a module as parameter so that wasm-disassembler.cc can pass its own
  // module.
  ConstantExpression consume_element_segment_entry(
      WasmModule* module, const WasmElemSegment& segment) {
    if (segment.element_type == WasmElemSegment::kExpressionElements) {
      return consume_init_expr(module, segment.type, segment.shared);
    } else {
      return ConstantExpression::RefFunc(
          consume_element_func_index(module, segment.type));
    }
  }

  const std::shared_ptr<WasmModule>& shared_module() const { return module_; }

 private:
  bool has_seen_unordered_section(SectionCode section_code) {
    return seen_unordered_sections_ & (1 << section_code);
  }

  void set_seen_unordered_section(SectionCode section_code) {
    seen_unordered_sections_ |= 1 << section_code;
  }

  uint32_t off(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr - start_) + buffer_offset_;
  }

  // Calculate individual global offsets and total size of globals table. This
  // function should be called after all globals have been defined, which is
  // after the import section and the global section, but before the global
  // offsets are accessed, e.g. by the function compilers. The moment when this
  // function should be called is not well-defined, as the global section may
  // not exist. Therefore this function is called multiple times.
  void CalculateGlobalOffsets(WasmModule* module) {
    if (module->globals.empty() || module->untagged_globals_buffer_size != 0 ||
        module->tagged_globals_buffer_size != 0) {
      // This function has already been executed before, so we don't have to
      // execute it again.
      return;
    }
    uint32_t untagged_offset = 0;
    uint32_t tagged_offset = 0;
    uint32_t num_imported_mutable_globals = 0;
    for (WasmGlobal& global : module->globals) {
      if (global.mutability && global.imported) {
        global.index = num_imported_mutable_globals++;
      } else if (global.type.is_reference()) {
        global.offset = tagged_offset;
        // All entries in the tagged_globals_buffer have size 1.
        tagged_offset++;
      } else {
        int size = global.type.value_kind_size();
        untagged_offset = (untagged_offset + size - 1) & ~(size - 1);  // align
        global.offset = untagged_offset;
        untagged_offset += size;
      }
    }
    module->untagged_globals_buffer_size = untagged_offset;
    module->tagged_globals_buffer_size = tagged_offset;
  }

  ModuleTypeIndex consume_sig_index(WasmModule* module,
                                    const FunctionSig** sig) {
    const uint8_t* pos = pc_;
    ModuleTypeIndex sig_index{consume_u32v("signature index")};
    if (tracer_) tracer_->Bytes(pos, static_cast<uint32_t>(pc_ - pos));
    if (!module->has_signature(sig_index)) {
      errorf(pos, "no signature at index %u (%d types)", sig_index.index,
             static_cast<int>(module->types.size()));
      *sig = nullptr;
      return {};
    }
    *sig = module->signature(sig_index);
    if (tracer_) {
      tracer_->Description(*sig);
      tracer_->NextLine();
    }
    return sig_index;
  }

  ModuleTypeIndex consume_tag_sig_index(WasmModule* module,
                                        const FunctionSig** sig) {
    const uint8_t* pos = pc_;
    ModuleTypeIndex sig_index = consume_sig_index(module, sig);

    if (!enabled_features_.has_wasmfx() && *sig &&
        (*sig)->return_count() != 0) {
      errorf(pos, "tag signature %u has non-void return", sig_index);
      *sig = nullptr;
      return {};
    }
    return sig_index;
  }

  uint32_t consume_count(const char* name, size_t maximum) {
    const uint8_t* p = pc_;
    uint32_t count = consume_u32v(name, tracer_);
    if (tracer_) {
      tracer_->Description(count);
      if (count == 1) {
        tracer_->Description(": ");
      } else {
        tracer_->NextLine();
      }
    }
    if (count > maximum) {
      errorf(p, "%s of %u exceeds internal limit of %zu", name, count, maximum);
      return 0;
    }
    return count;
  }

  uint32_t consume_func_index(WasmModule* module, WasmFunction** func) {
    return consume_index("function", &module->functions, func);
  }

  uint32_t consume_global_index(WasmModule* module, WasmGlobal** global) {
    return consume_index("global", &module->globals, global);
  }

  uint32_t consume_table_index(WasmModule* module, WasmTable** table) {
    return consume_index("table", &module->tables, table);
  }

  uint32_t consume_tag_index(WasmModule* module, WasmTag** tag) {
    return consume_index("tag", &module->tags, tag);
  }

  template <typename T>
  uint32_t consume_index(const char* name, std::vector<T>* vector, T** ptr) {
    const uint8_t* pos = pc_;
    uint32_t index = consume_u32v("index", tracer_);
    if (tracer_) {
      tracer_->Description(": ");
      tracer_->Description(index);
    }
    if (index >= vector->size()) {
      errorf(pos, "%s index %u out of bounds (%d entr%s)", name, index,
             static_cast<int>(vector->size()),
             vector->size() == 1 ? "y" : "ies");
      *ptr = nullptr;
      return 0;
    }
    *ptr = &(*vector)[index];
    return index;
  }

  // The limits byte structure is used for memories and tables.
  struct LimitsByte {
    uint8_t flags;

    // Flags 0..7 are valid (3 bits).
    bool is_valid() const { return (flags & ~0x7) == 0; }
    bool has_maximum() const { return flags & 0x1; }
    bool is_shared() const { return flags & 0x2; }
    bool is_64bit() const { return flags & 0x4; }
    AddressType address_type() const {
      return is_64bit() ? AddressType::kI64 : AddressType::kI32;
    }
  };

  enum LimitsByteType { kMemory, kTable };

  template <LimitsByteType limits_type>
  LimitsByte consume_limits_byte() {
    if (tracer_) tracer_->Bytes(pc_, 1);
    LimitsByte limits{consume_u8(
        limits_type == kMemory ? "memory limits flags" : "table limits flags")};
    if (!limits.is_valid()) {
      errorf(pc() - 1, "invalid %s limits flags 0x%x",
             limits_type == kMemory ? "memory" : "table", limits.flags);
    }

    if (limits.is_shared()) {
      if constexpr (limits_type == kMemory) {
        // V8 does not support shared memory without a maximum.
        if (!limits.has_maximum()) {
          error(pc() - 1, "shared memory must have a maximum defined");
        }
        // TODO(42204563): Implement proper handling of shared memories when
        // Shared Everything is enabled.
      } else if (!enabled_features_.has_shared()) {  // table
        error(pc() - 1, "invalid table limits flags");
      } else {
        // TODO(42204563): Support shared tables.
        error(pc() - 1, "shared tables are not supported yet");
      }
    }

    if (tracer_) {
      if (limits.is_shared()) tracer_->Description(" shared");
      if (limits.is_64bit()) {
        tracer_->Description(limits_type == kMemory ? " mem64" : " table64");
      }
      tracer_->Description(limits.has_maximum() ? " with maximum"
                                                : " no maximum");
      tracer_->NextLine();
    }

    return limits;
  }

  void consume_table_flags(WasmTable* table) {
    LimitsByte limits = consume_limits_byte<kTable>();
    table->has_maximum_size = limits.has_maximum();
    table->shared = limits.is_shared();
    table->address_type = limits.address_type();

    if (table->is_table64()) detected_features_->add_memory64();
  }

  void consume_memory_flags(WasmMemory* memory) {
    LimitsByte limits = consume_limits_byte<kMemory>();
    memory->has_maximum_pages = limits.has_maximum();
    memory->is_shared = limits.is_shared();
    memory->address_type = limits.address_type();

    if (memory->is_shared) detected_features_->add_shared_memory();
    if (memory->is_memory64()) detected_features_->add_memory64();
  }

  std::pair<bool, bool> consume_global_flags() {
    uint8_t flags = consume_u8("global flags");
    if (flags & ~0b11) {
      errorf(pc() - 1, "invalid global flags 0x%x", flags);
      return {false, false};
    }
    bool mutability = flags & 0b1;
    bool shared = flags & 0b10;
    if (tracer_) {
      tracer_->Bytes(pc_ - 1, 1);  // The flags byte.
      if (shared) tracer_->Description(" shared");
      tracer_->Description(mutability ? " mutable" : " immutable");
    }
    if (shared && !enabled_features_.has_shared()) {
      errorf(pc() - 1, "invalid global flags 0x%x", flags);
      return {false, false};
    }
    if (shared) {
      // TODO(42204563): Support shared globals.
      error(pc() - 1, "shared globals are not supported yet");
      return {false, false};
    }
    return {mutability, shared};
  }

  enum ResizableLimitsType : bool { k32BitLimits, k64BitLimits };
  void consume_resizable_limits(
      const char* name, const char* units,
      // Not: both memories and tables have a 32-bit limit on the initial size.
      uint32_t max_initial, uint32_t* initial, bool has_maximum,
      uint64_t max_maximum, uint64_t* maximum, ResizableLimitsType type) {
    const uint8_t* pos = pc();
    // Note that even if we read the values as 64-bit value, all V8 limits are
    // still within uint32_t range.
    uint64_t initial_64 = type == k64BitLimits
                              ? consume_u64v("initial size", tracer_)
                              : consume_u32v("initial size", tracer_);
    if (initial_64 > max_initial) {
      errorf(pos,
             "initial %s size (%" PRIu64
             " %s) is larger than implementation limit (%u %s)",
             name, initial_64, units, max_initial, units);
    }
    *initial = static_cast<uint32_t>(initial_64);
    if (tracer_) {
      tracer_->Description(*initial);
      tracer_->NextLine();
    }
    if (has_maximum) {
      pos = pc();
      uint64_t maximum_64 = type == k64BitLimits
                                ? consume_u64v("maximum size", tracer_)
                                : consume_u32v("maximum size", tracer_);
      if (maximum_64 > max_maximum) {
        errorf(pos,
               "maximum %s size (%" PRIu64
               " %s) is larger than implementation limit (%" PRIu64 " %s)",
               name, maximum_64, units, max_maximum, units);
      }
      if (maximum_64 < *initial) {
        errorf(pos,
               "maximum %s size (%" PRIu64 " %s) is less than initial (%u %s)",
               name, maximum_64, units, *initial, units);
      }
      *maximum = maximum_64;
      if (tracer_) {
        tracer_->Description(*maximum);
        tracer_->NextLine();
      }
    } else {
      *maximum = max_initial;
    }
  }

  // Consumes a byte, and emits an error if it does not equal {expected}.
  bool expect_u8(const char* name, uint8_t expected) {
    const uint8_t* pos = pc();
    uint8_t value = consume_u8(name);
    if (value != expected) {
      errorf(pos, "expected %s 0x%02x, got 0x%02x", name, expected, value);
      return false;
    }
    return true;
  }

  ConstantExpression consume_init_expr(WasmModule* module, ValueType expected,
                                       bool is_shared,
                                       bool* ends_with_struct_new = nullptr) {
    // The error message mimics the one generated by the {WasmFullDecoder}.
#define TYPE_CHECK(found)                                                \
  if (V8_UNLIKELY(!IsSubtypeOf(found, expected, module))) {              \
    errorf(pc() + 1,                                                     \
           "type error in constant expression[0] (expected %s, got %s)", \
           expected.name().c_str(), found.name().c_str());               \
    return {};                                                           \
  }

    if (tracer_) tracer_->NextLineIfNonEmpty();
    // To avoid initializing a {WasmFullDecoder} for the most common
    // expressions, we replicate their decoding and validation here. The
    // manually handled cases correspond to {ConstantExpression}'s kinds.
    // We need to make sure to check that the expression ends in {kExprEnd};
    // otherwise, it is just the first operand of a composite expression, and we
    // fall back to the default case.
    if (!more()) {
      error("Beyond end of code");
      return {};
    }
    switch (static_cast<WasmOpcode>(*pc())) {
      case kExprI32Const: {
        auto [value, length] =
            read_i32v<FullValidationTag>(pc() + 1, "i32.const");
        if (V8_UNLIKELY(failed())) return {};
        if (V8_LIKELY(lookahead(1 + length, kExprEnd))) {
          TYPE_CHECK(kWasmI32)
          if (tracer_) {
            tracer_->InitializerExpression(pc_, pc_ + length + 2, kWasmI32);
          }
          consume_bytes(length + 2);
          return ConstantExpression::I32Const(value);
        }
        break;
      }
      case kExprRefFunc: {
        auto [index, length] =
            read_u32v<FullValidationTag>(pc() + 1, "ref.func");
        if (V8_UNLIKELY(failed())) return {};
        if (V8_LIKELY(lookahead(1 + length, kExprEnd))) {
          if (V8_UNLIKELY(index >= module->functions.size())) {
            errorf(pc() + 1, "function index %u out of bounds", index);
            return {};
          }
          ModuleTypeIndex functype{module->functions[index].sig_index};
          bool functype_is_shared = module->type(functype).is_shared;
          ValueType type = ValueType::Ref(functype, functype_is_shared,
                                          RefTypeKind::kFunction)
                               .AsExactIfEnabled(enabled_features_);
          TYPE_CHECK(type)
          if (V8_UNLIKELY(is_shared && !type.is_shared())) {
            error(pc(), "ref.func does not have a shared type");
            return {};
          }
          module->functions[index].declared = true;
          if (tracer_) {
            tracer_->InitializerExpression(pc_, pc_ + length + 2, type);
          }
          consume_bytes(length + 2);
          return ConstantExpression::RefFunc(index);
        }
        break;
      }
      case kExprRefNull: {
        auto [type, length] =
            value_type_reader::read_heap_type<FullValidationTag>(
                this, pc() + 1, enabled_features_, detected_features_);
        value_type_reader::ValidateHeapType<FullValidationTag>(this, pc_,
                                                               module, type);
        if (V8_UNLIKELY(failed())) return {};
        value_type_reader::Populate(&type, module);
        if (V8_LIKELY(lookahead(1 + length, kExprEnd))) {
          TYPE_CHECK(ValueType::RefNull(type))
          if (V8_UNLIKELY(is_shared && !type.is_shared())) {
            error(pc(), "ref.null does not have a shared type");
            return {};
          }
          if (tracer_) {
            tracer_->InitializerExpression(pc_, pc_ + length + 2,
                                           ValueType::RefNull(type));
          }
          consume_bytes(length + 2);
          return ConstantExpression::RefNull(type);
        }
        break;
      }
      default:
        break;
    }
#undef TYPE_CHECK

    auto sig = FixedSizeSignature<ValueType>::Returns(expected);
    FunctionBody body(&sig, this->pc_offset(), pc_, end_, is_shared);
    WasmDetectedFeatures detected;
    ConstantExpression result;
    {
      // We need a scope for the decoder because its destructor resets some Zone
      // elements, which has to be done before we reset the Zone afterwards.
      WasmFullDecoder<Decoder::FullValidationTag, ConstantExpressionInterface,
                      kConstantExpression>
          decoder(&init_expr_zone_, module, enabled_features_, &detected, body,
                  module);

      uint32_t offset = this->pc_offset();

      decoder.DecodeFunctionBody();

      if (tracer_) {
        // In case of error, decoder.end() is set to the position right before
        // the byte(s) that caused the error. For debugging purposes, we should
        // print these bytes, but we don't know how many of them there are, so
        // for now we have to guess. For more accurate behavior, we'd have to
        // pass {num_invalid_bytes} to every {decoder->DecodeError()} call.
        static constexpr size_t kInvalidBytesGuess = 4;
        const uint8_t* end =
            decoder.ok() ? decoder.end()
                         : std::min(decoder.end() + kInvalidBytesGuess, end_);
        tracer_->InitializerExpression(pc_, end, expected);
      }
      this->pc_ = decoder.end();

      if (decoder.failed()) {
        error(decoder.error().offset(), decoder.error().message().c_str());
        return {};
      }

      if (!decoder.interface().end_found()) {
        error("constant expression is missing 'end'");
        return {};
      }

      result = ConstantExpression::WireBytes(
          offset, static_cast<uint32_t>(decoder.end() - decoder.start()));

      if (ends_with_struct_new) {
        *ends_with_struct_new = decoder.interface().ends_with_struct_new();
      }
    }

    // We reset the zone here; its memory is not used anymore, and we do not
    // want memory from all constant expressions to add up.
    init_expr_zone_.Reset();

    return result;
  }

  // Read a mutability flag
  bool consume_mutability() {
    if (tracer_) tracer_->Bytes(pc_, 1);
    uint8_t val = consume_u8("mutability");
    if (tracer_) {
      tracer_->Description(val == 0   ? " immutable"
                           : val == 1 ? " mutable"
                                      : " invalid");
    }
    if (val > 1) error(pc_ - 1, "invalid mutability");
    return val != 0;
  }

  // Pass a {module} to get a pre-{Populate()}d ValueType. That's safe
  // when the module's type section has already been fully decoded.
  ValueType consume_value_type(const WasmModule* module = nullptr) {
    auto [result, length] =
        value_type_reader::read_value_type<FullValidationTag>(
            this, pc_,
            module_->origin == kWasmOrigin ? enabled_features_
                                           : WasmEnabledFeatures::None(),
            detected_features_);
    value_type_reader::ValidateValueType<FullValidationTag>(
        this, pc_, module_.get(), result);
    if (ok() && module) value_type_reader::Populate(&result, module);
    if (tracer_) {
      tracer_->Bytes(pc_, length);
      tracer_->Description(result);
    }
    consume_bytes(length, "value type");
    return result;
  }

  HeapType consume_heap_type() {
    auto [heap_type, length] =
        value_type_reader::read_heap_type<FullValidationTag>(
            this, pc_,
            module_->origin == kWasmOrigin ? enabled_features_
                                           : WasmEnabledFeatures::None(),
            detected_features_);

    value_type_reader::ValidateHeapType<FullValidationTag>(
        this, pc_, module_.get(), heap_type);
    if (tracer_) {
      tracer_->Bytes(pc_, length);
      tracer_->Description(heap_type);
    }
    consume_bytes(length, "heap type");
    return heap_type;
  }

  ValueType consume_storage_type() {
    uint8_t opcode = read_u8<FullValidationTag>(this->pc());
    switch (opcode) {
      case kI8Code:
        consume_bytes(1, " i8", tracer_);
        return kWasmI8;
      case kI16Code:
        consume_bytes(1, " i16", tracer_);
        return kWasmI16;
      default:
        // It is not a packed type, so it has to be a value type.
        return consume_value_type();
    }
  }

  const FunctionSig* consume_sig(Zone* zone) {
    if (tracer_) tracer_->NextLine();
    // Parse parameter types.
    uint32_t param_count =
        consume_count("param count", kV8MaxWasmFunctionParams);
    // We don't know the return count yet, so decode the parameters into a
    // temporary SmallVector. This needs to be copied over into the permanent
    // storage later.
    base::SmallVector<ValueType, 8> params{param_count};
    for (uint32_t i = 0; i < param_count; ++i) {
      params[i] = consume_value_type();
      if (tracer_) tracer_->NextLineIfFull();
    }
    if (tracer_) tracer_->NextLineIfNonEmpty();

    // Parse return types.
    uint32_t return_count =
        consume_count("return count", kV8MaxWasmFunctionReturns);
    if (return_count > 1) {
      detected_features_->add_multi_value();
    }
    // Now that we know the param count and the return count, we can allocate
    // the permanent storage.
    ValueType* sig_storage =
        zone->AllocateArray<ValueType>(param_count + return_count);
    // Note: Returns come first in the signature storage.
    std::copy_n(params.begin(), param_count, sig_storage + return_count);
    for (uint32_t i = 0; i < return_count; ++i) {
      sig_storage[i] = consume_value_type();
      if (tracer_) tracer_->NextLineIfFull();
    }
    if (tracer_) tracer_->NextLineIfNonEmpty();

    return zone->New<FunctionSig>(return_count, param_count, sig_storage);
  }

  const StructType* consume_struct(Zone* zone, bool is_descriptor,
                                   bool is_shared) {
    uint32_t field_count =
        consume_count(", field count", kV8MaxWasmStructFields);
    if (failed()) return nullptr;
    ValueType* fields = zone->AllocateArray<ValueType>(field_count);
    bool* mutabilities = zone->AllocateArray<bool>(field_count);
    for (uint32_t i = 0; ok() && i < field_count; ++i) {
      fields[i] = consume_storage_type();
      mutabilities[i] = consume_mutability();
      if (tracer_) tracer_->NextLine();
    }
    if (failed()) return nullptr;
    uint32_t* offsets = zone->AllocateArray<uint32_t>(field_count);
    StructType* result = zone->New<StructType>(
        field_count, offsets, fields, mutabilities, is_descriptor, is_shared);
    result->InitializeOffsets();
    return result;
  }

  const ArrayType* consume_array(Zone* zone) {
    ValueType element_type = consume_storage_type();
    bool mutability = consume_mutability();
    if (tracer_) tracer_->NextLine();
    if (failed()) return nullptr;
    return zone->New<ArrayType>(element_type, mutability);
  }

  // Consume the attribute field of an exception.
  uint32_t consume_exception_attribute() {
    const uint8_t* pos = pc_;
    uint32_t attribute = consume_u32v("exception attribute");
    if (tracer_) tracer_->Bytes(pos, static_cast<uint32_t>(pc_ - pos));
    if (attribute != kExceptionAttribute) {
      errorf(pos, "exception attribute %u not supported", attribute);
      return 0;
    }
    return attribute;
  }

  WasmElemSegment consume_element_segment_header() {
    const uint8_t* pos = pc();

    // The mask for the bit in the flag which indicates if the segment is
    // active or not (0 is active).
    constexpr uint8_t kNonActiveMask = 1 << 0;
    // The mask for the bit in the flag which indicates:
    // - for active tables, if the segment has an explicit table index field.
    // - for non-active tables, whether the table is declarative (vs. passive).
    constexpr uint8_t kHasTableIndexOrIsDeclarativeMask = 1 << 1;
    // The mask for the bit in the flag which indicates if the functions of this
    // segment are defined as function indices (0) or constant expressions (1).
    constexpr uint8_t kExpressionsAsElementsMask = 1 << 2;
    // The mask for the bit which denotes whether this segment is shared.
    constexpr uint8_t kSharedFlag = 1 << 3;
    constexpr uint8_t kFullMask = kNonActiveMask |
                                  kHasTableIndexOrIsDeclarativeMask |
                                  kExpressionsAsElementsMask | kSharedFlag;

    uint32_t flag = consume_u32v("flag", tracer_);
    if ((flag & kFullMask) != flag) {
      errorf(pos, "illegal flag value %u", flag);
      return {};
    }

    bool is_shared = flag & kSharedFlag;
    if (is_shared && !enabled_features_.has_shared()) {
      errorf(pos, "illegal flag value %u", flag);
      return {};
    }
    if (is_shared) {
      // TODO(42204563): Support shared element segments.
      error(pos, "shared element segments are not supported yet.");
      return {};
    }

    if (is_shared) module_->has_shared_part = true;

    const WasmElemSegment::Status status =
        (flag & kNonActiveMask) ? (flag & kHasTableIndexOrIsDeclarativeMask)
                                      ? WasmElemSegment::kStatusDeclarative
                                      : WasmElemSegment::kStatusPassive
                                : WasmElemSegment::kStatusActive;
    const bool is_active = status == WasmElemSegment::kStatusActive;
    if (tracer_) {
      tracer_->Description(": ");
      tracer_->Description(status == WasmElemSegment::kStatusActive ? "active"
                           : status == WasmElemSegment::kStatusPassive
                               ? "passive,"
                               : "declarative,");
    }

    WasmElemSegment::ElementType element_type =
        flag & kExpressionsAsElementsMask
            ? WasmElemSegment::kExpressionElements
            : WasmElemSegment::kFunctionIndexElements;

    const bool has_table_index =
        is_active && (flag & kHasTableIndexOrIsDeclarativeMask);
    uint32_t table_index = 0;
    if (has_table_index) {
      table_index = consume_u32v(", table index", tracer_);
      if (tracer_) tracer_->Description(table_index);
    }
    if (V8_UNLIKELY(is_active && table_index >= module_->tables.size())) {
      // If `has_table_index`, we have an explicit table index. Otherwise, we
      // always have the implicit table index 0.
      errorf(pos, "out of bounds%s table index %u",
             has_table_index ? "" : " implicit", table_index);
      return {};
    }

    ValueType table_type =
        is_active ? module_->tables[table_index].type : kWasmBottom;

    ConstantExpression offset;
    if (is_active) {
      if (tracer_) {
        tracer_->Description(", offset:");
        tracer_->NextLine();
      }
      offset = consume_init_expr(
          module_.get(),
          module_->tables[table_index].is_table64() ? kWasmI64 : kWasmI32,
          is_shared);
      // Failed to parse offset initializer, return early.
      if (failed()) return {};
    }

    // Denotes an active segment without table index, type, or element kind.
    const bool backwards_compatible_mode =
        is_active && !(flag & kHasTableIndexOrIsDeclarativeMask);
    ValueType type;
    if (element_type == WasmElemSegment::kExpressionElements) {
      if (backwards_compatible_mode) {
        type = kWasmFuncRef;
      } else {
        if (tracer_) tracer_->Description(" element type:");
        type = consume_value_type(module_.get());
        if (failed()) return {};
      }
    } else {
      if (!backwards_compatible_mode) {
        // We have to check that there is an element kind of type Function. All
        // other element kinds are not valid yet.
        if (tracer_) tracer_->Description(" ");
        uint8_t val = consume_u8("element type: function", tracer_);
        if (V8_UNLIKELY(static_cast<ImportExportKindCode>(val) !=
                        kExternalFunction)) {
          errorf(pos, "illegal element kind 0x%x. Must be 0x%x", val,
                 kExternalFunction);
          return {};
        }
      }
      type = kWasmFuncRef.AsNonNull();
    }

    if (V8_UNLIKELY(is_active &&
                    !IsSubtypeOf(type, table_type, this->module_.get()))) {
      errorf(pos,
             "Element segment of type %s is not a subtype of referenced "
             "table %u (of type %s)",
             type.name().c_str(), table_index, table_type.name().c_str());
      return {};
    }

    // TODO(14616): Is this too restrictive?
    if (V8_UNLIKELY(is_active &&
                    (is_shared != module_->tables[table_index].shared))) {
      error(pos,
            "Shared (resp. non-shared) element segments must refer to shared "
            "(resp. non-shared) tables");
      return {};
    }

    uint32_t num_elem =
        consume_count(" number of elements", max_table_init_entries());

    if (is_active) {
      return {is_shared,    type,     table_index, std::move(offset),
              element_type, num_elem, pc_offset()};
    } else {
      return {status, is_shared, type, element_type, num_elem, pc_offset()};
    }
  }

  DataSegmentHeader consume_data_segment_header() {
    const uint8_t* pos = pc();
    uint32_t flag = consume_u32v("flag", tracer_);

    if (flag & ~0b1011) {
      errorf(pos, "illegal flag value %u", flag);
      return {};
    }

    uint32_t status_flag = flag & 0b11;

    if (tracer_) {
      tracer_->Description(": ");
      tracer_->Description(
          status_flag == SegmentFlags::kActiveNoIndex     ? "active no index"
          : status_flag == SegmentFlags::kPassive         ? "passive"
          : status_flag == SegmentFlags::kActiveWithIndex ? "active with index"
                                                          : "unknown");
    }

    if (status_flag != SegmentFlags::kActiveNoIndex &&
        status_flag != SegmentFlags::kPassive &&
        status_flag != SegmentFlags::kActiveWithIndex) {
      errorf(pos, "illegal flag value %u", flag);
      return {};
    }

    bool is_shared = flag & 0b1000;

    if (V8_UNLIKELY(is_shared && !enabled_features_.has_shared())) {
      errorf(pos, "illegal flag value %u.", flag);
      return {};
    }
    if (V8_UNLIKELY(is_shared)) {
      // TODO(42204563): Support shared data segments.
      error(pos, "shared data segments are not supported yet.");
      return {};
    }

    if (is_shared) module_->has_shared_part = true;

    if (tracer_) {
      if (is_shared) tracer_->Description(" shared");
      tracer_->NextLine();
    }

    bool is_active = status_flag == SegmentFlags::kActiveNoIndex ||
                     status_flag == SegmentFlags::kActiveWithIndex;
    uint32_t mem_index = status_flag == SegmentFlags::kActiveWithIndex
                             ? consume_u32v("memory index", tracer_)
                             : 0;
    ConstantExpression offset;

    if (is_active) {
      size_t num_memories = module_->memories.size();
      if (mem_index >= num_memories) {
        errorf(pos,
               "invalid memory index %u for data section (having %zu memor%s)",
               mem_index, num_memories, num_memories == 1 ? "y" : "ies");
        return {};
      }
      ValueType expected_type =
          module_->memories[mem_index].is_memory64() ? kWasmI64 : kWasmI32;
      offset = consume_init_expr(module_.get(), expected_type, is_shared);
    }

    return {is_active, is_shared, mem_index, offset};
  }

  uint32_t consume_element_func_index(WasmModule* module, ValueType expected) {
    WasmFunction* func = nullptr;
    const uint8_t* initial_pc = pc();
    uint32_t index = consume_func_index(module, &func);
    if (tracer_) tracer_->NextLine();
    if (failed()) return index;
    DCHECK_NOT_NULL(func);
    DCHECK_EQ(index, func->func_index);
    ValueType entry_type =
        ValueType::Ref(func->sig_index, module->type(func->sig_index).is_shared,
                       RefTypeKind::kFunction);
    if (V8_LIKELY(expected == kWasmFuncRef &&
                  !enabled_features_.has_shared())) {
      DCHECK(module->type(func->sig_index).kind == TypeDefinition::kFunction);
      DCHECK(IsSubtypeOf(entry_type, expected, module));
    } else if (V8_UNLIKELY(!IsSubtypeOf(entry_type, expected, module))) {
      errorf(initial_pc,
             "Invalid type in element entry: expected %s, got %s instead.",
             expected.name().c_str(), entry_type.name().c_str());
      return index;
    }
    func->declared = true;
    return index;
  }

  const WasmEnabledFeatures enabled_features_;
  WasmDetectedFeatures* const detected_features_;
  const std::shared_ptr<WasmModule> module_;
  const uint8_t* module_start_ = nullptr;
  const uint8_t* module_end_ = nullptr;
  ITracer* tracer_;
  // The type section is the first section in a module.
  uint8_t next_ordered_section_ = kFirstSectionInModule;
  // We store next_ordered_section_ as uint8_t instead of SectionCode so that
  // we can increment it. This static_assert should make sure that SectionCode
  // does not get bigger than uint8_t accidentally.
  static_assert(sizeof(ModuleDecoderImpl::next_ordered_section_) ==
                    sizeof(SectionCode),
                "type mismatch");
  uint32_t seen_unordered_sections_ = 0;
  static_assert(kBitsPerByte *
                        sizeof(ModuleDecoderImpl::seen_unordered_sections_) >
                    kLastKnownModuleSection,
                "not enough bits");
  AccountingAllocator allocator_;
  // We pass this {Zone} to the temporary {WasmFullDecoder} we allocate during
  // each call to {consume_init_expr}, and reset it after each such call. This
  // has been found to improve performance a bit over allocating a new {Zone}
  // each time.
  Zone init_expr_zone_{&allocator_, "constant expr. zone"};

  // Instruction traces are decoded in DecodeInstTraceSection as a 3-tuple
  // of the function index, function offset, and mark_id. In DecodeCodeSection,
  // after the functions have been decoded this is translated to pairs of module
  // offsets and mark ids.
  std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> inst_traces_;
};

// Convenient interface for iterating over a "descriptors" custom section.
// In the interest of efficiency, this is designed for disciplined callers:
// the order in which public methods should be called is not arbitrary, but
// should follow the order in which things appear in the section. That allows
// this class to avoid building up large caches, and instead use a streaming
// approach.
// The general flow is that callers follow a pattern of:
//     while (dsi.has_foo()) { Foo f = dsi.NextFoo(); }
// where the values of "foo" should be chosen in the right order, and the
// type "Foo" is typically one of the subclasses of {Entry}.
// After every operation that decodes wire bytes, an error check should be
// performed using the `ok()` accessor.
class DescriptorsSectionIterator : public Decoder {
 public:
  DescriptorsSectionIterator(base::Vector<const uint8_t> wire_bytes,
                             uint32_t start_offset, uint32_t end_offset)
      : Decoder(wire_bytes.begin() + start_offset,
                wire_bytes.begin() + end_offset, start_offset) {
    // Version.
    uint32_t version = consume_u32v("version");
    if (!ok()) return;
    if (version != 0) {
      errorf(start_, "unsupported descriptors section version: %u", version);
      return;
    }

    // Module name.
    module_name_ = consume_string("module name");
    if (!ok()) return;

    NextSubsection();
  }

  WireBytesRef module_name() const { return module_name_; }

  struct Method {
    enum Kind : uint8_t { kMethod, kGetter, kSetter };

    Kind kind;
    bool is_static;
    WireBytesRef name;
    uint32_t index;
  };

  // "Entry" is the abstract super class of the various actual kinds of entries.
  // It contains all implementations used by more than one subclass; which
  // implies that no subclass uses all provided methods. DCHECKs provide some
  // guardrails for correct usage.
  class Entry {
   public:
    bool ok() { return decoder_->ok(); }

    bool has_export() const { return state_ == State::kExports; }
    bool has_method() const { return state_ == State::kMethods; }
    bool has_constructor() const { return state_ == State::kConstructor; }
    bool has_static() const { return state_ == State::kStatics; }

    uint32_t NextExport(uint32_t max_index) {
      DCHECK(has_export());
      const uint8_t* pos = decoder_->pc();
      uint32_t exp = consume_u32v("export index");
      if (!ok()) return exp;
      if (exp >= max_index) {
        decoder_->errorf(pos, "import index %u is out of range", exp);
        return exp;
      }
      nested_index_++;
      DCHECK_LE(nested_index_, num_nested_);
      if (nested_index_ == num_nested_) Finish();
      return exp;
    }

    Method NextMethod(uint32_t max_index) {
      DCHECK(has_method());
      Method method = NextMethodImpl(max_index, false);
      if (nested_index_ == num_nested_) ProceedToConstructor();
      return method;
    }

    std::pair<WireBytesRef, uint32_t> Constructor(uint32_t max_index) {
      DCHECK(has_constructor());
      WireBytesRef name = decoder_->consume_string("name");
      if (!ok()) return {};
      const uint8_t* pos = decoder_->pc();
      uint32_t index = decoder_->consume_u32v("index");
      CheckIndex(pos, index, max_index);
      ProceedToStatics();
      return {name, index};
    }

    Method NextStatic(uint32_t max_index) {
      DCHECK(has_static());
      Method static_method = NextMethodImpl(max_index, true);
      if (nested_index_ == num_nested_) Finish();
      return static_method;
    }

    // Can be be an overestimate when there's a getter and a setter for the
    // same property.
    uint32_t estimated_number_of_methods() const {
      DCHECK(has_method());
      return num_nested();
    }
    uint32_t estimated_number_of_statics() const {
      DCHECK(has_static());
      return num_nested();
    }

   protected:
    enum class State {
      kParentOrImport,  // Whatever the first thing is.
      kExports,
      kMethods,
      kConstructor,
      kStatics,
      kFinished,
    };

    explicit Entry(DescriptorsSectionIterator* decoder) : decoder_(decoder) {}

    Method NextMethodImpl(uint32_t max_index, bool is_static) {
      const uint8_t* pos = decoder_->pc();
      uint8_t kind = decoder_->consume_u8("kind");
      if (kind > 2) {
        decoder_->errorf(pos, "invalid method kind %u", kind);
        return {};
      }
      if (!ok()) return {};
      WireBytesRef name = decoder_->consume_string("name");
      if (!ok()) return {};
      pos = decoder_->pc();
      uint32_t index = decoder_->consume_u32v("index");
      CheckIndex(pos, index, max_index);
      nested_index_++;
      DCHECK_LE(nested_index_, num_nested_);
      return {static_cast<Method::Kind>(kind), is_static, name, index};
    }

    void CheckIndex(const uint8_t* pos, uint32_t index, uint32_t max) {
      if (V8_LIKELY(index < max)) return;
      using DS = DescriptorsSectionIterator::State;
      DS parent_state = decoder_->state_;
      bool is_export_index = parent_state == DS::kInProtoConfig;
      const char* index_kind = is_export_index ? "export" : "function";
      decoder_->errorf(pos, "%s index %u out of range", index_kind, index);
    }

    void ProceedToExports() {
      if (!ok()) return;
      state_ = State::kExports;
      num_nested_ = consume_u32v("number of exports");
      nested_index_ = 0;
      if (num_nested_ == 0) Finish();
    }

    void ProceedToMethods() {
      if (!ok()) return;
      state_ = State::kMethods;
      num_nested_ = consume_u32v("number of methods");
      nested_index_ = 0;
      if (num_nested_ == 0) ProceedToConstructor();
    }

    void ProceedToConstructor() {
      if (!ok()) return;
      state_ = State::kConstructor;
      const uint8_t* pos = decoder_->pc();
      uint32_t has_constructor = consume_u32v("constructor");
      if (has_constructor == 0) return Finish();
      if (has_constructor > 1) {
        decoder_->error(pos, "at most one constructor allowed");
      }
    }

    void ProceedToStatics() {
      if (!ok()) return;
      state_ = State::kStatics;
      num_nested_ = consume_u32v("number of statics");
      nested_index_ = 0;
      if (num_nested_ == 0) Finish();
    }

    WireBytesRef consume_string(const char* description) {
      return decoder_->consume_string(description);
    }
    uint32_t consume_u32v(const char* description) {
      return decoder_->consume_u32v(description);
    }
    DescriptorsSectionIterator* decoder() const { return decoder_; }
    State state() const { return state_; }
    uint32_t num_nested() const { return num_nested_; }

   private:
    void Finish() {
      state_ = State::kFinished;
      decoder_->FinishEntry();
    }

    DescriptorsSectionIterator* decoder_;
    State state_{State::kParentOrImport};
    uint32_t num_nested_{0};
    uint32_t nested_index_{0};
  };  // class Entry

  class ImportEntry : public Entry {
   public:
    WireBytesRef name() {
      DCHECK_EQ(state(), State::kParentOrImport);
      WireBytesRef name = consume_string("import name");
      ProceedToExports();
      return name;
    }

   private:
    friend class DescriptorsSectionIterator;

    explicit ImportEntry(DescriptorsSectionIterator* decoder)
        : Entry(decoder) {}
  };  // class ImportEntry

  class DeclEntry : public Entry {
   public:
    bool has_parent() const { return state() == State::kParentOrImport; }

    uint32_t Parent(uint32_t current_index) {
      DCHECK_EQ(state(), State::kParentOrImport);
      const uint8_t* pos = decoder()->pc();
      uint32_t result = consume_u32v("parent");
      if (result >= current_index) {
        decoder()->errorf(pos, "parent index %u must be lower than %u", result,
                          current_index);
        return result;
      }
      ProceedToExports();
      return result;
    }

   private:
    friend class DescriptorsSectionIterator;

    explicit DeclEntry(DescriptorsSectionIterator* decoder) : Entry(decoder) {
      const uint8_t* pos = decoder->pc();
      uint32_t num_parents = consume_u32v("number of parents");
      if (num_parents > 1) {
        decoder->errorf(pos, "invalid parent count %u", num_parents);
        return;
      }
      if (num_parents == 0) ProceedToExports();
    }
  };  // class DeclEntry

  class ProtoConfig : public Entry {
   public:
    uint32_t import_index() const { return import_index_; }

   private:
    friend class DescriptorsSectionIterator;

    ProtoConfig(DescriptorsSectionIterator* decoder, uint32_t max_import_index)
        : Entry(decoder) {
      const uint8_t* pos = decoder->pc();
      import_index_ = consume_u32v("import index");
      if (import_index_ > max_import_index) {
        decoder->errorf(pos, "import index %u is out of range", import_index_);
      }
      ProceedToMethods();
    }

    uint32_t import_index_;
  };  // class ProtoConfig

  class GlobalEntry : public Entry {
   public:
    uint32_t global_index() const { return global_index_; }

    bool has_parent() const { return state() == State::kParentOrImport; }
    uint32_t Parent() {
      DCHECK_EQ(state(), State::kParentOrImport);
      const uint8_t* pos = decoder()->pc();
      uint32_t result = consume_u32v("parent");
      if (result >= global_index_) {
        decoder()->errorf(pos, "parent index %u must be lower than %u", result,
                          global_index_);
        return result;
      }
      ProceedToMethods();
      return result;
    }

   private:
    friend class DescriptorsSectionIterator;

    GlobalEntry(DescriptorsSectionIterator* decoder, uint32_t max_global_index)
        : Entry(decoder) {
      const uint8_t* pos = decoder->pc();
      global_index_ = consume_u32v("global index");
      if (global_index_ > max_global_index) {
        decoder->errorf(pos, "global index %u is out of range", global_index_);
        return;
      }
      if (global_index_ < decoder->next_min_global_index_) {
        decoder->errorf(pos, "global index %u is not in ascending order",
                        global_index_);
        return;
      }
      decoder->next_min_global_index_ = global_index_ + 1;
      pos = decoder->pc();
      uint32_t num_parents = consume_u32v("parent count");
      if (num_parents > 1) {
        decoder->errorf(pos, "invalid parent count %u", num_parents);
        return;
      }
      if (num_parents == 0) ProceedToMethods();
    }

    uint32_t global_index_;
  };  // class GlobalEntry

  bool has_import_entry() { return state_ == State::kImportEntries; }
  ImportEntry NextImportEntry() {
    DCHECK(has_import_entry());
    state_ = State::kInImportEntry;
    return ImportEntry(this);
  }

  bool has_decl_entry() { return state_ == State::kDeclEntries; }
  DeclEntry NextDeclEntry() {
    DCHECK(has_decl_entry());
    state_ = State::kInDeclEntry;
    return DeclEntry(this);
  }

  bool has_proto_config() { return state_ == State::kProtoConfigs; }
  ProtoConfig NextProtoConfig(uint32_t max_import_index) {
    DCHECK(has_proto_config());
    state_ = State::kInProtoConfig;
    return ProtoConfig(this, max_import_index);
  }

  bool has_global_entry() { return state_ == State::kGlobalEntries; }
  GlobalEntry NextGlobalEntry(uint32_t max_global_index) {
    DCHECK(has_global_entry());
    state_ = State::kInGlobalEntry;
    return GlobalEntry(this, max_global_index);
  }

  void SkipToGlobalEntries() {
    while (state_ < State::kGlobalEntries) SkipCurrentSubsection();
  }
  void SkipToProtoConfigs() {
    while (state_ < State::kProtoConfigs) SkipCurrentSubsection();
  }

  // Call this after initialization, before iterating over entries.
  uint32_t NumImportAndDeclEntries() const {
    static_assert(State::kGlobalEntries > State::kDeclEntries);
    static_assert(State::kDeclEntries > State::kImportEntries);
    if (state_ >= State::kGlobalEntries || !ok()) return 0;
    if (state_ == State::kDeclEntries) return num_entries_;
    DCHECK_EQ(state_, State::kImportEntries);
    uint32_t num_import_entries = num_entries_;
    // Use a separate decoder because we have to both rewind (to be able to
    // skip the importdecls section) and jump forward (to the vector length
    // of the declentries section).
    const uint8_t* next_subsection = start_ + next_subsection_start_position_;
    if (next_subsection >= end_) return num_import_entries;
    Decoder d(next_subsection, end_,
              buffer_offset_ + next_subsection_start_position_);
    uint8_t subsection = d.consume_u8("second subsection");
    if (!d.ok() || subsection != kDeclEntriesCode) return num_import_entries;
    d.consume_u32v("subsection length");
    if (!d.ok()) return num_import_entries;
    uint32_t num_decl_entries = d.consume_u32v("number of decl entries");
    if (!d.ok()) return num_import_entries;
    return num_import_entries + num_decl_entries;
  }

 private:
  static constexpr uint8_t kImportEntriesCode = 0;
  static constexpr uint8_t kDeclEntriesCode = 1;
  static constexpr uint8_t kGlobalEntriesCode = 2;
  static constexpr uint8_t kOffsetEntriesCode = 3;
  static constexpr uint8_t kProtoConfigsCode = 4;

  static constexpr uint8_t kStateBlocked = 0b1000'0000;
  enum class State : uint8_t {
    kUninitialized,
    kImportEntries,
    kDeclEntries,
    kGlobalEntries,
    kOffsetEntries,
    kProtoConfigs,
    kFinished,

    kInImportEntry = kImportEntries | kStateBlocked,
    kInDeclEntry = kDeclEntries | kStateBlocked,
    kInGlobalEntry = kGlobalEntries | kStateBlocked,
    kInOffsetEntry = kOffsetEntries | kStateBlocked,
    kInProtoConfig = kProtoConfigs | kStateBlocked,
  };

  WireBytesRef consume_string(const char* description) {
    return wasm::consume_string(this, unibrow::Utf8Variant::kLossyUtf8,
                                description);
  }

  void NextSubsection() {
    if (!more()) {
      state_ = State::kFinished;
      return;
    }
    const uint8_t* pos = pc_;
    uint8_t subsection_id = consume_u8("subsection ID");
    DCHECK(ok());  // Reading one byte can't fail after {more()}.

#define SUBSECTION(Name)                                              \
  if (subsection_id == k##Name##Code) {                               \
    if (state_ >= State::k##Name) {                                   \
      return error(pos, "invalid subsection order");                  \
    }                                                                 \
    state_ = State::k##Name;                                          \
    index_ = 0;                                                       \
    uint32_t subsection_length = consume_u32v("subsection length");   \
    if (!ok()) return;                                                \
    next_subsection_start_position_ = position() + subsection_length; \
    num_entries_ = consume_u32v("number of " #Name);                  \
    if (!ok()) return;                                                \
    if (num_entries_ == 0) return NextSubsection();                   \
    return;                                                           \
  }
    SUBSECTION(ImportEntries)
    SUBSECTION(DeclEntries)
    SUBSECTION(GlobalEntries)
    SUBSECTION(OffsetEntries)
    SUBSECTION(ProtoConfigs)
#undef SUBSECTION

    state_ = State::kFinished;
    errorf(pos, "invalid subsection ID %u", subsection_id);
  }

  void SkipCurrentSubsection() {
    DCHECK_GE(next_subsection_start_position_, position());
    consume_bytes(next_subsection_start_position_ - position());
    NextSubsection();
  }

  void FinishEntry() {
    state_ = static_cast<State>(static_cast<uint8_t>(state_) &
                                static_cast<uint8_t>(~kStateBlocked));
    index_++;
    if (index_ == num_entries_) NextSubsection();
  }

  State state_{State::kUninitialized};
  uint32_t num_entries_{0};
  uint32_t index_{0};
  uint32_t next_subsection_start_position_{0};
  uint32_t next_min_global_index_{0};  // For checking ascending order.
  WireBytesRef module_name_;
};

}  // namespace v8::internal::wasm

#undef TRACE

#endif  // V8_WASM_MODULE_DECODER_IMPL_H_
