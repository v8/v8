// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/corpus/material.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "experimental/redline/base/file_util.h"
#include "experimental/redline/base/log.h"
#include "experimental/redline/base/time_util.h"
#include "experimental/redline/core/ast_builder.h"
#include "experimental/redline/core/tunable.h"
#include "experimental/redline/engine/importer.h"
#include "experimental/redline/fast/ast.h"
#include "experimental/redline/fast/requirements.h"
#include "experimental/redline/fast/scope.h"

namespace redline {

REDLINE_TUNABLE(kMaterialPartnerChance, "material.partner_chance", 0.35, 0.0,
                1.0,
                "Probability that Splice and Combine pick a partner from "
                "`MaterialBank`.");
REDLINE_TUNABLE(kMaterialFuseChance, "material.fuse_chance", 0.15, 0.0, 1.0,
                "Probability that a mutation chain starts with a `Fuse` root.");

namespace {

constexpr char kLog[] = "material";

constexpr std::string_view kSkippedFileNames[] = {
    "d8-os.js",
    "d8-readbuffer.js",
    "d8-arguments.js",
    "d8-finalization-registry-quit",
    "d8-finalization-registry-quit.js",
    "regexp-pcre.js",
    "unicode-test.js",
    "unicodelctest.js",
    "unicodelctest-no-optimization.js",
    "regress-444805.js",
    "regress-crbug-489597.js",
    "regress-crbug-620253.js",
    "regress-8510.js",
    "regress-352044-02-n.js",
    "regress-crbug-400504688.js",
    "regress-392928803.js",
    "random-bit-correlations",
    "random-bit-correlations.js",
};

constexpr std::string_view kSkippedDirSegments[] = {
    "embenchen",
    "poppler",
    "sqlite",
    "v8/test/inspector",
    "test/inspector",
    "v8/test/js-perf-test/Modules",
    "test/js-perf-test/Modules",
    "v8/test/message",
    "test/message",
    "v8/test/mjsunit/tools",
    "test/mjsunit/tools",
    "v8/test/mjsunit/d8/cwd",
    "test/mjsunit/d8/cwd",
    "mozilla/data/e4x",
    "spidermonkey/non262/ReadableStream",
};

std::string NormalizeSlashes(std::string_view path) {
  std::string out(path);
  std::replace(out.begin(), out.end(), '\\', '/');
  return out;
}

bool HasDirSubpath(std::string_view dir, std::string_view subpath) {
  if (dir.empty() || subpath.empty()) {
    return false;
  }
  size_t pos = 0;
  while ((pos = dir.find(subpath, pos)) != std::string_view::npos) {
    bool left_ok = (pos == 0 || dir[pos - 1] == '/');
    size_t end = pos + subpath.size();
    bool right_ok = (end == dir.size() || dir[end] == '/');
    if (left_ok && right_ok) {
      return true;
    }
    pos++;
  }
  return false;
}

std::string_view TrimLeftWs(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  return s;
}

std::string NormalizeFlagName(std::string_view token) {
  size_t eq = token.find('=');
  std::string_view name = token.substr(0, eq);
  std::string out(name);
  for (size_t i = 2; i < out.size(); i++) {
    if (out[i] == '_') {
      out[i] = '-';
    }
  }
  return out;
}

bool IsDiscouragedFlagToken(std::string_view token) {
  if (token.ends_with(".mjs")) {
    return true;
  }
  if (!token.starts_with("--")) {
    return false;
  }
  std::string name = NormalizeFlagName(token);
  if (name == "--module" || name == "--correctness-fuzzer-suppressions" ||
      name == "--expose-trigger-failure" || name == "--sandbox-testing" ||
      name == "--sandbox-fuzzing") {
    return true;
  }
  if (name.starts_with("--icu-data-file")) {
    return true;
  }
  std::string_view sv(name);
  if (sv.starts_with("--harmony-")) {
    return !sv.substr(10).starts_with("shipping");
  }
  if (sv.starts_with("--js-")) {
    std::string_view rest = sv.substr(5);
    return !rest.starts_with("staging") && !rest.starts_with("shipping");
  }
  return false;
}

bool IsModuleHeaderSource(std::string_view source) {
  size_t pos = 0;
  while (pos < source.size()) {
    size_t end = source.find('\n', pos);
    if (end == std::string_view::npos) {
      end = source.size();
    }
    std::string_view line = source.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line = TrimLeftWs(line);
    if (line.starts_with("// MODULE") &&
        (line.size() == 9 || line[9] == ' ' || line[9] == '\t')) {
      return true;
    }
    pos = end + 1;
  }
  return false;
}

bool IsDirectiveStmt(const fast::Statement* s) {
  auto* e = s->As<fast::ExpressionStatement>();
  return e != nullptr && e->is_directive;
}

bool ExtractZeroParamIIFE(fast::Statement* stmt,
                          const fast::FunctionData** fn_out,
                          fast::Node** fn_node_out) {
  if (IsDirectiveStmt(stmt)) {
    return false;
  }
  auto* expr_stmt = stmt->As<fast::ExpressionStatement>();
  if (expr_stmt == nullptr || expr_stmt->expression == nullptr) {
    return false;
  }
  auto* call = expr_stmt->expression->As<fast::CallExpression>();
  if (call == nullptr || call->optional || !call->arguments.empty() ||
      call->callee == nullptr) {
    return false;
  }
  fast::Node* callee = call->callee;
  if (!callee->Is<fast::FunctionExpression>() &&
      !callee->Is<fast::ArrowFunctionExpression>()) {
    return false;
  }
  const fast::FunctionData* fn = fast::AsFunctionData(callee);
  if (fn == nullptr || fn->is_async || fn->is_generator ||
      !fn->params.empty() || fn->body == nullptr || fn->body->body.empty()) {
    return false;
  }
  *fn_out = fn;
  *fn_node_out = callee;
  return true;
}

bool HasStrictDirective(const fast::BlockStatement* block) {
  for (const fast::Statement* s : block->body) {
    if (!IsDirectiveStmt(s)) {
      break;
    }
    if (s->Cast<fast::ExpressionStatement>()->directive == "use strict") {
      return true;
    }
  }
  return false;
}

bool HasSelfNameReference(const fast::FunctionData* fn, fast::Node* fn_node,
                          const fast::ScopeAnalysis& scopes) {
  if (fn->name == nullptr) {
    return false;
  }
  for (const fast::Binding* b : scopes.bindings()) {
    if (b->declaration == fn_node &&
        b->kind == fast::BindingKind::kFunctionName) {
      return !b->references.empty();
    }
  }
  return true;
}

std::string FreshName(std::string_view prefix, int* counter,
                      const absl::flat_hash_set<std::string>& used) {
  while (true) {
    std::string candidate = StringPrintf(
        "%.*s%d", static_cast<int>(prefix.size()), prefix.data(), (*counter)++);
    if (!used.contains(candidate)) {
      return candidate;
    }
  }
}

void RenameCollidingScopeBindings(fast::Tree* tree, fast::Scope* scope,
                                  absl::flat_hash_set<std::string>* top_names,
                                  absl::flat_hash_set<std::string>* all_names,
                                  int* counter) {
  if (scope == nullptr) {
    return;
  }
  for (fast::Binding* b : scope->bindings) {
    if (b->kind == fast::BindingKind::kImplicit ||
        b->kind == fast::BindingKind::kFunctionName ||
        b->kind == fast::BindingKind::kClassName) {
      continue;
    }
    if (top_names->contains(b->name)) {
      std::string fresh = FreshName("_m", counter, *all_names);
      all_names->insert(fresh);
      top_names->insert(fresh);
      fast::RenameBinding(tree, b, fresh);
    } else {
      top_names->emplace(b->name);
    }
  }
}

void EmitSplitLexicalDecls(fast::Tree* out, const fast::VariableDeclaration* vd,
                           std::vector<fast::Statement*>* hoisted_lets,
                           std::vector<fast::Statement*>* exec_run) {
  for (const fast::VariableDeclarator* d : vd->declarations) {
    if (d == nullptr || d->target == nullptr) {
      continue;
    }
    std::vector<fast::Identifier*> ids;
    fast::CollectBoundIdentifiers(d->target, &ids);
    if (!ids.empty()) {
      auto* decl = out->New<fast::VariableDeclaration>();
      decl->declaration_kind = fast::VariableKind::kLet;
      for (const fast::Identifier* id : ids) {
        auto* declarator = out->New<fast::VariableDeclarator>();
        declarator->target = mk::Id(out, id->name);
        decl->declarations.push_back(declarator);
      }
      hoisted_lets->push_back(decl);
    }
    if (d->init != nullptr) {
      exec_run->push_back(mk::ExprStmt(
          out, mk::Assign(out, out->Clone(d->target), out->Clone(d->init))));
    }
  }
}

bool IsUsingDeclaration(const fast::Statement* stmt) {
  auto* vd = stmt->As<fast::VariableDeclaration>();
  return vd != nullptr &&
         (vd->declaration_kind == fast::VariableKind::kUsing ||
          vd->declaration_kind == fast::VariableKind::kAwaitUsing);
}

// Returns true when the definition of `cls` can run donor code: an `extends`
// clause, a computed key, a static block or a static field initializer.
bool ClassRunsCodeAtDefinition(const fast::ClassData* cls) {
  if (cls->super_class != nullptr) {
    return true;
  }
  if (cls->body == nullptr) {
    return false;
  }
  for (const fast::Node* member : cls->body->members) {
    if (member->Is<fast::StaticBlock>()) {
      return true;
    }
    if (auto* m = member->As<fast::MethodDefinition>()) {
      if (m->computed) {
        return true;
      }
    } else if (auto* p = member->As<fast::PropertyDefinition>()) {
      if (p->computed || (p->is_static && p->value != nullptr)) {
        return true;
      }
    }
  }
  return false;
}

// Splits `class C ...` into `let C;` in `hoisted_lets` and
// `C = class C ...;` in `exec_run`.
void EmitSplitClassDecl(fast::Tree* out, const fast::ClassDeclaration* cls,
                        std::vector<fast::Statement*>* hoisted_lets,
                        std::vector<fast::Statement*>* exec_run) {
  auto* decl = out->New<fast::VariableDeclaration>();
  decl->declaration_kind = fast::VariableKind::kLet;
  auto* declarator = out->New<fast::VariableDeclarator>();
  declarator->target = mk::Id(out, cls->name->name);
  decl->declarations.push_back(declarator);
  hoisted_lets->push_back(decl);

  auto* expr = out->New<fast::ClassExpression>();
  expr->name = mk::Id(out, cls->name->name);
  expr->super_class = out->Clone(cls->super_class);
  expr->body = out->Clone(cls->body);
  exec_run->push_back(
      mk::ExprStmt(out, mk::Assign(out, mk::Id(out, cls->name->name), expr)));
}

void EmitIsolatedDonor(fast::Tree* out, const fast::Program* donor,
                       std::vector<fast::Statement*>* body_out) {
  std::vector<fast::Statement*> hoisted_lets;
  std::vector<fast::Statement*> exec_run;
  auto flush = [&] {
    for (fast::Statement* let_stmt : hoisted_lets) {
      body_out->push_back(let_stmt);
    }
    hoisted_lets.clear();
    if (!exec_run.empty()) {
      body_out->push_back(mk::Try(out, std::move(exec_run), "", {}));
      exec_run.clear();
    }
  };

  for (const fast::Statement* stmt : donor->body) {
    if (stmt == nullptr || stmt->Is<fast::EmptyStatement>() ||
        IsDirectiveStmt(stmt)) {
      continue;
    }
    if (auto* cls = stmt->As<fast::ClassDeclaration>()) {
      if (cls->name != nullptr && ClassRunsCodeAtDefinition(cls)) {
        EmitSplitClassDecl(out, cls, &hoisted_lets, &exec_run);
      } else {
        flush();
        body_out->push_back(out->Clone(stmt));
      }
      continue;
    }
    if (stmt->Is<fast::FunctionDeclaration>()) {
      flush();
      body_out->push_back(out->Clone(stmt));
      continue;
    }
    if (auto* vd = stmt->As<fast::VariableDeclaration>()) {
      // `using` is not valid at the top level of a script, so it stays in the
      // `try` block.
      if (vd->declaration_kind == fast::VariableKind::kVar ||
          IsUsingDeclaration(vd)) {
        exec_run.push_back(out->Clone(vd));
      } else {
        EmitSplitLexicalDecls(out, vd, &hoisted_lets, &exec_run);
      }
      continue;
    }
    exec_run.push_back(out->Clone(stmt));
  }
  flush();
}

}  // namespace

double MaterialPartnerChance() { return kMaterialPartnerChance(); }
double MaterialFuseChance() { return kMaterialFuseChance(); }

bool IsMaterialSkippedPath(std::string_view path) {
  std::string norm = NormalizeSlashes(path);
  size_t slash = norm.rfind('/');
  std::string_view base = slash == std::string::npos
                              ? norm
                              : std::string_view(norm).substr(slash + 1);
  if (base.ends_with("_FIXTURE.js")) {
    return true;
  }
  for (std::string_view skipped : kSkippedFileNames) {
    if (base == skipped) {
      return true;
    }
  }
  if (base.ends_with(".js") &&
      (base.find("numops-fuzz-part") != std::string_view::npos ||
       base.starts_with("modules") || base.starts_with("regexp-property-") ||
       base.find("foozzie") != std::string_view::npos)) {
    return true;
  }
  if (slash != std::string::npos) {
    std::string_view dir = std::string_view(norm).substr(0, slash);
    for (std::string_view skipped_dir : kSkippedDirSegments) {
      if (HasDirSubpath(dir, skipped_dir)) {
        return true;
      }
    }
  }
  return false;
}

bool HasDiscouragedFlags(std::string_view source) {
  size_t pos = 0;
  while (pos < source.size()) {
    size_t end = source.find('\n', pos);
    if (end == std::string_view::npos) {
      end = source.size();
    }
    std::string_view line = source.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    line = TrimLeftWs(line);
    if (line.starts_with("// Flags:")) {
      std::string_view rest = line.substr(9);
      size_t i = 0;
      while (i < rest.size()) {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) {
          i++;
        }
        size_t start = i;
        while (i < rest.size() && rest[i] != ' ' && rest[i] != '\t') {
          i++;
        }
        if (i > start &&
            IsDiscouragedFlagToken(rest.substr(start, i - start))) {
          return true;
        }
      }
    }
    pos = end + 1;
  }
  return false;
}

