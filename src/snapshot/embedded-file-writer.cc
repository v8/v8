// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded-file-writer.h"

#include <cinttypes>

#include "src/codegen/source-position-table.h"
#include "src/objects/code-inl.h"

// TODO(jgruber): Refactor to move windows-specific code into the
// windows-specific file writer.
#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"

namespace v8 {
namespace internal {

void EmbeddedFileWriter::WriteBuiltin(PlatformEmbeddedFileWriterBase* w,
                                      const i::EmbeddedData* blob,
                                      const int builtin_id) const {
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  i::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    i::SNPrintF(builtin_symbol, "Builtins_%s", i::Builtins::name(builtin_id));
  } else {
    i::SNPrintF(builtin_symbol, "%s_Builtins_%s", embedded_variant_,
                i::Builtins::name(builtin_id));
  }

  // Labels created here will show up in backtraces. We check in
  // Isolate::SetEmbeddedBlob that the blob layout remains unchanged, i.e.
  // that labels do not insert bytes into the middle of the blob byte
  // stream.
  w->DeclareFunctionBegin(builtin_symbol.begin());
  const std::vector<byte>& current_positions = source_positions_[builtin_id];

  // The code below interleaves bytes of assembly code for the builtin
  // function with source positions at the appropriate offsets.
  Vector<const byte> vpos(current_positions.data(), current_positions.size());
  v8::internal::SourcePositionTableIterator positions(
      vpos, SourcePositionTableIterator::kExternalOnly);

  const uint8_t* data = reinterpret_cast<const uint8_t*>(
      blob->InstructionStartOfBuiltin(builtin_id));
  uint32_t size = blob->PaddedInstructionSizeOfBuiltin(builtin_id);
  uint32_t i = 0;
  uint32_t next_offset =
      static_cast<uint32_t>(positions.done() ? size : positions.code_offset());
  while (i < size) {
    if (i == next_offset) {
      // Write source directive.
      w->SourceInfo(positions.source_position().ExternalFileId(),
                    GetExternallyCompiledFilename(
                        positions.source_position().ExternalFileId()),
                    positions.source_position().ExternalLine());
      positions.Advance();
      next_offset = static_cast<uint32_t>(
          positions.done() ? size : positions.code_offset());
    }
    CHECK_GE(next_offset, i);
    WriteBinaryContentsAsInlineAssembly(w, data + i, next_offset - i);
    i = next_offset;
  }

