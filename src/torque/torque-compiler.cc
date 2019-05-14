// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/torque-compiler.h"

#include <fstream>
#include "src/torque/declarable.h"
#include "src/torque/declaration-visitor.h"
#include "src/torque/global-context.h"
#include "src/torque/implementation-visitor.h"
#include "src/torque/torque-parser.h"
#include "src/torque/type-oracle.h"

namespace v8 {
namespace internal {
namespace torque {

namespace {

base::Optional<std::string> ReadFile(const std::string& path) {
  std::ifstream file_stream(path);
  if (!file_stream.good()) return base::nullopt;

  return std::string{std::istreambuf_iterator<char>(file_stream),
                     std::istreambuf_iterator<char>()};
}

void ReadAndParseTorqueFile(const std::string& path) {
  SourceId source_id = SourceFileMap::AddSource(path);
  CurrentSourceFile::Scope source_id_scope(source_id);

  // path might be either a normal file path or an encoded URI.
  auto maybe_content = ReadFile(path);
  if (!maybe_content) {
    if (auto maybe_path = FileUriDecode(path)) {
      maybe_content = ReadFile(*maybe_path);
    }
  }

  if (!maybe_content) {
    ReportErrorWithoutPosition("Cannot open file path/uri: ", path);
  }

  ParseTorque(*maybe_content);
}

void CompileCurrentAst(TorqueCompilerOptions options) {
  GlobalContext::Scope global_context(std::move(CurrentAst::Get()));
  if (options.verbose) GlobalContext::SetVerbose();
  if (options.collect_language_server_data) {
    GlobalContext::SetCollectLanguageServerData();
  }
  if (options.force_assert_statements) {
    GlobalContext::SetForceAssertStatements();
  }
  TypeOracle::Scope type_oracle;

  // Two-step process of predeclaration + resolution allows to resolve type
  // declarations independent of the order they are given.
  PredeclarationVisitor::Predeclare(GlobalContext::Get().ast());
  PredeclarationVisitor::ResolvePredeclarations();

  // Process other declarations.
  DeclarationVisitor::Visit(GlobalContext::Get().ast());

  // A class types' fields are resolved here, which allows two class fields to
  // mutually refer to each others.
  TypeOracle::FinalizeClassTypes();

  ImplementationVisitor implementation_visitor;
  for (Namespace* n : GlobalContext::Get().GetNamespaces()) {
    implementation_visitor.BeginNamespaceFile(n);
  }

  implementation_visitor.VisitAllDeclarables();

  std::string output_directory = options.output_directory;
  if (output_directory.length() != 0) {
    std::string output_header_path = output_directory;
    output_header_path += "/builtin-definitions-from-dsl.h";
    implementation_visitor.GenerateBuiltinDefinitions(output_header_path);

    output_header_path = output_directory + "/class-definitions-from-dsl.h";
    implementation_visitor.GenerateClassDefinitions(output_header_path);

    std::string output_source_path =
        output_directory + "/objects-printer-from-dsl.cc";
    implementation_visitor.GeneratePrintDefinitions(output_source_path);

    implementation_visitor.GenerateClassVerifiers(output_directory);

    for (Namespace* n : GlobalContext::Get().GetNamespaces()) {
      implementation_visitor.EndNamespaceFile(n);
      implementation_visitor.GenerateImplementation(output_directory, n);
    }
  }

  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::SetGlobalContext(std::move(GlobalContext::Get()));
  }
}

}  // namespace

TorqueCompilerResult CompileTorque(const std::string& source,
                                   TorqueCompilerOptions options) {
  SourceFileMap::Scope source_map_scope;
  CurrentSourceFile::Scope no_file_scope(SourceFileMap::AddSource("<torque>"));
  CurrentAst::Scope ast_scope;
  LintErrors::Scope lint_errors_scope;
  LanguageServerData::Scope server_data_scope;

  TorqueCompilerResult result;
  try {
    ParseTorque(source);
    CompileCurrentAst(options);
  } catch (TorqueError& error) {
    result.error = error;
  }

  result.source_file_map = SourceFileMap::Get();
  result.language_server_data = std::move(LanguageServerData::Get());
  result.lint_errors = LintErrors::Get();

  return result;
}

TorqueCompilerResult CompileTorque(std::vector<std::string> files,
                                   TorqueCompilerOptions options) {
  SourceFileMap::Scope source_map_scope;
  CurrentSourceFile::Scope unknown_source_file_scope(SourceId::Invalid());
  CurrentAst::Scope ast_scope;
  LintErrors::Scope lint_errors_scope;
  LanguageServerData::Scope server_data_scope;

  TorqueCompilerResult result;
  try {
    for (const auto& path : files) ReadAndParseTorqueFile(path);
    CompileCurrentAst(options);
  } catch (TorqueError& error) {
    result.error = error;
  }

  result.source_file_map = SourceFileMap::Get();
  result.language_server_data = std::move(LanguageServerData::Get());
  result.lint_errors = LintErrors::Get();

  return result;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