MaterialBank::LoadStats MaterialBank::Load(const Options& options) {
  Stopwatch timer;
  programs_.clear();
  LoadStats stats;
  if (options.paths.empty() || options.max_programs == 0) {
    stats.elapsed_ms = static_cast<double>(timer.ElapsedUs()) / 1000.0;
    return stats;
  }

  std::vector<std::string> files;
  absl::flat_hash_set<std::string> seen_paths;
  for (const std::string& path : options.paths) {
    std::vector<std::string> listed = ListFiles(path, ".js");
    if (listed.empty()) {
      REDLINE_WARN(kLog, "--material=%s: no .js files found", path.c_str());
    }
    for (const std::string& file : listed) {
      if (seen_paths.insert(file).second) {
        files.push_back(file);
      }
    }
  }

  ImportOptions import_opts;
  import_opts.root = options.import_root;
  Random rng(options.seed == 0 ? 1 : options.seed);
  size_t accepted = 0;

  for (const std::string& file : files) {
    stats.scanned++;
    if (IsMaterialSkippedPath(file)) {
      stats.skipped++;
      continue;
    }
    size_t bytes = 0;
    if (!FileSize(file, &bytes) || bytes >= kMaxFileBytes) {
      stats.skipped++;
      continue;
    }
    std::string source;
    std::string err;
    if (!ReadFileToString(file, &source, &err)) {
      stats.errors++;
      continue;
    }
    if (IsModuleHeaderSource(source) || HasDiscouragedFlags(source)) {
      stats.skipped++;
      continue;
    }
    ImportInfo import_info;
    std::unique_ptr<fast::Tree> tree =
        ImportSource(source, file, import_opts, &import_info, &err);
    if (tree == nullptr) {
      if (err.starts_with("test262")) {
        stats.skipped++;
      } else {
        stats.errors++;
      }
      continue;
    }
    if (tree->root()->body.empty()) {
      stats.skipped++;
      continue;
    }

    Lineage lineage;
    lineage.origin = "material";
    lineage.detail = file;
    ProgramPtr prog = Program::FromTree(std::move(tree), std::move(lineage));
    accepted++;
    if (programs_.size() < options.max_programs) {
      programs_.push_back(std::move(prog));
    } else {
      size_t slot = rng.Index(accepted);
      if (slot < options.max_programs) {
        programs_[slot] = std::move(prog);
      }
    }
  }

  stats.loaded = programs_.size();
  stats.elapsed_ms = static_cast<double>(timer.ElapsedUs()) / 1000.0;
  return stats;
}