  w->DeclareFunctionEnd(builtin_symbol.begin());
}

void EmbeddedFileWriter::WriteFileEpilogue(PlatformEmbeddedFileWriterBase* w,
                                           const i::EmbeddedData* blob) const {
  {
    i::EmbeddedVector<char, kTemporaryStringLength> embedded_blob_symbol;
    i::SNPrintF(embedded_blob_symbol, "v8_%s_embedded_blob_",
                embedded_variant_);

    w->Comment("Pointer to the beginning of the embedded blob.");
    w->SectionData();
    w->AlignToDataAlignment();
    w->DeclarePointerToSymbol(embedded_blob_symbol.begin(),
                              EmbeddedBlobDataSymbol().c_str());
    w->Newline();
  }

  {
    i::EmbeddedVector<char, kTemporaryStringLength> embedded_blob_size_symbol;
    i::SNPrintF(embedded_blob_size_symbol, "v8_%s_embedded_blob_size_",
                embedded_variant_);

    w->Comment("The size of the embedded blob in bytes.");
    w->SectionRoData();
    w->AlignToDataAlignment();
    w->DeclareUint32(embedded_blob_size_symbol.begin(), blob->size());
    w->Newline();
  }

#if defined(V8_OS_WIN_X64)
  if (win64_unwindinfo::CanEmitUnwindInfoForBuiltins()) {
    WriteUnwindInfo(w, blob);
  }
#endif

  w->FileEpilogue();
}

namespace {

int WriteDirectiveOrSeparator(PlatformEmbeddedFileWriterBase* w,
                              int current_line_length,
                              DataDirective directive) {
  int printed_chars;
  if (current_line_length == 0) {
    printed_chars = w->IndentedDataDirective(directive);
    DCHECK_LT(0, printed_chars);
  } else {
    printed_chars = fprintf(w->fp(), ",");
    DCHECK_EQ(1, printed_chars);
  }
  return current_line_length + printed_chars;
}

#if defined(_MSC_VER) && !defined(__clang__)
#define V8_COMPILER_IS_MSVC
#endif

// TODO(jgruber): Move these sections into platform-dependent file writers.

#if defined(V8_COMPILER_IS_MSVC)
// Windows MASM doesn't have an .octa directive, use QWORDs instead.
// Note: MASM *really* does not like large data streams. It takes over 5
// minutes to assemble the ~350K lines of embedded.S produced when using
// BYTE directives in a debug build. QWORD produces roughly 120KLOC and
// reduces assembly time to ~40 seconds. Still terrible, but much better
// than before. See also: https://crbug.com/v8/8475.
static constexpr DataDirective kByteChunkDirective = kQuad;
static constexpr int kByteChunkSize = 8;

int WriteByteChunk(PlatformEmbeddedFileWriterBase* w, int current_line_length,
                   const uint8_t* data) {
  const uint64_t* quad_ptr = reinterpret_cast<const uint64_t*>(data);
  return current_line_length + w->HexLiteral(*quad_ptr);
}

#elif defined(V8_OS_AIX)
// PPC uses a fixed 4 byte instruction set, using .long
// to prevent any unnecessary padding.
static constexpr DataDirective kByteChunkDirective = kLong;
static constexpr int kByteChunkSize = 4;

int WriteByteChunk(PlatformEmbeddedFileWriterBase* w, int current_line_length,
                   const uint8_t* data) {
  const uint32_t* long_ptr = reinterpret_cast<const uint32_t*>(data);
  return current_line_length + w->HexLiteral(*long_ptr);
}

#else  // defined(V8_COMPILER_IS_MSVC) || defined(V8_OS_AIX)
static constexpr DataDirective kByteChunkDirective = kOcta;
static constexpr int kByteChunkSize = 16;

int WriteByteChunk(PlatformEmbeddedFileWriterBase* w, int current_line_length,
                   const uint8_t* data) {
  const size_t size = kInt64Size;

  uint64_t part1, part2;
  // Use memcpy for the reads since {data} is not guaranteed to be aligned.
#ifdef V8_TARGET_BIG_ENDIAN
  memcpy(&part1, data, size);
  memcpy(&part2, data + size, size);
#else
  memcpy(&part1, data + size, size);
  memcpy(&part2, data, size);
#endif  // V8_TARGET_BIG_ENDIAN

  if (part1 != 0) {
    current_line_length +=
        fprintf(w->fp(), "0x%" PRIx64 "%016" PRIx64, part1, part2);
  } else {
    current_line_length += fprintf(w->fp(), "0x%" PRIx64, part2);
  }
  return current_line_length;
}
#endif  // defined(V8_COMPILER_IS_MSVC) || defined(V8_OS_AIX)

#undef V8_COMPILER_IS_MSVC

int WriteLineEndIfNeeded(PlatformEmbeddedFileWriterBase* w,
                         int current_line_length, int write_size) {
  static const int kTextWidth = 100;
  // Check if adding ',0xFF...FF\n"' would force a line wrap. This doesn't use
  // the actual size of the string to be written to determine this so it's
  // more conservative than strictly needed.
  if (current_line_length + strlen(",0x") + write_size * 2 > kTextWidth) {
    fprintf(w->fp(), "\n");
    return 0;
  } else {
    return current_line_length;
  }
}

}  // namespace

// static
void EmbeddedFileWriter::WriteBinaryContentsAsInlineAssembly(
    PlatformEmbeddedFileWriterBase* w, const uint8_t* data, uint32_t size) {
  int current_line_length = 0;
  uint32_t i = 0;

  // Begin by writing out byte chunks.
  for (; i + kByteChunkSize < size; i += kByteChunkSize) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, kByteChunkDirective);
    current_line_length = WriteByteChunk(w, current_line_length, data + i);
    current_line_length =
        WriteLineEndIfNeeded(w, current_line_length, kByteChunkSize);
  }
  if (current_line_length != 0) w->Newline();
  current_line_length = 0;

  // Write any trailing bytes one-by-one.
  for (; i < size; i++) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, kByte);
    current_line_length += w->HexLiteral(data[i]);
    current_line_length = WriteLineEndIfNeeded(w, current_line_length, 1);
  }

  if (current_line_length != 0) w->Newline();
}

