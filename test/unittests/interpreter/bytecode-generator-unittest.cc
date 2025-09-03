// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-generator.h"

#include <fstream>

#include "src/init/v8.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/interpreter/bytecode-expectations-parser.h"
#include "test/unittests/interpreter/bytecode-expectations-printer.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

namespace internal {
namespace interpreter {

class BytecodeGeneratorTest : public TestWithContext {
 public:
  BytecodeGeneratorTest() : printer_(isolate()) {}
  static void SetUpTestSuite() {
    i::v8_flags.allow_natives_syntax = true;
    i::v8_flags.enable_lazy_source_positions = false;
    i::v8_flags.function_context_cells = false;
    TestWithContext::SetUpTestSuite();
  }

  BytecodeExpectationsPrinter& printer() { return printer_; }

 private:
  BytecodeExpectationsPrinter printer_;
};

static const char* kGoldenFileDirectory =
    "test/unittests/interpreter/bytecode_expectations/";

struct GoldenCase {
  std::string snippet;
  std::string expectation;
};

struct GoldenFile {
  BytecodeExpectationsHeaderOptions header;
  std::vector<GoldenCase> cases;
};

GoldenFile LoadGoldenFile(const std::string& golden_filename) {
  GoldenFile ret;
  std::ifstream file((kGoldenFileDirectory + golden_filename).c_str());
  CHECK(file.is_open());

  BytecodeExpectationsParser parser(&file);
  ret.header = parser.ParseHeader();

  std::string snippet;
  while (parser.ReadNextSnippet(&snippet)) {
    std::string expected = parser.ReadToNextSeparator();
    ret.cases.emplace_back(snippet, expected);
  }

  return ret;
}

template <size_t N>
std::string BuildActual(const BytecodeExpectationsPrinter& printer,
                        std::string (&snippet_list)[N],
                        const char* prologue = nullptr,
                        const char* epilogue = nullptr) {
  std::ostringstream actual_stream;
  for (std::string snippet : snippet_list) {
    std::string source_code;
    if (prologue) source_code += prologue;
    source_code += snippet;
    if (epilogue) source_code += epilogue;
    printer.PrintExpectation(&actual_stream, source_code);
  }
  return actual_stream.str();
}

std::string BuildActual(const BytecodeExpectationsPrinter& printer,
                        const GoldenFile& golden) {
  std::ostringstream actual_stream;
  for (const GoldenCase& golden_case : golden.cases) {
    printer.PrintExpectation(&actual_stream, golden_case.snippet);
  }
  return actual_stream.str();
}

std::string BuildExpected(const BytecodeExpectationsPrinter& printer,
                          const GoldenFile& golden) {
  std::ostringstream expected_stream;
  for (const GoldenCase& golden_case : golden.cases) {
    expected_stream << "---" << std::endl;
    printer.PrintCodeSnippet(&expected_stream, golden_case.snippet);
    expected_stream << golden_case.expectation;
  }
  return expected_stream.str();
}

// inplace left trim
static inline void ltrim(std::string* str) {
  str->erase(str->begin(),
             std::find_if(str->begin(), str->end(),
                          [](unsigned char ch) { return !std::isspace(ch); }));
}

// inplace right trim
static inline void rtrim(std::string* str) {
  str->erase(std::find_if(str->rbegin(), str->rend(),
                          [](unsigned char ch) { return !std::isspace(ch); })
                 .base(),
             str->end());
}

static inline std::string trim(std::string* str) {
  ltrim(str);
  rtrim(str);
  return *str;
}

bool CompareTexts(const std::string& generated, const std::string& expected) {
  std::istringstream generated_stream(generated);
  std::istringstream expected_stream(expected);
  std::string generated_line;
  std::string expected_line;
  // Line number does not include golden file header.
  int line_number = 0;
  bool strings_match = true;

  do {
    std::getline(generated_stream, generated_line);
    std::getline(expected_stream, expected_line);

    if (!generated_stream.good() && !expected_stream.good()) {
      return strings_match;
    }

    if (!generated_stream.good()) {
      std::cerr << "Expected has extra lines after line " << line_number
                << "\n";
      std::cerr << "  Expected: '" << expected_line << "'\n";
      return false;
    } else if (!expected_stream.good()) {
      std::cerr << "Generated has extra lines after line " << line_number
                << "\n";
      std::cerr << "  Generated: '" << generated_line << "'\n";
      return false;
    }

    if (trim(&generated_line) != trim(&expected_line)) {
      std::cerr << "Inputs differ at line " << line_number << "\n";
      std::cerr << "  Generated: '" << generated_line << "'\n";
      std::cerr << "  Expected:  '" << expected_line << "'\n";
      strings_match = false;
    }
    line_number++;
  } while (true);
}

TEST_F(BytecodeGeneratorTest, PrimitiveReturnStatements) {
  GoldenFile golden = LoadGoldenFile("PrimitiveReturnStatements.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrimitiveExpressions) {
  GoldenFile golden = LoadGoldenFile("PrimitiveExpressions.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, LogicalExpressions) {
  GoldenFile golden = LoadGoldenFile("LogicalExpressions.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Parameters) {
  GoldenFile golden = LoadGoldenFile("Parameters.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, IntegerConstants) {
  GoldenFile golden = LoadGoldenFile("IntegerConstants.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, HeapNumberConstants) {
  GoldenFile golden = LoadGoldenFile("HeapNumberConstants.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StringConstants) {
  GoldenFile golden = LoadGoldenFile("StringConstants.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PropertyLoads) {
  GoldenFile golden = LoadGoldenFile("PropertyLoads.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PropertyLoadStore) {
  GoldenFile golden = LoadGoldenFile("PropertyLoadStore.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, IIFE) {
  GoldenFile golden = LoadGoldenFile("IIFE.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PropertyStores) {
  GoldenFile golden = LoadGoldenFile("PropertyStores.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PropertyCall) {
  GoldenFile golden = LoadGoldenFile("PropertyCall.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, LoadGlobal) {
  GoldenFile golden = LoadGoldenFile("LoadGlobal.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StoreGlobal) {
  GoldenFile golden = LoadGoldenFile("StoreGlobal.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CallGlobal) {
  GoldenFile golden = LoadGoldenFile("CallGlobal.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CallRuntime) {
  GoldenFile golden = LoadGoldenFile("CallRuntime.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, IfConditions) {
  GoldenFile golden = LoadGoldenFile("IfConditions.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, DeclareGlobals) {
  GoldenFile golden = LoadGoldenFile("DeclareGlobals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, BreakableBlocks) {
  GoldenFile golden = LoadGoldenFile("BreakableBlocks.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, BasicLoops) {
  GoldenFile golden = LoadGoldenFile("BasicLoops.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, UnaryOperators) {
  GoldenFile golden = LoadGoldenFile("UnaryOperators.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Typeof) {
  GoldenFile golden = LoadGoldenFile("Typeof.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CompareTypeOf) {
  GoldenFile golden = LoadGoldenFile("CompareTypeOf.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, VariableWithHint) {
  GoldenFile golden = LoadGoldenFile("VariableWithHint.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CompareBoolean) {
  GoldenFile golden = LoadGoldenFile("CompareBoolean.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CompareNil) {
  GoldenFile golden = LoadGoldenFile("CompareNil.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Delete) {
  GoldenFile golden = LoadGoldenFile("Delete.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, GlobalDelete) {
  GoldenFile golden = LoadGoldenFile("GlobalDelete.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, FunctionLiterals) {
  GoldenFile golden = LoadGoldenFile("FunctionLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, RegExpLiterals) {
  GoldenFile golden = LoadGoldenFile("RegExpLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ArrayLiterals) {
  GoldenFile golden = LoadGoldenFile("ArrayLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ObjectLiterals) {
  GoldenFile golden = LoadGoldenFile("ObjectLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, TopLevelObjectLiterals) {
  GoldenFile golden = LoadGoldenFile("TopLevelObjectLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, TryCatch) {
  GoldenFile golden = LoadGoldenFile("TryCatch.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, TryFinally) {
  GoldenFile golden = LoadGoldenFile("TryFinally.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Throw) {
  GoldenFile golden = LoadGoldenFile("Throw.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CallNew) {
  GoldenFile golden = LoadGoldenFile("CallNew.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ContextVariables) {
  GoldenFile golden = LoadGoldenFile("ContextVariables.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ContextParameters) {
  GoldenFile golden = LoadGoldenFile("ContextParameters.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, OuterContextVariables) {
  GoldenFile golden = LoadGoldenFile("OuterContextVariables.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CountOperators) {
  GoldenFile golden = LoadGoldenFile("CountOperators.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, GlobalCountOperators) {
  GoldenFile golden = LoadGoldenFile("GlobalCountOperators.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CompoundExpressions) {
  GoldenFile golden = LoadGoldenFile("CompoundExpressions.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, GlobalCompoundExpressions) {
  GoldenFile golden = LoadGoldenFile("GlobalCompoundExpressions.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CreateArguments) {
  GoldenFile golden = LoadGoldenFile("CreateArguments.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CreateRestParameter) {
  GoldenFile golden = LoadGoldenFile("CreateRestParameter.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ForIn) {
  GoldenFile golden = LoadGoldenFile("ForIn.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ForOf) {
  GoldenFile golden = LoadGoldenFile("ForOf.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Conditional) {
  GoldenFile golden = LoadGoldenFile("Conditional.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Switch) {
  GoldenFile golden = LoadGoldenFile("Switch.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, BasicBlockToBoolean) {
  GoldenFile golden = LoadGoldenFile("BasicBlockToBoolean.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, DeadCodeRemoval) {
  GoldenFile golden = LoadGoldenFile("DeadCodeRemoval.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ThisFunction) {
  GoldenFile golden = LoadGoldenFile("ThisFunction.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, NewTarget) {
  GoldenFile golden = LoadGoldenFile("NewTarget.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, RemoveRedundantLdar) {
  GoldenFile golden = LoadGoldenFile("RemoveRedundantLdar.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, GenerateTestUndetectable) {
  GoldenFile golden = LoadGoldenFile("GenerateTestUndetectable.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, AssignmentsInBinaryExpression) {
  GoldenFile golden = LoadGoldenFile("AssignmentsInBinaryExpression.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, DestructuringAssignment) {
  GoldenFile golden = LoadGoldenFile("DestructuringAssignment.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Eval) {
  GoldenFile golden = LoadGoldenFile("Eval.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, LookupSlot) {
  GoldenFile golden = LoadGoldenFile("LookupSlot.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CallLookupSlot) {
  GoldenFile golden = LoadGoldenFile("CallLookupSlot.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

// TODO(mythria): tests for variable/function declaration in lookup slots.

TEST_F(BytecodeGeneratorTest, LookupSlotInEval) {
  GoldenFile golden = LoadGoldenFile("LookupSlotInEval.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, DeleteLookupSlotInEval) {
  GoldenFile golden = LoadGoldenFile("DeleteLookupSlotInEval.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, WideRegisters) {
  GoldenFile golden = LoadGoldenFile("WideRegisters.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ConstVariable) {
  GoldenFile golden = LoadGoldenFile("ConstVariable.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, LetVariable) {
  GoldenFile golden = LoadGoldenFile("LetVariable.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ConstVariableContextSlot) {
  // TODO(mythria): Add tests for initialization of this via super calls.
  // TODO(mythria): Add tests that walk the context chain.
  GoldenFile golden = LoadGoldenFile("ConstVariableContextSlot.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, LetVariableContextSlot) {
  GoldenFile golden = LoadGoldenFile("LetVariableContextSlot.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, WithStatement) {
  GoldenFile golden = LoadGoldenFile("WithStatement.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, DoDebugger) {
  GoldenFile golden = LoadGoldenFile("DoDebugger.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ClassDeclarations) {
  GoldenFile golden = LoadGoldenFile("ClassDeclarations.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ClassAndSuperClass) {
  GoldenFile golden = LoadGoldenFile("ClassAndSuperClass.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PublicClassFields) {
  GoldenFile golden = LoadGoldenFile("PublicClassFields.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateClassFields) {
  GoldenFile golden = LoadGoldenFile("PrivateClassFields.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateClassFieldAccess) {
  GoldenFile golden = LoadGoldenFile("PrivateClassFieldAccess.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateMethodDeclaration) {
  GoldenFile golden = LoadGoldenFile("PrivateMethodDeclaration.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateMethodAccess) {
  GoldenFile golden = LoadGoldenFile("PrivateMethodAccess.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateAccessorAccess) {
  GoldenFile golden = LoadGoldenFile("PrivateAccessorAccess.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StaticPrivateMethodDeclaration) {
  GoldenFile golden = LoadGoldenFile("StaticPrivateMethodDeclaration.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StaticPrivateMethodAccess) {
  GoldenFile golden = LoadGoldenFile("StaticPrivateMethodAccess.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, PrivateAccessorDeclaration) {
  GoldenFile golden = LoadGoldenFile("PrivateAccessorDeclaration.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StaticClassFields) {
  GoldenFile golden = LoadGoldenFile("StaticClassFields.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Generators) {
  GoldenFile golden = LoadGoldenFile("Generators.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, AsyncGenerators) {
  GoldenFile golden = LoadGoldenFile("AsyncGenerators.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, Modules) {
  GoldenFile golden = LoadGoldenFile("Modules.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, AsyncModules) {
  GoldenFile golden = LoadGoldenFile("AsyncModules.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, SuperCallAndSpread) {
  GoldenFile golden = LoadGoldenFile("SuperCallAndSpread.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, CallAndSpread) {
  GoldenFile golden = LoadGoldenFile("CallAndSpread.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, NewAndSpread) {
  GoldenFile golden = LoadGoldenFile("NewAndSpread.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ForAwaitOf) {
  GoldenFile golden = LoadGoldenFile("ForAwaitOf.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StandardForLoop) {
  GoldenFile golden = LoadGoldenFile("StandardForLoop.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ForOfLoop) {
  GoldenFile golden = LoadGoldenFile("ForOfLoop.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, StringConcat) {
  GoldenFile golden = LoadGoldenFile("StringConcat.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, TemplateLiterals) {
  GoldenFile golden = LoadGoldenFile("TemplateLiterals.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ElideRedundantLoadOperationOfImmutableContext) {
  GoldenFile golden =
      LoadGoldenFile("ElideRedundantLoadOperationOfImmutableContext.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

TEST_F(BytecodeGeneratorTest, ElideRedundantHoleChecks) {
  GoldenFile golden = LoadGoldenFile("ElideRedundantHoleChecks.golden");
  printer().set_options(golden.header);
  CHECK(CompareTexts(BuildActual(printer(), golden),
                     BuildExpected(printer(), golden)));
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