ProgramPtr MaterialBank::Pick(Random& rng) const {
  if (programs_.empty()) {
    return nullptr;
  }
  return programs_[rng.Index(programs_.size())];
}

bool UnwrapTopLevelIIFEs(fast::Tree* tree) {
  if (tree == nullptr || tree->root() == nullptr) {
    return false;
  }
  std::unique_ptr<fast::ScopeAnalysis> scopes =
      fast::ScopeAnalysis::Analyze(tree);
  absl::flat_hash_set<std::string> all_names;
  for (std::string_view name : scopes->all_names()) {
    all_names.emplace(name);
  }
  absl::flat_hash_set<std::string> top_names;
  for (const fast::Binding* b : scopes->root()->bindings) {
    top_names.emplace(b->name);
  }
  for (const fast::Identifier* id : scopes->unresolved()) {
    top_names.emplace(id->name);
  }

  bool changed = false;
  int fresh_counter = 0;
  std::vector<fast::Statement*> new_body;
  new_body.reserve(tree->root()->body.size());

  for (fast::Statement* stmt : tree->root()->body) {
    const fast::FunctionData* fn = nullptr;
    fast::Node* fn_node = nullptr;
    if (!ExtractZeroParamIIFE(stmt, &fn, &fn_node)) {
      new_body.push_back(stmt);
      continue;
    }
    if (!tree->root()->strict && HasStrictDirective(fn->body)) {
      new_body.push_back(stmt);
      continue;
    }
    if (HasSelfNameReference(fn, fn_node, *scopes)) {
      new_body.push_back(stmt);
      continue;
    }
    // `using` is not valid at the top level of a script.
    if (std::any_of(fn->body->body.begin(), fn->body->body.end(),
                    IsUsingDeclaration)) {
      new_body.push_back(stmt);
      continue;
    }
    fast::Scope* fn_scope = scopes->ScopeOf(fn_node);
    fast::Scope* body_scope = scopes->ScopeOf(fn->body);
    if ((fn_scope != nullptr && fn_scope->dynamic_inside) ||
        (body_scope != nullptr && body_scope->dynamic_inside)) {
      new_body.push_back(stmt);
      continue;
    }
    fast::Requirements req = fast::AnalyzeRequirements(fn->body);
    if (req.needs_function || req.needs_async || req.needs_generator ||
        req.needs_break || req.needs_continue || req.needs_this ||
        req.needs_arguments || req.needs_super || req.needs_new_target ||
        req.has_external_label || (req.needs_sloppy && tree->root()->strict)) {
      new_body.push_back(stmt);
      continue;
    }

    RenameCollidingScopeBindings(tree, fn_scope, &top_names, &all_names,
                                 &fresh_counter);
    if (body_scope != fn_scope) {
      RenameCollidingScopeBindings(tree, body_scope, &top_names, &all_names,
                                   &fresh_counter);
    }

    for (fast::Statement* inner : fn->body->body) {
      if (!IsDirectiveStmt(inner)) {
        new_body.push_back(inner);
      }
    }
    changed = true;
  }

  if (changed) {
    tree->root()->body = std::move(new_body);
  }
  return changed;
}