int EmbeddedFileWriter::LookupOrAddExternallyCompiledFilename(
    const char* filename) {
  auto result = external_filenames_.find(filename);
  if (result != external_filenames_.end()) {
    return result->second;
  }
  int new_id =
      ExternalFilenameIndexToId(static_cast<int>(external_filenames_.size()));
  external_filenames_.insert(std::make_pair(filename, new_id));
  external_filenames_by_index_.push_back(filename);
  DCHECK_EQ(external_filenames_by_index_.size(), external_filenames_.size());
  return new_id;
}

const char* EmbeddedFileWriter::GetExternallyCompiledFilename(
    int fileid) const {
  size_t index = static_cast<size_t>(ExternalFilenameIdToIndex(fileid));
  DCHECK_GE(index, 0);
  DCHECK_LT(index, external_filenames_by_index_.size());

  return external_filenames_by_index_[index];
}

int EmbeddedFileWriter::GetExternallyCompiledFilenameCount() const {
  return static_cast<int>(external_filenames_.size());
}

void EmbeddedFileWriter::PrepareBuiltinSourcePositionMap(Builtins* builtins) {
  for (int i = 0; i < Builtins::builtin_count; i++) {
    // Retrieve the SourcePositionTable and copy it.
    Code code = builtins->builtin(i);
    // Verify that the code object is still the "real code" and not a
    // trampoline (which wouldn't have source positions).
    DCHECK(!code.is_off_heap_trampoline());
    std::vector<unsigned char> data(
        code.SourcePositionTable().GetDataStartAddress(),
        code.SourcePositionTable().GetDataEndAddress());
    source_positions_[i] = data;
  }
}

#if defined(V8_OS_WIN_X64)
std::string EmbeddedFileWriter::BuiltinsUnwindInfoLabel() const {
  i::EmbeddedVector<char, kTemporaryStringLength> embedded_blob_data_symbol;
  i::SNPrintF(embedded_blob_data_symbol, "%s_Builtins_UnwindInfo",
              embedded_variant_);
  return std::string{embedded_blob_data_symbol.begin()};
}

void EmbeddedFileWriter::SetBuiltinUnwindData(
    int builtin_index, const win64_unwindinfo::BuiltinUnwindInfo& unwind_info) {
  DCHECK_LT(builtin_index, Builtins::builtin_count);
  unwind_infos_[builtin_index] = unwind_info;
}

void EmbeddedFileWriter::WriteUnwindInfoEntry(PlatformEmbeddedFileWriterBase* w,
                                              uint64_t rva_start,
                                              uint64_t rva_end) const {
  PlatformEmbeddedFileWriterWin* w_win =
      static_cast<PlatformEmbeddedFileWriterWin*>(w);
  w_win->DeclareRvaToSymbol(EmbeddedBlobDataSymbol().c_str(), rva_start);
  w_win->DeclareRvaToSymbol(EmbeddedBlobDataSymbol().c_str(), rva_end);
  w_win->DeclareRvaToSymbol(BuiltinsUnwindInfoLabel().c_str());
}

