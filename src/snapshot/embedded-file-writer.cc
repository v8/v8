// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded-file-writer.h"

#include <cinttypes>

#include "src/objects/code-inl.h"

// TODO(jgruber): Remove once windows-specific code is extracted.
#include "src/snapshot/embedded/platform-embedded-file-writer-generic.h"

namespace v8 {
namespace internal {

void EmbeddedFileWriter::PrepareBuiltinSourcePositionMap(Builtins* builtins) {
  for (int i = 0; i < Builtins::builtin_count; i++) {
    // Retrieve the SourcePositionTable and copy it.
    Code code = builtins->builtin(i);
    // Verify that the code object is still the "real code" and not a
    // trampoline (which wouldn't have source positions).
    DCHECK(!code->is_off_heap_trampoline());
    std::vector<unsigned char> data(
        code->SourcePositionTable()->GetDataStartAddress(),
        code->SourcePositionTable()->GetDataEndAddress());
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
  PlatformEmbeddedFileWriterGeneric* w_gen =
      static_cast<PlatformEmbeddedFileWriterGeneric*>(w);
  w_gen->DeclareRvaToSymbol(EmbeddedBlobDataSymbol().c_str(), rva_start);
  w_gen->DeclareRvaToSymbol(EmbeddedBlobDataSymbol().c_str(), rva_end);
  w_gen->DeclareRvaToSymbol(BuiltinsUnwindInfoLabel().c_str());
}

void EmbeddedFileWriter::WriteUnwindInfo(PlatformEmbeddedFileWriterBase* w,
                                         const i::EmbeddedData* blob) const {
  PlatformEmbeddedFileWriterGeneric* w_gen =
      static_cast<PlatformEmbeddedFileWriterGeneric*>(w);

  // Emit an UNWIND_INFO (XDATA) struct, which contains the unwinding
  // information that is used for all builtin functions.
  DCHECK(win64_unwindinfo::CanEmitUnwindInfoForBuiltins());
  w_gen->Comment("xdata for all the code in the embedded blob.");
  w_gen->DeclareExternalFunction(CRASH_HANDLER_FUNCTION_NAME_STRING);

  w_gen->StartXdataSection();
  {
    w_gen->DeclareLabel(BuiltinsUnwindInfoLabel().c_str());
    std::vector<uint8_t> xdata =
        win64_unwindinfo::GetUnwindInfoForBuiltinFunctions();
    WriteBinaryContentsAsInlineAssembly(w_gen, xdata.data(),
                                        static_cast<uint32_t>(xdata.size()));
    w_gen->Comment("    ExceptionHandler");
    w_gen->DeclareRvaToSymbol(CRASH_HANDLER_FUNCTION_NAME_STRING);
  }
  w_gen->EndXdataSection();
  w_gen->Newline();

  // Emit a RUNTIME_FUNCTION (PDATA) entry for each builtin function, as
  // documented here:
  // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64.
  w_gen->Comment(
      "pdata for all the code in the embedded blob (structs of type "
      "RUNTIME_FUNCTION).");
  w_gen->Comment("    BeginAddress");
  w_gen->Comment("    EndAddress");
  w_gen->Comment("    UnwindInfoAddress");
  w_gen->StartPdataSection();
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
            w_gen,
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
          WriteUnwindInfoEntry(w_gen,
                               std::max(prev_builtin_end_offset,
                                        builtin_start_offset -
                                            win64_unwindinfo::kRbpPrefixLength),
                               builtin_start_offset + xdata_desc[0]);
        }

        for (size_t j = 0; j < xdata_desc.size(); j++) {
          int chunk_start = xdata_desc[j];
          int chunk_end =
              (j < xdata_desc.size() - 1) ? xdata_desc[j + 1] : builtin_size;
          WriteUnwindInfoEntry(w_gen, builtin_start_offset + chunk_start,
                               builtin_start_offset + chunk_end);
        }
      }

      prev_builtin_end_offset = builtin_start_offset + builtin_size;
      w_gen->Newline();
    }
  }
  w_gen->EndPdataSection();
  w_gen->Newline();
}
#endif

}  // namespace internal
}  // namespace v8