std::shared_ptr<Program> FusePrograms(const std::vector<ProgramPtr>& donors,
                                      Lineage lineage) {
  if (donors.empty()) {
    return nullptr;
  }

  std::vector<std::unique_ptr<fast::Tree>> trees;
  trees.reserve(donors.size());
  for (const ProgramPtr& donor : donors) {
    if (donor == nullptr) {
      continue;
    }
    std::unique_ptr<fast::Tree> copy = donor->CopyTree();
    UnwrapTopLevelIIFEs(copy.get());
    trees.push_back(std::move(copy));
  }
  if (trees.empty()) {
    return nullptr;
  }

  std::vector<std::unique_ptr<fast::ScopeAnalysis>> donor_scopes;
  donor_scopes.reserve(trees.size());
  absl::flat_hash_set<std::string> all_names;
  absl::flat_hash_set<std::string> occupied_names;
  for (const std::unique_ptr<fast::Tree>& t : trees) {
    auto scopes = fast::ScopeAnalysis::Analyze(t.get());
    for (std::string_view name : scopes->all_names()) {
      all_names.emplace(name);
    }
    for (const fast::Identifier* id : scopes->unresolved()) {
      occupied_names.emplace(id->name);
    }
    donor_scopes.push_back(std::move(scopes));
  }

  int fresh_counter = 0;
  for (size_t i = 0; i < trees.size(); i++) {
    for (fast::Binding* b : donor_scopes[i]->root()->bindings) {
      if (b->declarations.empty() || b->kind == fast::BindingKind::kImplicit) {
        continue;
      }
      if (occupied_names.contains(b->name)) {
        std::string fresh = FreshName("_f", &fresh_counter, all_names);
        all_names.insert(fresh);
        occupied_names.insert(fresh);
        fast::RenameBinding(trees[i].get(), b, fresh);
      } else {
        occupied_names.emplace(b->name);
      }
    }
  }

  auto fused = std::make_unique<fast::Tree>();
  auto* fused_root = fused->New<fast::Program>();
  fused->set_root(fused_root);
  for (const std::unique_ptr<fast::Tree>& t : trees) {
    EmitIsolatedDonor(fused.get(), t->root(), &fused_root->body);
  }
  if (fused_root->body.empty()) {
    return nullptr;
  }

  if (lineage.origin.empty()) {
    lineage.origin = "Fuse";
  }
  return Program::FromTree(std::move(fused), std::move(lineage));
}

std::shared_ptr<Program> FusePrograms(const std::vector<ProgramPtr>& donors,
                                      const std::string& detail) {
  Lineage lineage;
  lineage.origin = "Fuse";
  lineage.detail = detail;
  return FusePrograms(donors, std::move(lineage));
}

}  // namespace redline