void EmbeddedFileWriter::WriteUnwindInfo(PlatformEmbeddedFileWriterBase* w,
                                         const i::EmbeddedData* blob) const {
  PlatformEmbeddedFileWriterWin* w_win =
      static_cast<PlatformEmbeddedFileWriterWin*>(w);

  // Emit an UNWIND_INFO (XDATA) struct, which contains the unwinding
  // information that is used for all builtin functions.
  DCHECK(win64_unwindinfo::CanEmitUnwindInfoForBuiltins());
  w_win->Comment("xdata for all the code in the embedded blob.");
  w_win->DeclareExternalFunction(CRASH_HANDLER_FUNCTION_NAME_STRING);

  w_win->StartXdataSection();
  {
    w_win->DeclareLabel(BuiltinsUnwindInfoLabel().c_str());
    std::vector<uint8_t> xdata =
        win64_unwindinfo::GetUnwindInfoForBuiltinFunctions();
    WriteBinaryContentsAsInlineAssembly(w_win, xdata.data(),
                                        static_cast<uint32_t>(xdata.size()));
    w_win->Comment("    ExceptionHandler");
    w_win->DeclareRvaToSymbol(CRASH_HANDLER_FUNCTION_NAME_STRING);
  }
  w_win->EndXdataSection();
  w_win->Newline();

  // Emit a RUNTIME_FUNCTION (PDATA) entry for each builtin function, as
  // documented here:
  // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64.
  w_win->Comment(
      "pdata for all the code in the embedded blob (structs of type "
      "RUNTIME_FUNCTION).");
  w_win->Comment("    BeginAddress");
  w_win->Comment("    EndAddress");
  w_win->Comment("    UnwindInfoAddress");
  w_win->StartPdataSection();
  {
    Address prev_builtin_end_offset = 0;
    for (int i = 0; i < Builtins::builtin_count; i++) {
      // Some builtins are leaf functions from the point of view of Win64 stack
      // walking: they do not move the stack pointer and do not require a PDATA
      // entry because the return address can be retrieved from [rsp].
      if (!blob->ContainsBuiltin(i)) continue;
      if (unwind_infos_[i].is_leaf_function()) continue;

      uint64_t builtin_start_offset = blob->InstructionStartOfBuiltin(i) -
                                      reinterpret_cast<Address>(blob->data());
      uint32_t builtin_size = blob->InstructionSizeOfBuiltin(i);

      const std::vector<int>& xdata_desc = unwind_infos_[i].fp_offsets();
      if (xdata_desc.empty()) {
        // Some builtins do not have any "push rbp - mov rbp, rsp" instructions
        // to start a stack frame. We still emit a PDATA entry as if they had,
        // relying on the fact that we can find the previous frame address from
        // rbp in most cases. Note that since the function does not really start
        // with a 'push rbp' we need to specify the start RVA in the PDATA entry
        // a few bytes before the beginning of the function, if it does not
        // overlap the end of the previous builtin.
        WriteUnwindInfoEntry(
            w_win,
            std::max(prev_builtin_end_offset,
                     builtin_start_offset - win64_unwindinfo::kRbpPrefixLength),
            builtin_start_offset + builtin_size);
      } else {
        // Some builtins have one or more "push rbp - mov rbp, rsp" sequences,
        // but not necessarily at the beginning of the function. In this case
        // we want to yield a PDATA entry for each block of instructions that
        // emit an rbp frame. If the function does not start with 'push rbp'
        // we also emit a PDATA entry for the initial block of code up to the
        // first 'push rbp', like in the case above.
        if (xdata_desc[0] > 0) {
          WriteUnwindInfoEntry(w_win,
                               std::max(prev_builtin_end_offset,
                                        builtin_start_offset -
                                            win64_unwindinfo::kRbpPrefixLength),
                               builtin_start_offset + xdata_desc[0]);
        }

        for (size_t j = 0; j < xdata_desc.size(); j++) {
          int chunk_start = xdata_desc[j];
          int chunk_end =
              (j < xdata_desc.size() - 1) ? xdata_desc[j + 1] : builtin_size;
          WriteUnwindInfoEntry(w_win, builtin_start_offset + chunk_start,
                               builtin_start_offset + chunk_end);
        }
      }

      prev_builtin_end_offset = builtin_start_offset + builtin_size;
      w_win->Newline();
    }
  }
  w_win->EndPdataSection();
  w_win->Newline();
}
#endif

}  // namespace internal
}  // namespace v8
