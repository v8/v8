// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_H_
#define V8_CODEGEN_H_

#include "code-stubs.h"
#include "runtime.h"

// Include the declaration of the architecture defined class CodeGenerator.
// The contract  to the shared code is that the the CodeGenerator is a subclass
// of Visitor and that the following methods are available publicly:
//   MakeCode
//   MakeCodePrologue
//   MakeCodeEpilogue
//   masm
//   frame
//   script
//   has_valid_frame
//   SetFrame
//   DeleteFrame
//   allocator
//   AddDeferred
//   in_spilled_code
//   set_in_spilled_code
//   RecordPositions
//
// These methods are either used privately by the shared code or implemented as
// shared code:
//   CodeGenerator
//   ~CodeGenerator
//   Generate
//   ComputeLazyCompile
//   BuildFunctionInfo
//   ProcessDeclarations
//   DeclareGlobals
//   CheckForInlineRuntimeCall
//   AnalyzeCondition
//   CodeForFunctionPosition
//   CodeForReturnPosition
//   CodeForStatementPosition
//   CodeForDoWhileConditionPosition
//   CodeForSourcePosition

enum TypeofState { INSIDE_TYPEOF, NOT_INSIDE_TYPEOF };

#if V8_TARGET_ARCH_IA32
#include "ia32/codegen-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "x64/codegen-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "arm64/codegen-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "arm/codegen-arm.h"
#elif V8_TARGET_ARCH_MIPS
#include "mips/codegen-mips.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {


class CompilationInfo;


class CodeGenerator {
 public:
  // Printing of AST, etc. as requested by flags.
  static void MakeCodePrologue(CompilationInfo* info, const char* kind);

  // Allocate and install the code.
  static Handle<Code> MakeCodeEpilogue(MacroAssembler* masm,
                                       Code::Flags flags,
                                       CompilationInfo* info);

  // Print the code after compiling it.
  static void PrintCode(Handle<Code> code, CompilationInfo* info);

  static bool ShouldGenerateLog(Isolate* isolate, Expression* type);

  static bool RecordPositions(MacroAssembler* masm,
                              int pos,
                              bool right_here = false);

 private:
  DISALLOW_COPY_AND_ASSIGN(CodeGenerator);
};


// Results of the library implementation of transcendental functions may differ
// from the one we use in our generated code.  Therefore we use the same
// generated code both in runtime and compiled code.
typedef double (*UnaryMathFunction)(double x);

UnaryMathFunction CreateExpFunction();
UnaryMathFunction CreateSqrtFunction();


class ElementsTransitionGenerator : public AllStatic {
 public:
  // If |mode| is set to DONT_TRACK_ALLOCATION_SITE,
  // |allocation_memento_found| may be NULL.
  static void GenerateMapChangeElementsTransition(MacroAssembler* masm,
      AllocationSiteMode mode,
      Label* allocation_memento_found);
  static void GenerateSmiToDouble(MacroAssembler* masm,
                                  AllocationSiteMode mode,
                                  Label* fail);
  static void GenerateDoubleToObject(MacroAssembler* masm,
                                     AllocationSiteMode mode,
                                     Label* fail);

 private:
  DISALLOW_COPY_AND_ASSIGN(ElementsTransitionGenerator);
};

static const int kNumberDictionaryProbes = 4;


} }  // namespace v8::internal

#endif  // V8_CODEGEN_H_
