// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"

namespace v8 {
namespace internal {

// V8_CC_MSVC is true for both MSVC and clang on windows. clang can handle
// __asm__-style inline assembly but MSVC cannot, and thus we need a more
// precise compiler detection that can distinguish between the two. clang on
// windows sets both __clang__ and _MSC_VER, MSVC sets only _MSC_VER.
#if defined(_MSC_VER) && !defined(__clang__)
#define V8_COMPILER_IS_MSVC
#endif

// MSVC uses MASM for x86 and x64, while it has a ARMASM for ARM32 and
// ARMASM64 for ARM64. Since ARMASM and ARMASM64 accept a slightly tweaked
// version of ARM assembly language, they are referred to together in Visual
// Studio project files as MARMASM.
//
// ARM assembly language docs:
// http://infocenter.arm.com/help/topic/com.arm.doc.dui0802b/index.html
// Microsoft ARM assembler and assembly language docs:
// https://docs.microsoft.com/en-us/cpp/assembler/arm/arm-assembler-reference
#if defined(V8_COMPILER_IS_MSVC)
#if defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_ARM)
#define V8_ASSEMBLER_IS_MARMASM
#elif defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_X64)
#define V8_ASSEMBLER_IS_MASM
#else
#error Unknown Windows assembler target architecture.
#endif
#endif

// Name mangling.
// Symbols are prefixed with an underscore on 32-bit architectures.
#if !defined(V8_TARGET_ARCH_X64) && !defined(V8_TARGET_ARCH_ARM64)
#define SYMBOL_PREFIX "_"
#else
#define SYMBOL_PREFIX ""
#endif

// Notes:
//
// Cross-bitness builds are unsupported. It's thus safe to detect bitness
// through compile-time defines.
//
// Cross-compiler builds (e.g. with mixed use of clang / MSVC) are likewise
// unsupported and hence the compiler can also be detected through compile-time
// defines.

namespace {

const char* DirectiveAsString(DataDirective directive) {
#if defined(V8_ASSEMBLER_IS_MASM)
  switch (directive) {
    case kByte:
      return "BYTE";
    case kLong:
      return "DWORD";
    case kQuad:
      return "QWORD";
    default:
      UNREACHABLE();
  }
#elif defined(V8_ASSEMBLER_IS_MARMASM)
  switch (directive) {
    case kByte:
      return "DCB";
    case kLong:
      return "DCDU";
    case kQuad:
      return "DCQU";
    default:
      UNREACHABLE();
  }
#else
  switch (directive) {
    case kByte:
      return ".byte";
    case kLong:
      return ".long";
    case kQuad:
      return ".quad";
    case kOcta:
      return ".octa";
  }
  UNREACHABLE();
#endif
}

}  // namespace

// Windows, MSVC, not arm/arm64.
// -----------------------------------------------------------------------------

#if defined(V8_ASSEMBLER_IS_MASM)

// For MSVC builds we emit assembly in MASM syntax.
// See https://docs.microsoft.com/en-us/cpp/assembler/masm/directives-reference.

void PlatformEmbeddedFileWriterWin::SectionText() { fprintf(fp_, ".CODE\n"); }

void PlatformEmbeddedFileWriterWin::SectionData() { fprintf(fp_, ".DATA\n"); }

void PlatformEmbeddedFileWriterWin::SectionRoData() {
  fprintf(fp_, ".CONST\n");
}

void PlatformEmbeddedFileWriterWin::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  fprintf(fp_, "%s%s %s %d\n", SYMBOL_PREFIX, name, DirectiveAsString(kLong),
          value);
}

void PlatformEmbeddedFileWriterWin::DeclarePointerToSymbol(const char* name,
                                                           const char* target) {
  DeclareSymbolGlobal(name);
  fprintf(fp_, "%s%s %s %s%s\n", SYMBOL_PREFIX, name,
          DirectiveAsString(PointerSizeDirective()), SYMBOL_PREFIX, target);
}

void PlatformEmbeddedFileWriterWin::StartPdataSection() {
  fprintf(fp_, "OPTION DOTNAME\n");
  fprintf(fp_, ".pdata SEGMENT DWORD READ ''\n");
}

void PlatformEmbeddedFileWriterWin::EndPdataSection() {
  fprintf(fp_, ".pdata ENDS\n");
}

void PlatformEmbeddedFileWriterWin::StartXdataSection() {
  fprintf(fp_, "OPTION DOTNAME\n");
  fprintf(fp_, ".xdata SEGMENT DWORD READ ''\n");
}

void PlatformEmbeddedFileWriterWin::EndXdataSection() {
  fprintf(fp_, ".xdata ENDS\n");
}

void PlatformEmbeddedFileWriterWin::DeclareExternalFunction(const char* name) {
  fprintf(fp_, "EXTERN %s : PROC\n", name);
}

void PlatformEmbeddedFileWriterWin::DeclareRvaToSymbol(const char* name,
                                                       uint64_t offset) {
  if (offset > 0) {
    fprintf(fp_, "DD IMAGEREL %s+%llu\n", name, offset);
  } else {
    fprintf(fp_, "DD IMAGEREL %s\n", name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareSymbolGlobal(const char* name) {
  fprintf(fp_, "PUBLIC %s%s\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::AlignToCodeAlignment() {
  // Diverges from other platforms due to compile error
  // 'invalid combination with segment alignment'.
  fprintf(fp_, "ALIGN 4\n");
}

void PlatformEmbeddedFileWriterWin::AlignToDataAlignment() {
  fprintf(fp_, "ALIGN 4\n");
}

void PlatformEmbeddedFileWriterWin::Comment(const char* string) {
  fprintf(fp_, "; %s\n", string);
}

void PlatformEmbeddedFileWriterWin::DeclareLabel(const char* name) {
  fprintf(fp_, "%s%s LABEL %s\n", SYMBOL_PREFIX, name,
          DirectiveAsString(kByte));
}

void PlatformEmbeddedFileWriterWin::SourceInfo(int fileid, const char* filename,
                                               int line) {
  // TODO(mvstanton): output source information for MSVC.
  // Its syntax is #line <line> "<filename>"
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionBegin(const char* name) {
  fprintf(fp_, "%s%s PROC\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionEnd(const char* name) {
  fprintf(fp_, "%s%s ENDP\n", SYMBOL_PREFIX, name);
}

int PlatformEmbeddedFileWriterWin::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0%" PRIx64 "h", value);
}

void PlatformEmbeddedFileWriterWin::FilePrologue() {
  if (target_arch_ != EmbeddedTargetArch::kX64) {
    fprintf(fp_, ".MODEL FLAT\n");
  }
}

void PlatformEmbeddedFileWriterWin::DeclareExternalFilename(
    int fileid, const char* filename) {}

void PlatformEmbeddedFileWriterWin::FileEpilogue() { fprintf(fp_, "END\n"); }

int PlatformEmbeddedFileWriterWin::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

// Windows, MSVC, arm/arm64.
// -----------------------------------------------------------------------------

#elif defined(V8_ASSEMBLER_IS_MARMASM)

// The AARCH64 ABI requires instructions be 4-byte-aligned and Windows does
// not have a stricter alignment requirement (see the TEXTAREA macro of
// kxarm64.h in the Windows SDK), so code is 4-byte-aligned.
// The data fields in the emitted assembly tend to be accessed with 8-byte
// LDR instructions, so data is 8-byte-aligned.
//
// armasm64's warning A4228 states
//     Alignment value exceeds AREA alignment; alignment not guaranteed
// To ensure that ALIGN directives are honored, their values are defined as
// equal to their corresponding AREA's ALIGN attributes.

#define ARM64_DATA_ALIGNMENT_POWER (3)
#define ARM64_DATA_ALIGNMENT (1 << ARM64_DATA_ALIGNMENT_POWER)
#define ARM64_CODE_ALIGNMENT_POWER (2)
#define ARM64_CODE_ALIGNMENT (1 << ARM64_CODE_ALIGNMENT_POWER)

void PlatformEmbeddedFileWriterWin::SectionText() {
  fprintf(fp_, "  AREA |.text|, CODE, ALIGN=%d, READONLY\n",
          ARM64_CODE_ALIGNMENT_POWER);
}

void PlatformEmbeddedFileWriterWin::SectionData() {
  fprintf(fp_, "  AREA |.data|, DATA, ALIGN=%d, READWRITE\n",
          ARM64_DATA_ALIGNMENT_POWER);
}

void PlatformEmbeddedFileWriterWin::SectionRoData() {
  fprintf(fp_, "  AREA |.rodata|, DATA, ALIGN=%d, READONLY\n",
          ARM64_DATA_ALIGNMENT_POWER);
}

void PlatformEmbeddedFileWriterWin::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  fprintf(fp_, "%s%s %s %d\n", SYMBOL_PREFIX, name, DirectiveAsString(kLong),
          value);
}

void PlatformEmbeddedFileWriterWin::DeclarePointerToSymbol(const char* name,
                                                           const char* target) {
  DeclareSymbolGlobal(name);
  fprintf(fp_, "%s%s %s %s%s\n", SYMBOL_PREFIX, name,
          DirectiveAsString(PointerSizeDirective()), SYMBOL_PREFIX, target);
}

void PlatformEmbeddedFileWriterWin::DeclareSymbolGlobal(const char* name) {
  fprintf(fp_, "  EXPORT %s%s\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::AlignToCodeAlignment() {
  fprintf(fp_, "  ALIGN %d\n", ARM64_CODE_ALIGNMENT);
}

void PlatformEmbeddedFileWriterWin::AlignToDataAlignment() {
  fprintf(fp_, "  ALIGN %d\n", ARM64_DATA_ALIGNMENT);
}

void PlatformEmbeddedFileWriterWin::Comment(const char* string) {
  fprintf(fp_, "; %s\n", string);
}

void PlatformEmbeddedFileWriterWin::DeclareLabel(const char* name) {
  fprintf(fp_, "%s%s\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::SourceInfo(int fileid, const char* filename,
                                               int line) {
  // TODO(mvstanton): output source information for MSVC.
  // Its syntax is #line <line> "<filename>"
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionBegin(const char* name) {
  fprintf(fp_, "%s%s FUNCTION\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionEnd(const char* name) {
  fprintf(fp_, "  ENDFUNC\n");
}

int PlatformEmbeddedFileWriterWin::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0x%" PRIx64, value);
}

void PlatformEmbeddedFileWriterWin::FilePrologue() {}

void PlatformEmbeddedFileWriterWin::DeclareExternalFilename(
    int fileid, const char* filename) {}

void PlatformEmbeddedFileWriterWin::FileEpilogue() { fprintf(fp_, "  END\n"); }

int PlatformEmbeddedFileWriterWin::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

#undef ARM64_DATA_ALIGNMENT_POWER
#undef ARM64_DATA_ALIGNMENT
#undef ARM64_CODE_ALIGNMENT_POWER
#undef ARM64_CODE_ALIGNMENT

// All Windows builds without MSVC.
// -----------------------------------------------------------------------------

#else

void PlatformEmbeddedFileWriterWin::SectionText() {
  fprintf(fp_, ".section .text\n");
}

void PlatformEmbeddedFileWriterWin::SectionData() {
  fprintf(fp_, ".section .data\n");
}

void PlatformEmbeddedFileWriterWin::SectionRoData() {
  fprintf(fp_, ".section .rdata\n");
}

void PlatformEmbeddedFileWriterWin::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  DeclareLabel(name);
  IndentedDataDirective(kLong);
  fprintf(fp_, "%d", value);
  Newline();
}

void PlatformEmbeddedFileWriterWin::DeclarePointerToSymbol(const char* name,
                                                           const char* target) {
  DeclareSymbolGlobal(name);
  DeclareLabel(name);
  fprintf(fp_, "  %s %s%s\n", DirectiveAsString(PointerSizeDirective()),
          SYMBOL_PREFIX, target);
}

void PlatformEmbeddedFileWriterWin::StartPdataSection() {
  fprintf(fp_, ".section .pdata\n");
}

void PlatformEmbeddedFileWriterWin::EndPdataSection() {}

void PlatformEmbeddedFileWriterWin::StartXdataSection() {
  fprintf(fp_, ".section .xdata\n");
}

void PlatformEmbeddedFileWriterWin::EndXdataSection() {}

void PlatformEmbeddedFileWriterWin::DeclareExternalFunction(const char* name) {}

void PlatformEmbeddedFileWriterWin::DeclareRvaToSymbol(const char* name,
                                                       uint64_t offset) {
  if (offset > 0) {
    fprintf(fp_, ".rva %s + %" PRIu64 "\n", name, offset);
  } else {
    fprintf(fp_, ".rva %s\n", name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareSymbolGlobal(const char* name) {
  fprintf(fp_, ".global %s%s\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::AlignToCodeAlignment() {
  fprintf(fp_, ".balign 32\n");
}

void PlatformEmbeddedFileWriterWin::AlignToDataAlignment() {
  // On Windows ARM64, s390, PPC and possibly more platforms, aligned load
  // instructions are used to retrieve v8_Default_embedded_blob_ and/or
  // v8_Default_embedded_blob_size_. The generated instructions require the
  // load target to be aligned at 8 bytes (2^3).
  fprintf(fp_, ".balign 8\n");
}

void PlatformEmbeddedFileWriterWin::Comment(const char* string) {
  fprintf(fp_, "// %s\n", string);
}

void PlatformEmbeddedFileWriterWin::DeclareLabel(const char* name) {
  fprintf(fp_, "%s%s:\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::SourceInfo(int fileid, const char* filename,
                                               int line) {
  fprintf(fp_, ".loc %d %d\n", fileid, line);
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionBegin(const char* name) {
  DeclareLabel(name);

  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    // Windows ARM64 assembly is in GAS syntax, but ".type" is invalid directive
    // in PE/COFF for Windows.
  } else {
    // The directives for inserting debugging information on Windows come
    // from the PE (Portable Executable) and COFF (Common Object File Format)
    // standards. Documented here:
    // https://docs.microsoft.com/en-us/windows/desktop/debug/pe-format
    //
    // .scl 2 means StorageClass external.
    // .type 32 means Type Representation Function.
    fprintf(fp_, ".def %s%s; .scl 2; .type 32; .endef;\n", SYMBOL_PREFIX, name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionEnd(const char* name) {}

int PlatformEmbeddedFileWriterWin::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0x%" PRIx64, value);
}

void PlatformEmbeddedFileWriterWin::FilePrologue() {}

void PlatformEmbeddedFileWriterWin::DeclareExternalFilename(
    int fileid, const char* filename) {
  // Replace any Windows style paths (backslashes) with forward
  // slashes.
  std::string fixed_filename(filename);
  std::replace(fixed_filename.begin(), fixed_filename.end(), '\\', '/');
  fprintf(fp_, ".file %d \"%s\"\n", fileid, fixed_filename.c_str());
}

void PlatformEmbeddedFileWriterWin::FileEpilogue() {}

int PlatformEmbeddedFileWriterWin::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

#endif

#undef SYMBOL_PREFIX
#undef V8_ASSEMBLER_IS_MASM
#undef V8_ASSEMBLER_IS_MARMASM
#undef V8_COMPILER_IS_MSVC

}  // namespace internal
}  // namespace v8
