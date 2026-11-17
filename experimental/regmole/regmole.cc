// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Clang AST plugin for static register clobber and control-flow analysis in
// V8's Assembler, MacroAssembler, BaselineAssembler, Maglev, Liftoff, and
// codegen callers across x64 and arm64.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "clang/AST/APValue.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace {

bool g_verify_annotations = true;
bool g_check_uses = true;
bool g_check_headers = false;

using RegMask = uint32_t;
constexpr int kMaxGprs = 32;

enum class ArchKind {
  kAuto,
  kX64,
  kArm64,
};

ArchKind g_arch_override = ArchKind::kAuto;

struct ArchInfo {
  ArchKind kind = ArchKind::kX64;
  int num_gprs = 16;
  const char* gpr_names[kMaxGprs] = {};
  RegMask pinned_mask = 0;
  RegMask c_callee_saved_mask = 0;
  RegMask caller_saved_mask = 0;
  RegMask implicit_macro_scratch_mask = 0;
  RegMask arm64_default_tmp_mask = 0;
  int ret_reg0 = 0;
  int ret_reg1 = 2;
  int baseline_scratch_pool_size = 0;
  int baseline_scratch_regs[8] = {};
  RegMask baseline_scratch_mask = 0;
};

ArchInfo MakeX64ArchInfo() {
  ArchInfo a;
  a.kind = ArchKind::kX64;
  a.num_gprs = 16;
  static constexpr const char* kNames[16] = {
      "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
      "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 16; ++i) a.gpr_names[i] = kNames[i];
  // rsp (4), rbp (5), r13=root (13), r14=cage_base (14).
  a.pinned_mask = (1u << 4) | (1u << 5) | (1u << 13) | (1u << 14);
  // rbx (3), r12 (12), r15 (15).
  a.c_callee_saved_mask = (1u << 3) | (1u << 12) | (1u << 15);
  a.caller_saved_mask = 0xFFFFu & ~a.pinned_mask;
  // r10 = kScratchRegister on x64.
  a.implicit_macro_scratch_mask = (1u << 10);
  a.arm64_default_tmp_mask = 0;
  a.ret_reg0 = 0;  // rax
  a.ret_reg1 = 2;  // rdx
  a.baseline_scratch_pool_size = 5;
  int pool[5] = {8, 9, 11, 12, 15};
  for (int i = 0; i < 5; ++i) {
    a.baseline_scratch_regs[i] = pool[i];
    a.baseline_scratch_mask |= (1u << pool[i]);
  }
  return a;
}

ArchInfo MakeArm64ArchInfo() {
  ArchInfo a;
  a.kind = ArchKind::kArm64;
  a.num_gprs = 32;
  static constexpr const char* kNames[32] = {
      "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
      "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
      "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
      "x24", "x25", "x26", "x27", "x28", "fp",  "lr",  "xzr"};
  for (int i = 0; i < 32; ++i) a.gpr_names[i] = kNames[i];
  // x18 (platform), x26 (root), x27 (cp), x28 (cage_base), x29 (fp), x31 (xzr).
  a.pinned_mask = (1u << 18) | (1u << 26) | (1u << 27) | (1u << 28) |
                  (1u << 29) | (1u << 31);
  // AAPCS64 callee-saved x19..x28 (excluding pinned x26..x28).
  a.c_callee_saved_mask = 0x1FF80000u & ~a.pinned_mask;
  a.caller_saved_mask = 0xFFFFFFFFu & ~a.pinned_mask;
  // ip0 = x16, ip1 = x17.
  a.implicit_macro_scratch_mask = (1u << 16) | (1u << 17);
  a.arm64_default_tmp_mask = (1u << 16) | (1u << 17);
  a.ret_reg0 = 0;  // x0
  a.ret_reg1 = 1;  // x1
  // BaselineAssembler::ScratchRegisterScope includes x14, x15, x19 + ip0, ip1.
  a.baseline_scratch_pool_size = 5;
  int pool[5] = {14, 15, 16, 17, 19};
  for (int i = 0; i < 5; ++i) {
    a.baseline_scratch_regs[i] = pool[i];
    a.baseline_scratch_mask |= (1u << pool[i]);
  }
  return a;
}

inline llvm::StringRef SafeDeclName(const clang::NamedDecl* d) {
  return (d && d->getIdentifier()) ? d->getName() : llvm::StringRef();
}

ArchInfo DetectArchFromAST(clang::ASTContext& ctx, ArchKind override_kind) {
  if (override_kind == ArchKind::kX64) return MakeX64ArchInfo();
  if (override_kind == ArchKind::kArm64) return MakeArm64ArchInfo();

  const clang::TranslationUnitDecl* tu = ctx.getTranslationUnitDecl();
  for (const clang::Decl* d : tu->decls()) {
    const auto* ns_v8 = llvm::dyn_cast<clang::NamespaceDecl>(d);
    if (!ns_v8 || SafeDeclName(ns_v8) != "v8") continue;
    for (const clang::Decl* d2 : ns_v8->decls()) {
      const auto* ns_int = llvm::dyn_cast<clang::NamespaceDecl>(d2);
      if (!ns_int || SafeDeclName(ns_int) != "internal") continue;
      for (const clang::Decl* d3 : ns_int->decls()) {
        if (const auto* rec = llvm::dyn_cast<clang::CXXRecordDecl>(d3)) {
          if (SafeDeclName(rec) == "CPURegister") {
            return MakeArm64ArchInfo();
          }
        }
      }
    }
  }
  return MakeX64ArchInfo();
}

enum class ParamRole {
  kNone,
  kIn,
  kRdOnly,
  kOut,
  kInOut,
  kScratch,
  kInClobber,
  kBind,
  kBranch,
};

bool IsInputLikeRole(ParamRole role) {
  return role == ParamRole::kIn || role == ParamRole::kRdOnly;
}

const char* ParamRoleToString(ParamRole role) {
  switch (role) {
    case ParamRole::kNone:
      return "none";
    case ParamRole::kIn:
      return "V8_REG_IN";
    case ParamRole::kRdOnly:
      return "V8_REG_RDONLY";
    case ParamRole::kOut:
      return "V8_REG_OUT";
    case ParamRole::kInOut:
      return "V8_REG_INOUT";
    case ParamRole::kScratch:
      return "V8_REG_SCRATCH";
    case ParamRole::kInClobber:
      return "V8_REG_IN_CLOBBER";
    case ParamRole::kBind:
      return "V8_ASM_BIND";
    case ParamRole::kBranch:
      return "V8_ASM_BRANCH";
  }
  return "unknown";
}

const clang::ValueDecl* CanonValueDecl(const clang::ValueDecl* d) {
  return d ? llvm::cast<clang::ValueDecl>(d->getCanonicalDecl()) : nullptr;
}

std::string FormatRegMask(RegMask mask, const ArchInfo& arch) {
  std::string out;
  for (int i = 0; i < arch.num_gprs; ++i) {
    if (mask & (1u << i)) {
      if (!out.empty()) out += ", ";
      out += arch.gpr_names[i];
    }
  }
  return out.empty() ? "none" : out;
}

bool IsTypeNamed(clang::QualType qt, llvm::StringRef target_name) {
  if (qt.isNull()) return false;
  qt = qt.getNonReferenceType().getUnqualifiedType();
  if (const auto* td = qt->getAs<clang::TypedefType>()) {
    if (SafeDeclName(td->getDecl()) == target_name) return true;
  }
  if (const auto* rec = qt->getAsCXXRecordDecl()) {
    if (SafeDeclName(rec) == target_name) return true;
  }
  return false;
}

bool IsNonGprRegType(clang::QualType qt) {
  return IsTypeNamed(qt, "VRegister") || IsTypeNamed(qt, "DoubleRegister") ||
         IsTypeNamed(qt, "FloatRegister") ||
         IsTypeNamed(qt, "Simd128Register") || IsTypeNamed(qt, "ZRegister") ||
         IsTypeNamed(qt, "PRegister") || IsTypeNamed(qt, "CRegister") ||
         IsTypeNamed(qt, "XMMRegister") || IsTypeNamed(qt, "YMMRegister");
}

bool IsCoreGprRegType(clang::QualType qt) {
  if (IsNonGprRegType(qt)) return false;
  return IsTypeNamed(qt, "Register") || IsTypeNamed(qt, "TaggedRegister") ||
         IsTypeNamed(qt, "CPURegister");
}

bool IsTrackableRegType(clang::QualType qt) {
  return IsCoreGprRegType(qt) || IsTypeNamed(qt, "LiftoffRegister");
}

bool IsOperandType(clang::QualType qt) {
  return IsTypeNamed(qt, "Operand") || IsTypeNamed(qt, "MemOperand");
}

bool IsLabelPtrType(clang::QualType qt) {
  if (qt.isNull() || !qt->isPointerType()) return false;
  clang::QualType pointee = qt->getPointeeType();
  return IsTypeNamed(pointee, "Label");
}

enum class ScratchScopeKind {
  kNone,
  kBaseline,
  kArm64UseScratch,
  kMaglevTemp,
};

ScratchScopeKind GetScratchScopeKind(clang::QualType qt) {
  if (IsTypeNamed(qt, "ScratchRegisterScope")) {
    return ScratchScopeKind::kBaseline;
  }
  if (IsTypeNamed(qt, "UseScratchRegisterScope")) {
    return ScratchScopeKind::kArm64UseScratch;
  }
  if (IsTypeNamed(qt, "TemporaryRegisterScope")) {
    return ScratchScopeKind::kMaglevTemp;
  }
  return ScratchScopeKind::kNone;
}

// Unwraps implicit casts, parens, MaterializeTemporaryExpr,
// CXXBindTemporaryExpr, __builtin_expect, and GPR size/type wrappers
// (.reg(), .gp(), .W(), .X(), .Reg()).
const clang::Expr* UnwrapExpr(const clang::Expr* expr) {
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    if (const auto* bind = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr)) {
      expr = bind->getSubExpr();
      continue;
    }
    if (const auto* mat =
            llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
      expr = mat->getSubExpr();
      continue;
    }
    if (const auto* clean = llvm::dyn_cast<clang::ExprWithCleanups>(expr)) {
      expr = clean->getSubExpr();
      continue;
    }
    if (const auto* ce = llvm::dyn_cast<clang::CallExpr>(expr)) {
      if (const clang::FunctionDecl* fd = ce->getDirectCallee()) {
        if (fd->getIdentifier() && fd->getName() == "__builtin_expect" &&
            ce->getNumArgs() >= 1) {
          expr = ce->getArg(0);
          continue;
        }
      }
    }
    if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
      if (construct->getConstructor()->isCopyOrMoveConstructor() &&
          construct->getNumArgs() >= 1) {
        expr = construct->getArg(0);
        continue;
      }
      if (construct->getNumArgs() == 1 &&
          IsTrackableRegType(construct->getType())) {
        const clang::Expr* arg0 = construct->getArg(0)->IgnoreParenImpCasts();
        if (arg0 && IsTrackableRegType(arg0->getType())) {
          expr = arg0;
          continue;
        }
      }
    }
    if (const auto* mem_call = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
      if (const auto* method = mem_call->getMethodDecl()) {
        llvm::StringRef mname = SafeDeclName(method);
        if (mem_call->getNumArgs() == 0 &&
            (mname == "reg" || mname == "gp" || mname == "W" || mname == "X" ||
             mname == "Reg") &&
            IsCoreGprRegType(mem_call->getType())) {
          const clang::Expr* obj = mem_call->getImplicitObjectArgument();
          if (obj && IsTrackableRegType(obj->getType())) {
            expr = obj;
            continue;
          }
        }
      }
    }
    break;
  }
  return expr;
}

enum class RecordKind {
  kOther,
  kAssembler,
  kMacroAssembler,
  kBaselineAssembler,
};

bool HasBaseOrSelfNamed(const clang::CXXRecordDecl* rec,
                        llvm::StringRef target) {
  if (!rec) return false;
  if (SafeDeclName(rec) == target) return true;
  if (!rec->hasDefinition()) return false;
  for (const clang::CXXBaseSpecifier& base : rec->bases()) {
    if (const auto* base_rec = base.getType()->getAsCXXRecordDecl()) {
      if (HasBaseOrSelfNamed(base_rec, target)) return true;
    }
  }
  return false;
}

RecordKind GetRecordKind(const clang::CXXRecordDecl* rec) {
  if (!rec) return RecordKind::kOther;
  if (HasBaseOrSelfNamed(rec, "BaselineAssembler")) {
    return RecordKind::kBaselineAssembler;
  }
  // Check MacroAssembler before Assembler because MacroAssembler inherits from
  // Assembler.
  if (HasBaseOrSelfNamed(rec, "MacroAssembler") ||
      HasBaseOrSelfNamed(rec, "MacroAssemblerBase") ||
      HasBaseOrSelfNamed(rec, "SharedMacroAssemblerBase") ||
      HasBaseOrSelfNamed(rec, "SharedTurboAssembler")) {
    return RecordKind::kMacroAssembler;
  }
  if (HasBaseOrSelfNamed(rec, "Assembler") ||
      HasBaseOrSelfNamed(rec, "AssemblerBase")) {
    return RecordKind::kAssembler;
  }
  return RecordKind::kOther;
}

// Only check unannotated methods directly on MacroAssembler / BaselineAssembler
// (not derived compilers like MaglevAssembler / LiftoffAssembler unless
// explicitly annotated).
bool ShouldVerifyUnannotatedRecord(const clang::CXXRecordDecl* rec) {
  if (!rec) return false;
  llvm::StringRef name = SafeDeclName(rec);
  return name == "MacroAssembler" || name == "MacroAssemblerBase" ||
         name == "SharedMacroAssemblerBase" || name == "SharedMacroAssembler" ||
         name == "SharedTurboAssembler" || name == "BaselineAssembler";
}

bool HasAssemblerOrRegSignature(const clang::FunctionDecl* fn) {
  if (!fn) return false;
  clang::QualType ret = fn->getReturnType();
  if (IsOperandType(ret) || IsCoreGprRegType(ret)) return true;
  for (unsigned i = 0; i < fn->getNumParams(); ++i) {
    clang::QualType pt = fn->getParamDecl(i)->getType();
    if (IsTrackableRegType(pt) || IsOperandType(pt)) return true;
    clang::QualType deref =
        pt->isPointerType() ? pt->getPointeeType() : pt.getNonReferenceType();
    if (const auto* rec = deref->getAsCXXRecordDecl()) {
      if (GetRecordKind(rec) != RecordKind::kOther) return true;
    }
  }
  return false;
}

// Extracts an integer field recursively from an APValue representing
// RegisterBase.
std::optional<int64_t> ExtractIntFromAPValue(const clang::APValue& val) {
  if (val.isInt()) {
    return val.getInt().getSExtValue();
  }
  if (val.isStruct()) {
    for (unsigned i = 0; i < val.getStructNumBases(); ++i) {
      if (auto res = ExtractIntFromAPValue(val.getStructBase(i))) return res;
    }
    for (unsigned i = 0; i < val.getStructNumFields(); ++i) {
      if (auto res = ExtractIntFromAPValue(val.getStructField(i))) return res;
    }
  }
  return std::nullopt;
}

std::optional<int> ExtractGprCodeFromAPValue(const clang::APValue& val,
                                             const ArchInfo& arch) {
  if (arch.kind == ArchKind::kArm64 && val.isStruct()) {
    // On ARM64, CPURegister has 1 base (RegisterBase) and 2 fields:
    // field 0 = reg_size_, field 1 = reg_type_ (0=kRegister, 1=kVRegister,
    // 2=kZRegister, 3=kNoRegister).
    if (val.getStructNumBases() == 1 && val.getStructNumFields() == 2 &&
        val.getStructField(1).isInt()) {
      int64_t reg_type = val.getStructField(1).getInt().getSExtValue();
      if (reg_type == 3) return -1;            // kNoRegister (NoReg / no_reg)
      if (reg_type != 0) return std::nullopt;  // Reject VRegister / ZRegister
      auto code = ExtractIntFromAPValue(val.getStructBase(0));
      if (!code) return std::nullopt;
      if (*code == 63) return std::nullopt;  // kSPRegInternalCode (sp / wsp)
      if (*code >= -1 && *code < arch.num_gprs) {
        return static_cast<int>(*code);
      }
      return std::nullopt;
    }
    if (val.getStructNumBases() == 1 && val.getStructNumFields() == 0) {
      return ExtractGprCodeFromAPValue(val.getStructBase(0), arch);
    }
  }
  if (auto code = ExtractIntFromAPValue(val)) {
    if (*code >= -1 && *code < arch.num_gprs) {
      return static_cast<int>(*code);
    }
  }
  return std::nullopt;
}

// Evaluates a constexpr Register expression (e.g. `rax`, `x16`, `ip0`,
// `kScratchRegister`, `no_reg`) to its register code (-1 for no_reg,
// 0..num_gprs-1 for physical GPRs).
std::optional<int> EvaluateRegisterCode(const clang::Expr* expr,
                                        clang::ASTContext& ast_ctx,
                                        const ArchInfo& arch) {
  if (!expr || expr->isValueDependent()) return std::nullopt;
  const clang::Expr* unwrapped = UnwrapExpr(expr);
  if (!unwrapped || !IsCoreGprRegType(unwrapped->getType())) {
    return std::nullopt;
  }
  // Evaluate the original expression if it is a core GPR (e.g. `x0.W()`), or
  // the unwrapped expression if `expr` was a wrapper.
  const clang::Expr* eval_target =
      IsCoreGprRegType(expr->getType()) ? expr : unwrapped;
  clang::Expr::EvalResult eval_result;
  if (eval_target->EvaluateAsRValue(eval_result, ast_ctx)) {
    return ExtractGprCodeFromAPValue(eval_result.Val, arch);
  }
  return std::nullopt;
}

struct FuncSummary {
  std::vector<ParamRole> param_roles;
  RegMask implicit_in = 0;
  RegMask implicit_out = 0;
  RegMask clobbers = 0;
  bool clobbers_caller_saved = false;
  bool no_fallthrough = false;
  bool zeroing_if_same = false;
  bool is_push = false;
  bool is_pop = false;
  bool has_explicit_annotations = false;
  int baseline_scratch_used = 0;
  int must_baseline_scratch_used = 0;
  int arm64_scratch_used = 0;
  int must_arm64_scratch_used = 0;

  // Scratch-backed and input-backed Operand/MemOperand return summaries:
  RegMask returned_operand_scratch_phys_mask = 0;
  uint32_t returned_operand_scratch_param_mask = 0;
  uint32_t returned_operand_input_param_mask = 0;
  RegMask returned_operand_input_phys_mask = 0;
  uint32_t preserved_across_call_param_mask = 0;

  // Early-clobber pairs (out_param_idx, in_param_idx): writing out_param_idx
  // happens before reading in_param_idx on at least one path without an
  // aliasing-safe guard.
  std::set<std::pair<unsigned, unsigned>> early_clobber_param_pairs;
  // Physical registers overwritten before reading param `in_param_idx`.
  std::vector<RegMask> early_clobber_phys_before_param;
};

// Collects all declarations to inspect for attributes (redecls + template
// pattern redecls).
std::vector<const clang::FunctionDecl*> GetAllDeclForms(
    const clang::FunctionDecl* fn) {
  std::vector<const clang::FunctionDecl*> forms;
  llvm::SmallPtrSet<const clang::FunctionDecl*, 8> seen;
  auto add_redecls = [&](const clang::FunctionDecl* d) {
    if (!d) return;
    for (const clang::FunctionDecl* r : d->redecls()) {
      if (seen.insert(r).second) {
        forms.push_back(r);
      }
    }
  };
  add_redecls(fn);
  if (const clang::FunctionDecl* pattern =
          fn->getTemplateInstantiationPattern()) {
    add_redecls(pattern);
  }
  if (const clang::FunctionTemplateDecl* ftd = fn->getPrimaryTemplate()) {
    add_redecls(ftd->getTemplatedDecl());
  }
  return forms;
}

FuncSummary ExtractExplicitSummary(const clang::FunctionDecl* fn,
                                   clang::ASTContext& ast_ctx,
                                   const ArchInfo& arch) {
  FuncSummary sum;
  unsigned num_params = fn->getNumParams();
  sum.param_roles.assign(num_params, ParamRole::kNone);
  sum.early_clobber_phys_before_param.assign(num_params, 0);

  for (unsigned i = 0; i < num_params; ++i) {
    clang::QualType pt = fn->getParamDecl(i)->getType();
    if (IsTrackableRegType(pt) || IsOperandType(pt)) {
      sum.param_roles[i] = ParamRole::kIn;
    }
  }

  std::vector<const clang::FunctionDecl*> forms = GetAllDeclForms(fn);
  for (const clang::FunctionDecl* form : forms) {
    for (const auto* attr : form->specific_attrs<clang::AnnotateAttr>()) {
      llvm::StringRef tag = attr->getAnnotation();
      if (tag == "v8_reg_implicit_in" || tag == "v8_reg_implicit_out" ||
          tag == "v8_reg_implicit_inout" || tag == "v8_reg_clobbers") {
        sum.has_explicit_annotations = true;
        RegMask mask = 0;
        for (const clang::Expr* arg : attr->args()) {
          if (auto code = EvaluateRegisterCode(arg, ast_ctx, arch)) {
            if (*code >= 0 && *code < arch.num_gprs) {
              mask |= (1u << *code);
            }
          }
        }
        if (tag == "v8_reg_implicit_in") {
          sum.implicit_in |= mask;
        } else if (tag == "v8_reg_implicit_out") {
          sum.implicit_out |= mask;
        } else if (tag == "v8_reg_implicit_inout") {
          sum.implicit_in |= mask;
          sum.implicit_out |= mask;
        } else if (tag == "v8_reg_clobbers") {
          sum.clobbers |= mask;
        }
      } else if (tag == "v8_reg_clobbers_caller_saved") {
        sum.has_explicit_annotations = true;
        sum.clobbers_caller_saved = true;
        sum.clobbers |= arch.caller_saved_mask;
      } else if (tag == "v8_asm_no_fallthrough") {
        sum.has_explicit_annotations = true;
        sum.no_fallthrough = true;
      } else if (tag == "v8_asm_zeroing_if_same") {
        sum.has_explicit_annotations = true;
        sum.zeroing_if_same = true;
      } else if (tag == "v8_asm_push") {
        sum.has_explicit_annotations = true;
        sum.is_push = true;
      } else if (tag == "v8_asm_pop") {
        sum.has_explicit_annotations = true;
        sum.is_pop = true;
      }
    }

    unsigned form_params = std::min(num_params, form->getNumParams());
    for (unsigned i = 0; i < form_params; ++i) {
      const clang::ParmVarDecl* p = form->getParamDecl(i);
      for (const auto* attr : p->specific_attrs<clang::AnnotateAttr>()) {
        llvm::StringRef tag = attr->getAnnotation();
        if (tag == "v8_reg_in") {
          sum.param_roles[i] = ParamRole::kIn;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_reg_rdonly") {
          sum.param_roles[i] = ParamRole::kRdOnly;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_reg_out") {
          sum.param_roles[i] = ParamRole::kOut;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_reg_inout") {
          sum.param_roles[i] = ParamRole::kInOut;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_reg_scratch") {
          sum.param_roles[i] = ParamRole::kScratch;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_reg_in_clobber") {
          sum.param_roles[i] = ParamRole::kInClobber;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_asm_bind") {
          sum.param_roles[i] = ParamRole::kBind;
          sum.has_explicit_annotations = true;
        } else if (tag == "v8_asm_branch") {
          sum.param_roles[i] = ParamRole::kBranch;
          sum.has_explicit_annotations = true;
        }
      }
    }
  }

  // Any function returning an Operand/MemOperand that takes a V8_REG_OUT or
  // V8_REG_SCRATCH register parameter (e.g. ExternalReferenceAsOperand,
  // MoveImmediateForShiftedOp) ties the returned Operand's validity to that
  // scratch/destination register's version.
  if (IsOperandType(fn->getReturnType())) {
    sum.returned_operand_scratch_phys_mask |=
        (sum.clobbers & arch.implicit_macro_scratch_mask);
    for (unsigned i = 0; i < num_params && i < 32; ++i) {
      if (sum.param_roles[i] == ParamRole::kOut ||
          sum.param_roles[i] == ParamRole::kScratch) {
        sum.returned_operand_scratch_param_mask |= (1u << i);
      }
    }
  }

  return sum;
}

// ----------------------------------------------------------------------------
// Dataflow Lattice & Environment
// ----------------------------------------------------------------------------

enum class RegStatus {
  kInitial,    // Holds untouched entry value.
  kAlive,      // Holds valid value written during this function.
  kClobbered,  // Value was destroyed by a clobber or scratch operation.
};

enum class ReadonlyKind {
  kNone,
  kExplicitAttr,  // V8_REG_RDONLY parameter
  kMaglevInput,   // Maglev UseRegister / UseFixed input without UseAndClobber*
};

struct ClobberNote {
  clang::SourceLocation loc;
  std::string message;
};

struct SymRegState {
  RegStatus status = RegStatus::kInitial;
  const clang::ValueDecl* root_decl = nullptr;
  int phys_reg = -1;          // 0..num_gprs-1 if known physical reg, else -1.
  int param_index = -1;       // >= 0 if bound to entry parameter i.
  int scratch_slot = -1;      // >= 0 if acquired from a scratch scope.
  int scratch_scope_id = -1;  // Active scope ID when acquired.
  bool holds_entry_value = false;
  uint64_t def_version = 0;
  ReadonlyKind readonly_kind = ReadonlyKind::kNone;
  std::string readonly_reason;
  ClobberNote clobber;
};

struct PhysRegState {
  RegStatus status = RegStatus::kInitial;
  bool holds_entry_value = true;
  uint64_t def_version = 0;
  ClobberNote clobber;
};

struct OperandDep {
  llvm::SmallVector<const clang::ValueDecl*, 2> sym_decls;
  RegMask phys_mask = 0;
  // Scratch registers (physical or symbolic) populated when the Operand was
  // created, along with their exact def_version at creation time.
  llvm::SmallVector<std::pair<int, uint64_t>, 2> scratch_phys_versions;
  llvm::SmallVector<std::pair<const clang::ValueDecl*, uint64_t>, 2>
      scratch_sym_versions;
  clang::SourceLocation creation_loc;
};

struct SavedStackEntry {
  enum Kind { kSingleReg, kCallerSaved, kAllRegs } kind = kSingleReg;
  const clang::ValueDecl* root_decl = nullptr;
  SymRegState saved_sym;
  int phys_reg = -1;
  PhysRegState saved_phys;
  PhysRegState saved_phys_all[kMaxGprs];
  std::map<const clang::ValueDecl*, SymRegState> saved_syms_all;
  bool saved_clobbered_caller_saved = false;
};

struct ScratchScopeFrame {
  int base_depth = 0;
  ScratchScopeKind kind = ScratchScopeKind::kNone;
  RegMask saved_arm64_avail = 0;
  RegMask saved_arm64_included = 0;
  int saved_arm64_caller_held = 0;
};

struct Env {
  bool reachable = true;
  PhysRegState phys[kMaxGprs];
  std::map<const clang::ValueDecl*, SymRegState> syms;
  std::map<const clang::ValueDecl*, OperandDep> operands;
  std::map<const clang::ValueDecl*, std::vector<const clang::ValueDecl*>>
      reg_arrays;
  std::vector<SavedStackEntry> asm_stack;

  // Parameters/regs known to be `no_reg` (or immediate Operand) on this branch.
  llvm::SmallPtrSet<const clang::ValueDecl*, 4> known_no_reg;
  // Pairs of symbolic declarations known to be equal or non-aliasing on branch.
  std::set<std::pair<const clang::ValueDecl*, const clang::ValueDecl*>>
      known_equal;
  std::set<std::pair<const clang::ValueDecl*, const clang::ValueDecl*>>
      known_not_equal;
  // On ARM64, pairs guarded only by size-sensitive `!=` / `!(==)` where
  // `wN != xN` evaluates to true despite aliasing physical register N.
  std::map<std::pair<const clang::ValueDecl*, const clang::ValueDecl*>,
           clang::SourceLocation>
      size_sensitive_not_equal;

  // Scratch scope tracking:
  int active_scratch_depth = 0;
  RegMask arm64_avail_scratch_mask = 0;
  RegMask arm64_included_scratch_mask = 0;
  int arm64_caller_scratches_held = 0;
  int path_max_baseline_scratch = 0;
  int path_max_arm64_scratch = 0;
  std::vector<ScratchScopeFrame> scratch_scope_stack;

  // Summary inference accumulators along this path:
  uint32_t params_read_mask = 0;
  uint32_t params_written_mask = 0;
  RegMask phys_written_mask = 0;
  bool clobbered_caller_saved = false;
};

bool EnvEquivalentForFixpoint(const Env& a, const Env& b, int num_gprs) {
  if (a.reachable != b.reachable) return false;
  if (!a.reachable) return true;
  for (int r = 0; r < num_gprs; ++r) {
    if (a.phys[r].status != b.phys[r].status ||
        a.phys[r].holds_entry_value != b.phys[r].holds_entry_value) {
      return false;
    }
  }
  if (a.syms.size() != b.syms.size()) return false;
  for (const auto& [decl, sa] : a.syms) {
    auto it = b.syms.find(decl);
    if (it == b.syms.end()) return false;
    const SymRegState& sb = it->second;
    if (sa.status != sb.status || sa.phys_reg != sb.phys_reg ||
        sa.holds_entry_value != sb.holds_entry_value ||
        sa.scratch_slot != sb.scratch_slot) {
      return false;
    }
  }
  if (a.known_no_reg.size() != b.known_no_reg.size()) return false;
  for (const auto* d : a.known_no_reg) {
    if (!b.known_no_reg.contains(d)) return false;
  }
  if (a.operands.size() != b.operands.size()) return false;
  auto op_scratch_valid = [num_gprs](const OperandDep& dep, const Env& e) {
    for (const auto& [r, ver] : dep.scratch_phys_versions) {
      if (r >= 0 && r < num_gprs && e.phys[r].def_version != ver) return false;
    }
    for (const auto& [sd, ver] : dep.scratch_sym_versions) {
      auto it = e.syms.find(sd);
      if (it == e.syms.end() || it->second.def_version != ver) return false;
    }
    return true;
  };
  for (const auto& [op_d, op_a] : a.operands) {
    auto it_b = b.operands.find(op_d);
    if (it_b == b.operands.end()) return false;
    if (op_scratch_valid(op_a, a) != op_scratch_valid(it_b->second, b)) {
      return false;
    }
  }
  return true;
}

RegStatus MergeStatus(RegStatus a, RegStatus b, ClobberNote* out_note,
                      const ClobberNote& note_a, const ClobberNote& note_b) {
  if (a == RegStatus::kClobbered || b == RegStatus::kClobbered) {
    if (out_note) {
      *out_note = (a == RegStatus::kClobbered) ? note_a : note_b;
    }
    return RegStatus::kClobbered;
  }
  if (a == RegStatus::kAlive || b == RegStatus::kAlive) {
    return RegStatus::kAlive;
  }
  return RegStatus::kInitial;
}

Env MergeEnvs(const Env& a, const Env& b, int num_gprs) {
  if (!a.reachable) return b;
  if (!b.reachable) return a;

  Env out = a;
  out.path_max_baseline_scratch =
      std::min(a.path_max_baseline_scratch, b.path_max_baseline_scratch);
  out.path_max_arm64_scratch =
      std::min(a.path_max_arm64_scratch, b.path_max_arm64_scratch);
  for (int r = 0; r < num_gprs; ++r) {
    ClobberNote note;
    out.phys[r].status = MergeStatus(a.phys[r].status, b.phys[r].status, &note,
                                     a.phys[r].clobber, b.phys[r].clobber);
    out.phys[r].clobber = note;
    out.phys[r].holds_entry_value =
        a.phys[r].holds_entry_value && b.phys[r].holds_entry_value;
    out.phys[r].def_version =
        std::max(a.phys[r].def_version, b.phys[r].def_version);
  }

  for (const auto& [decl, b_sym] : b.syms) {
    auto it = out.syms.find(decl);
    if (it == out.syms.end()) {
      out.syms[decl] = b_sym;
    } else {
      SymRegState& a_sym = it->second;
      ClobberNote note;
      bool a_noreg = a.known_no_reg.contains(decl);
      bool b_noreg = b.known_no_reg.contains(decl);
      if (a_noreg && !b_noreg) {
        a_sym = b_sym;
      } else if (!b_noreg) {
        RegStatus b_eff_status = b_sym.status;
        if (b_eff_status == RegStatus::kInitial) {
          for (const auto& eq_pair : b.known_equal) {
            if (eq_pair.first == decl || eq_pair.second == decl) {
              b_eff_status = RegStatus::kAlive;
            }
          }
        }
        RegStatus a_eff_status = a_sym.status;
        if (a_eff_status == RegStatus::kInitial) {
          for (const auto& eq_pair : a.known_equal) {
            if (eq_pair.first == decl || eq_pair.second == decl) {
              a_eff_status = RegStatus::kAlive;
            }
          }
        }
        a_sym.status = MergeStatus(a_eff_status, b_eff_status, &note,
                                   a_sym.clobber, b_sym.clobber);
        a_sym.clobber = note;
        a_sym.holds_entry_value =
            a_sym.holds_entry_value && b_sym.holds_entry_value;
        a_sym.def_version = std::max(a_sym.def_version, b_sym.def_version);
        if (a_sym.phys_reg != b_sym.phys_reg) {
          a_sym.phys_reg = -1;
        }
        if (a_sym.readonly_kind == ReadonlyKind::kNone &&
            b_sym.readonly_kind != ReadonlyKind::kNone) {
          a_sym.readonly_kind = b_sym.readonly_kind;
          a_sym.readonly_reason = b_sym.readonly_reason;
        }
      }
    }
  }

  for (const auto& [op_decl, b_dep] : b.operands) {
    auto it = out.operands.find(op_decl);
    if (it == out.operands.end()) {
      out.operands[op_decl] = b_dep;
    } else {
      OperandDep& a_dep = it->second;
      a_dep.phys_mask |= b_dep.phys_mask;
      for (const auto* sd : b_dep.sym_decls) {
        if (std::find(a_dep.sym_decls.begin(), a_dep.sym_decls.end(), sd) ==
            a_dep.sym_decls.end()) {
          a_dep.sym_decls.push_back(sd);
        }
      }
      for (const auto& pv : b_dep.scratch_phys_versions) {
        bool found = false;
        for (auto& apv : a_dep.scratch_phys_versions) {
          if (apv.first == pv.first) {
            apv.second = std::min(apv.second, pv.second);
            found = true;
            break;
          }
        }
        if (!found) a_dep.scratch_phys_versions.push_back(pv);
      }
      for (const auto& sv : b_dep.scratch_sym_versions) {
        bool found = false;
        for (auto& asv : a_dep.scratch_sym_versions) {
          if (asv.first == sv.first) {
            asv.second = std::min(asv.second, sv.second);
            found = true;
            break;
          }
        }
        if (!found) a_dep.scratch_sym_versions.push_back(sv);
      }
    }
  }

  out.known_no_reg.clear();
  for (const auto* d : a.known_no_reg) {
    if (b.known_no_reg.contains(d)) out.known_no_reg.insert(d);
  }
  out.known_equal.clear();
  out.known_not_equal.clear();
  out.size_sensitive_not_equal.clear();
  out.params_read_mask = a.params_read_mask | b.params_read_mask;
  out.params_written_mask = a.params_written_mask | b.params_written_mask;
  out.phys_written_mask = a.phys_written_mask | b.phys_written_mask;
  out.clobbered_caller_saved =
      a.clobbered_caller_saved || b.clobbered_caller_saved;
  return out;
}

// ----------------------------------------------------------------------------
// Function & Statement Analyzer
// ----------------------------------------------------------------------------

class RegmoleAnalyzer {
 public:
  RegmoleAnalyzer(clang::ASTContext& ast_ctx, clang::DiagnosticsEngine& diag,
                  clang::SourceManager& sm, const ArchInfo& arch)
      : ast_ctx_(ast_ctx), diag_(diag), sm_(sm), arch_(arch) {
    diag_warn_ = diag_.getCustomDiagID(clang::DiagnosticsEngine::Warning, "%0");
    diag_note_ = diag_.getCustomDiagID(clang::DiagnosticsEngine::Note, "%0");
  }

  void EmitImmediateWarning(clang::SourceLocation loc, const std::string& msg,
                            const ClobberNote* note = nullptr) {
    if (!loc.isValid()) return;
    diag_.Report(clang::FullSourceLoc(loc, sm_), diag_warn_) << msg;
    if (note && note->loc.isValid()) {
      diag_.Report(clang::FullSourceLoc(note->loc, sm_), diag_note_)
          << note->message;
    }
  }

  const FuncSummary& GetOrInferSummary(const clang::FunctionDecl* fn) {
    const clang::FunctionDecl* canon = fn->getCanonicalDecl();
    auto it = summary_cache_.find(canon);
    if (it != summary_cache_.end()) {
      return it->second;
    }

    FuncSummary explicit_sum = ExtractExplicitSummary(fn, ast_ctx_, arch_);
    summary_cache_[canon] = explicit_sum;

    const clang::CXXMethodDecl* method =
        llvm::dyn_cast<clang::CXXMethodDecl>(fn);
    RecordKind rec_kind =
        method ? GetRecordKind(method->getParent()) : RecordKind::kOther;

    const clang::FunctionDecl* body_decl = nullptr;
    if (fn->hasBody(body_decl) && active_inferences_.size() < 8) {
      if (rec_kind == RecordKind::kAssembler &&
          !explicit_sum.has_explicit_annotations) {
        if (active_inferences_.insert(canon).second) {
          FuncSummary inferred =
              InferAssemblerForwarder(body_decl, explicit_sum);
          active_inferences_.erase(canon);
          summary_cache_[canon] = inferred;
        }
      } else if (rec_kind != RecordKind::kAssembler) {
        bool should_infer =
            (rec_kind != RecordKind::kOther) ||
            explicit_sum.has_explicit_annotations ||
            HasAssemblerOrRegSignature(fn) ||
            (sm_.getFileID(body_decl->getLocation()) == sm_.getMainFileID());
        if (should_infer && active_inferences_.insert(canon).second) {
          FuncSummary inferred = AnalyzeFunctionInternal(
              body_decl, explicit_sum, /*emit_diagnostics=*/false);
          active_inferences_.erase(canon);
          MergeInferredIntoSummary(summary_cache_[canon], explicit_sum,
                                   inferred);
        }
      }
    }

    return summary_cache_[canon];
  }

  void AnalyzeTopLevelFunction(const clang::FunctionDecl* fn) {
    if (!fn->doesThisDeclarationHaveABody()) return;
    if (fn->isDependentContext()) return;

    const clang::CXXMethodDecl* method =
        llvm::dyn_cast<clang::CXXMethodDecl>(fn);
    RecordKind rec_kind =
        method ? GetRecordKind(method->getParent()) : RecordKind::kOther;

    // Do not analyze low-level Assembler byte-emission bodies.
    if (rec_kind == RecordKind::kAssembler) return;

    FuncSummary explicit_sum = ExtractExplicitSummary(fn, ast_ctx_, arch_);
    const clang::FunctionDecl* canon = fn->getCanonicalDecl();
    active_inferences_.insert(canon);
    FuncSummary inferred = AnalyzeFunctionInternal(
        fn, explicit_sum, /*emit_diagnostics=*/g_check_uses);
    active_inferences_.erase(canon);

    FuncSummary merged = explicit_sum;
    MergeInferredIntoSummary(merged, explicit_sum, inferred);
    summary_cache_[canon] = merged;
  }

 private:
  struct PendingDiag {
    clang::SourceLocation loc;
    std::string msg;
    ClobberNote note;
    bool has_note = false;
  };

  struct CfTarget {
    bool is_loop = false;
    Env break_env;
    Env continue_env;
  };

  struct MaglevConstraints {
    bool analyzed = false;
    bool has_define_same_as_first = false;
    std::string first_input_key;
    std::set<std::string> readonly_inputs;
    std::set<std::string> clobber_inputs;
  };

  struct FunctionAnalysisContext {
    const clang::FunctionDecl* fn = nullptr;
    const clang::CXXRecordDecl* maglev_node_cls = nullptr;
    FuncSummary explicit_sum;
    bool emit_diagnostics = false;
    uint64_t next_version = 1;
    int max_baseline_scratch_used = 0;
    int max_arm64_scratch_used = 0;
    bool used_baseline_scratch_scope = false;
    bool used_arm64_scratch_scope = false;

    // Inferred summary tracking across the function:
    std::vector<bool> param_read_entry;
    std::vector<bool> param_written_any;
    std::vector<bool> param_clobbered_any;
    std::vector<bool> param_written_before_read_any;
    bool any_push_param0 = false;
    bool any_pop_param0 = false;
    RegMask phys_read_entry = 0;
    RegMask phys_written_any = 0;
    RegMask phys_clobbered_any = 0;
    std::set<std::pair<unsigned, unsigned>> early_clobber_param_pairs;
    std::vector<RegMask> early_clobber_phys_before_param;

    // Returned Operand/MemOperand provenance:
    RegMask returned_operand_scratch_phys_mask = 0;
    uint32_t returned_operand_scratch_param_mask = 0;
    uint32_t returned_operand_input_param_mask = 0;
    RegMask returned_operand_input_phys_mask = 0;
    std::map<const clang::CallExpr*, OperandDep> call_operand_deps;

    // Label states for V8_ASM_BRANCH / V8_ASM_BIND:
    std::map<const clang::ValueDecl*, Env> forward_label_states;
    std::map<const clang::ValueDecl*, Env> backward_label_states;
    const std::map<const clang::ValueDecl*, Env>* carried_backedges = nullptr;
    std::set<const clang::ValueDecl*> bound_labels;

    // C++ switch/loop break & continue targets:
    std::vector<CfTarget> cf_stack;

    // Exit environments (at `return` statements and fallthrough end of body):
    std::vector<Env> exit_envs;

    // Buffered diagnostics for the current fixpoint pass:
    std::vector<PendingDiag> pending_diags;
  };

  void MergeInferredIntoSummary(FuncSummary& dst,
                                const FuncSummary& explicit_sum,
                                const FuncSummary& inferred) {
    if (!explicit_sum.has_explicit_annotations) {
      dst = inferred;
      return;
    }
    dst.early_clobber_param_pairs = inferred.early_clobber_param_pairs;
    dst.early_clobber_phys_before_param =
        inferred.early_clobber_phys_before_param;
    dst.baseline_scratch_used = inferred.baseline_scratch_used;
    dst.must_baseline_scratch_used = inferred.must_baseline_scratch_used;
    dst.arm64_scratch_used = inferred.arm64_scratch_used;
    dst.must_arm64_scratch_used = inferred.must_arm64_scratch_used;
    dst.preserved_across_call_param_mask |=
        inferred.preserved_across_call_param_mask;
    dst.clobbers |= (inferred.clobbers & arch_.implicit_macro_scratch_mask);
    dst.returned_operand_scratch_phys_mask |=
        inferred.returned_operand_scratch_phys_mask;
    dst.returned_operand_scratch_param_mask |=
        inferred.returned_operand_scratch_param_mask;
    dst.returned_operand_input_param_mask |=
        inferred.returned_operand_input_param_mask;
    dst.returned_operand_input_phys_mask |=
        inferred.returned_operand_input_phys_mask;
  }

  void QueueWarning(FunctionAnalysisContext& fctx, clang::SourceLocation loc,
                    const std::string& msg, const ClobberNote* note = nullptr) {
    if (!fctx.emit_diagnostics || !loc.isValid()) return;
    PendingDiag pd;
    pd.loc = loc;
    pd.msg = msg;
    if (note && note->loc.isValid()) {
      pd.note = *note;
      pd.has_note = true;
    }
    fctx.pending_diags.push_back(std::move(pd));
  }

  void FlushDiagnostics(const std::vector<PendingDiag>& diags) {
    std::set<std::pair<unsigned, std::string>> seen;
    for (const PendingDiag& pd : diags) {
      clang::SourceLocation exp_loc = sm_.getExpansionLoc(pd.loc);
      auto key = std::make_pair(exp_loc.getRawEncoding(), pd.msg);
      if (!seen.insert(key).second) continue;
      EmitImmediateWarning(pd.loc, pd.msg, pd.has_note ? &pd.note : nullptr);
    }
  }

  // Maglev SetValueLocationConstraints analysis:
  std::pair<std::string, std::string> ExtractMaglevInputKey(
      const clang::Expr* expr) {
    expr = UnwrapExpr(expr);
    if (!expr) return {"", ""};
    if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
      if (construct->getNumArgs() == 1) {
        expr = UnwrapExpr(construct->getArg(0));
      }
    }
    if (const auto* mce = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
      if (const auto* md = mce->getMethodDecl()) {
        std::string mname = md->getNameAsString();
        if (mce->getNumArgs() == 0) {
          return {"m:" + mname, mname};
        }
        if (mname == "input" && mce->getNumArgs() == 1) {
          std::string idx_str;
          llvm::raw_string_ostream os(idx_str);
          mce->getArg(0)->IgnoreParenImpCasts()->printPretty(
              os, nullptr, ast_ctx_.getPrintingPolicy());
          os.flush();
          return {"input:" + idx_str, "input(" + idx_str + ")"};
        }
      }
    }
    std::string s;
    llvm::raw_string_ostream os(s);
    expr->printPretty(os, nullptr, ast_ctx_.getPrintingPolicy());
    os.flush();
    return {"expr:" + s, s};
  }

  const MaglevConstraints& GetMaglevConstraints(
      const clang::CXXRecordDecl* cls) {
    const clang::CXXRecordDecl* canon = cls->getCanonicalDecl();
    auto it = maglev_cache_.find(canon);
    if (it != maglev_cache_.end()) return it->second;

    MaglevConstraints mc;
    mc.analyzed = true;
    const clang::CXXRecordDecl* def = cls->getDefinition();
    if (def) {
      for (const clang::CXXMethodDecl* m : def->methods()) {
        if (SafeDeclName(m) != "SetValueLocationConstraints") continue;
        const clang::FunctionDecl* body_decl = nullptr;
        if (!m->hasBody(body_decl)) continue;

        class ConstraintVisitor
            : public clang::RecursiveASTVisitor<ConstraintVisitor> {
         public:
          std::vector<const clang::CallExpr*> calls;
          bool VisitCallExpr(const clang::CallExpr* ce) {
            calls.push_back(ce);
            return true;
          }
        } visitor;
        visitor.TraverseStmt(const_cast<clang::Stmt*>(body_decl->getBody()));

        for (const clang::CallExpr* ce : visitor.calls) {
          const clang::FunctionDecl* callee = ce->getDirectCallee();
          if (!callee || !callee->getIdentifier()) continue;
          llvm::StringRef cname = callee->getName();
          if (cname == "DefineSameAsFirst") {
            mc.has_define_same_as_first = true;
          } else if ((cname == "UseRegister" || cname == "UseFixed" ||
                      cname == "UseAny") &&
                     ce->getNumArgs() >= 1) {
            auto [key, label] = ExtractMaglevInputKey(ce->getArg(0));
            if (!key.empty()) {
              if (mc.first_input_key.empty()) mc.first_input_key = key;
              mc.readonly_inputs.insert(key);
            }
          } else if ((cname == "UseAndClobberRegister" ||
                      cname == "UseAndClobberFixed") &&
                     ce->getNumArgs() >= 1) {
            auto [key, label] = ExtractMaglevInputKey(ce->getArg(0));
            if (!key.empty()) {
              if (mc.first_input_key.empty()) mc.first_input_key = key;
              mc.clobber_inputs.insert(key);
            }
          }
        }
        break;
      }
    }
    maglev_cache_[canon] = std::move(mc);
    return maglev_cache_[canon];
  }

  bool MatchMaglevReadonlyToRegister(const clang::Expr* expr,
                                     const clang::CXXRecordDecl* node_cls,
                                     std::string* out_reason) {
    if (!node_cls || !expr) return false;
    const clang::Expr* cur = UnwrapExpr(expr);
    if (!cur) return false;
    const auto* ce = llvm::dyn_cast<clang::CallExpr>(cur);
    if (!ce || ce->getNumArgs() != 1) return false;
    const clang::FunctionDecl* callee = ce->getDirectCallee();
    if (!callee || !callee->getIdentifier() ||
        callee->getName() != "ToRegister") {
      return false;
    }
    auto [key, label] = ExtractMaglevInputKey(ce->getArg(0));
    if (key.empty()) return false;
    const MaglevConstraints& mc = GetMaglevConstraints(node_cls);
    if (!mc.readonly_inputs.count(key)) return false;
    if (mc.clobber_inputs.count(key)) return false;
    if (mc.has_define_same_as_first && mc.first_input_key == key) return false;
    if (out_reason) {
      *out_reason = "Maglev input '" + label +
                    "' uses UseRegister/UseFixed without UseAndClobberRegister";
    }
    return true;
  }

  bool IsBenignMaglevSameRegExt(llvm::StringRef callee_name,
                                const clang::CallExpr* ce, unsigned out_idx,
                                const Env& env) {
    if (out_idx != 0 || ce->getNumArgs() < 2) return false;
    if (callee_name != "SignExtend32To64Bits" &&
        callee_name != "ZeroExtend32To64Bits" && callee_name != "movsxlq" &&
        callee_name != "movl" && callee_name != "Sxtw" &&
        callee_name != "Uxtw" && callee_name != "sxtw" &&
        callee_name != "uxtw") {
      return false;
    }
    const clang::ValueDecl* s0 = ResolveSymDecl(ce->getArg(0), env);
    const clang::ValueDecl* s1 = ResolveSymDecl(ce->getArg(1), env);
    if (s0 && s0 == s1) return true;
    std::optional<int> p0 = ResolvePhysReg(ce->getArg(0), env);
    std::optional<int> p1 = ResolvePhysReg(ce->getArg(1), env);
    return p0 && p1 && *p0 >= 0 && *p0 == *p1;
  }

  bool IsSymSavedOnStack(const clang::ValueDecl* sym, int phys_reg,
                         const Env& env) {
    for (const SavedStackEntry& e : env.asm_stack) {
      if (e.kind == SavedStackEntry::kCallerSaved ||
          e.kind == SavedStackEntry::kAllRegs) {
        return true;
      }
      if (e.kind == SavedStackEntry::kSingleReg) {
        if ((sym && e.root_decl == sym) ||
            (phys_reg >= 0 && e.phys_reg == phys_reg)) {
          return true;
        }
      }
    }
    return false;
  }

  const clang::FunctionDecl* ResolveCalleeDecl(const clang::CallExpr* ce) {
    if (!ce) return nullptr;
    if (const clang::FunctionDecl* direct = ce->getDirectCallee()) {
      return direct;
    }
    const clang::Expr* callee_expr = UnwrapExpr(ce->getCallee());
    if (const auto* bo =
            llvm::dyn_cast_or_null<clang::BinaryOperator>(callee_expr)) {
      if (bo->getOpcode() == clang::BO_PtrMemI ||
          bo->getOpcode() == clang::BO_PtrMemD) {
        const clang::Expr* rhs = UnwrapExpr(bo->getRHS());
        while (const auto* snttp =
                   llvm::dyn_cast_or_null<clang::SubstNonTypeTemplateParmExpr>(
                       rhs)) {
          rhs = UnwrapExpr(snttp->getReplacement());
        }
        if (const auto* uo =
                llvm::dyn_cast_or_null<clang::UnaryOperator>(rhs)) {
          if (uo->getOpcode() == clang::UO_AddrOf) {
            rhs = UnwrapExpr(uo->getSubExpr());
          }
        }
        if (const auto* dre = llvm::dyn_cast_or_null<clang::DeclRefExpr>(rhs)) {
          if (const auto* fd =
                  llvm::dyn_cast<clang::FunctionDecl>(dre->getDecl())) {
            return fd;
          }
        }
      }
    }
    return nullptr;
  }

  FuncSummary InferAssemblerForwarder(const clang::FunctionDecl* fn,
                                      const FuncSummary& base_sum) {
    FuncSummary out = base_sum;
    const clang::Stmt* body = fn->getBody();
    if (!body) return out;

    std::map<const clang::ValueDecl*, unsigned> param_map;
    for (unsigned i = 0; i < fn->getNumParams(); ++i) {
      param_map[fn->getParamDecl(i)->getCanonicalDecl()] = i;
    }

    class CallFinder : public clang::RecursiveASTVisitor<CallFinder> {
     public:
      std::vector<const clang::CallExpr*> calls;
      bool VisitCallExpr(const clang::CallExpr* ce) {
        calls.push_back(ce);
        return true;
      }
    } finder;
    finder.TraverseStmt(const_cast<clang::Stmt*>(body));

    for (const clang::CallExpr* ce : finder.calls) {
      const clang::FunctionDecl* callee = ce->getDirectCallee();
      if (!callee) continue;
      FuncSummary callee_sum = ExtractExplicitSummary(callee, ast_ctx_, arch_);
      if (!callee_sum.has_explicit_annotations) {
        const clang::FunctionDecl* callee_body = nullptr;
        if (callee->hasBody(callee_body) && callee_body != fn) {
          callee_sum = InferAssemblerForwarder(callee_body, callee_sum);
        }
      }
      if (!callee_sum.has_explicit_annotations) continue;

      out.has_explicit_annotations = true;
      out.implicit_in |= callee_sum.implicit_in;
      out.implicit_out |= callee_sum.implicit_out;
      out.clobbers |= callee_sum.clobbers;
      out.clobbers_caller_saved |= callee_sum.clobbers_caller_saved;
      out.no_fallthrough |= callee_sum.no_fallthrough;
      out.zeroing_if_same |= callee_sum.zeroing_if_same;
      out.is_push |= callee_sum.is_push;
      out.is_pop |= callee_sum.is_pop;

      unsigned n =
          std::min<unsigned>(ce->getNumArgs(), callee_sum.param_roles.size());
      for (unsigned i = 0; i < n; ++i) {
        const clang::Expr* arg = UnwrapExpr(ce->getArg(i));
        if (const auto* dre = llvm::dyn_cast_or_null<clang::DeclRefExpr>(arg)) {
          auto it = param_map.find(CanonValueDecl(dre->getDecl()));
          if (it != param_map.end()) {
            out.param_roles[it->second] = callee_sum.param_roles[i];
          }
        }
      }
    }
    return out;
  }

  FuncSummary AnalyzeFunctionInternal(const clang::FunctionDecl* fn,
                                      const FuncSummary& explicit_sum,
                                      bool emit_diagnostics) {
    const clang::CXXRecordDecl* maglev_cls = nullptr;
    if (const auto* md = llvm::dyn_cast<clang::CXXMethodDecl>(fn)) {
      if (md->getIdentifier() && md->getName() == "GenerateCode") {
        maglev_cls = md->getParent();
      }
    }

    std::map<const clang::ValueDecl*, Env> carried_backedges;
    constexpr int kMaxFixpointPasses = 4;

    FuncSummary inferred;
    std::vector<PendingDiag> final_diags;

    for (int pass = 0; pass < kMaxFixpointPasses; ++pass) {
      FunctionAnalysisContext fctx;
      fctx.fn = fn;
      fctx.maglev_node_cls = maglev_cls;
      fctx.explicit_sum = explicit_sum;
      fctx.emit_diagnostics = emit_diagnostics;
      fctx.carried_backedges = &carried_backedges;

      unsigned num_params = fn->getNumParams();
      fctx.param_read_entry.assign(num_params, false);
      fctx.param_written_any.assign(num_params, false);
      fctx.param_clobbered_any.assign(num_params, false);
      fctx.param_written_before_read_any.assign(num_params, false);
      fctx.early_clobber_phys_before_param.assign(num_params, 0);

      Env env;
      env.arm64_avail_scratch_mask = arch_.arm64_default_tmp_mask;
      for (int r = 0; r < arch_.num_gprs; ++r) {
        env.phys[r].status = RegStatus::kInitial;
        env.phys[r].holds_entry_value = true;
        env.phys[r].def_version = fctx.next_version++;
      }

      for (unsigned i = 0; i < num_params; ++i) {
        const clang::ParmVarDecl* p = fn->getParamDecl(i);
        const clang::ValueDecl* canon_p = p->getCanonicalDecl();
        if (IsTrackableRegType(p->getType())) {
          SymRegState s;
          s.status = RegStatus::kInitial;
          s.root_decl = canon_p;
          s.param_index = static_cast<int>(i);
          s.holds_entry_value = true;
          s.def_version = fctx.next_version++;
          if (i < explicit_sum.param_roles.size() &&
              explicit_sum.param_roles[i] == ParamRole::kRdOnly) {
            s.readonly_kind = ReadonlyKind::kExplicitAttr;
            s.readonly_reason = "parameter '" + p->getNameAsString() +
                                "' is annotated V8_REG_RDONLY";
          }
          env.syms[canon_p] = s;
        } else if (IsOperandType(p->getType())) {
          SymRegState s;
          s.status = RegStatus::kInitial;
          s.root_decl = canon_p;
          s.param_index = static_cast<int>(i);
          s.holds_entry_value = true;
          s.def_version = fctx.next_version++;
          env.syms[canon_p] = s;

          OperandDep dep;
          dep.creation_loc = p->getLocation();
          dep.sym_decls.push_back(canon_p);
          env.operands[canon_p] = dep;
        }
      }

      if (const clang::Stmt* body = fn->getBody()) {
        VisitStmt(body, env, fctx);
        if (env.reachable) {
          fctx.exit_envs.push_back(env);
        }
      }

      inferred = BuildInferredSummary(fctx);
      final_diags = std::move(fctx.pending_diags);

      if (fctx.backward_label_states.empty()) {
        break;
      }

      bool changed = false;
      for (const auto& [lbl, back_env] : fctx.backward_label_states) {
        auto it = carried_backedges.find(lbl);
        if (it == carried_backedges.end()) {
          carried_backedges[lbl] = back_env;
          changed = true;
        } else {
          Env merged = MergeEnvs(it->second, back_env, arch_.num_gprs);
          if (!EnvEquivalentForFixpoint(it->second, merged, arch_.num_gprs)) {
            it->second = std::move(merged);
            changed = true;
          }
        }
      }
      if (!changed) break;

      if (pass + 1 == kMaxFixpointPasses) {
        break;
      }
    }

    if (emit_diagnostics) {
      FlushDiagnostics(final_diags);
      if (g_verify_annotations) {
        VerifyAnnotations(fn, explicit_sum, inferred);
      }
    }
    return inferred;
  }

  // Resolves a symbolic Register (or Operand `.reg()` / `.base()`) expression
  // to its canonical ValueDecl* in env.
  const clang::ValueDecl* ResolveSymDecl(const clang::Expr* expr,
                                         const Env& env) {
    expr = UnwrapExpr(expr);
    if (!expr) return nullptr;

    if (const auto* mce = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
      if (const auto* md = mce->getMethodDecl()) {
        llvm::StringRef mname = SafeDeclName(md);
        if (mce->getNumArgs() == 0 && (mname == "reg" || mname == "base" ||
                                       mname == "regoffset" || mname == "rm")) {
          const clang::Expr* obj = UnwrapExpr(mce->getImplicitObjectArgument());
          if (const auto* dre =
                  llvm::dyn_cast_or_null<clang::DeclRefExpr>(obj)) {
            const clang::ValueDecl* d = CanonValueDecl(dre->getDecl());
            auto sym_it = env.syms.find(d);
            if (sym_it != env.syms.end() && sym_it->second.root_decl) {
              return sym_it->second.root_decl;
            }
            auto op_it = env.operands.find(d);
            if (op_it != env.operands.end() &&
                op_it->second.sym_decls.size() == 1) {
              return op_it->second.sym_decls[0];
            }
          }
        }
      }
    }

    if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
      const clang::ValueDecl* d = CanonValueDecl(dre->getDecl());
      if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(d)) {
        if (vd->isConstexpr() || !vd->isLocalVarDeclOrParm()) {
          return nullptr;
        }
      }
      auto it = env.syms.find(d);
      if (it != env.syms.end() && it->second.root_decl) {
        return it->second.root_decl;
      }
      return d;
    }
    return nullptr;
  }

  std::optional<int> ResolvePhysReg(const clang::Expr* expr, const Env& env) {
    if (!expr) return std::nullopt;
    if (auto c = EvaluateRegisterCode(expr, ast_ctx_, arch_)) {
      return c;
    }
    const clang::Expr* unwrapped = UnwrapExpr(expr);
    if (!unwrapped) return std::nullopt;
    if (auto c = EvaluateRegisterCode(unwrapped, ast_ctx_, arch_)) {
      return c;
    }
    if (const clang::ValueDecl* d = ResolveSymDecl(unwrapped, env)) {
      auto it = env.syms.find(d);
      if (it != env.syms.end() && it->second.phys_reg >= 0) {
        return it->second.phys_reg;
      }
    }
    return std::nullopt;
  }

  const clang::ValueDecl* ResolveLabelDecl(const clang::Expr* expr) {
    expr = UnwrapExpr(expr);
    if (!expr) return nullptr;
    if (const auto* uo = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        expr = UnwrapExpr(uo->getSubExpr());
      }
    }
    if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
      return CanonValueDecl(dre->getDecl());
    }
    return nullptr;
  }

  void MergeSubOperandDep(OperandDep& dst, const OperandDep& sub) {
    for (const auto* sd : sub.sym_decls) {
      if (std::find(dst.sym_decls.begin(), dst.sym_decls.end(), sd) ==
          dst.sym_decls.end()) {
        dst.sym_decls.push_back(sd);
      }
    }
    dst.phys_mask |= sub.phys_mask;
    for (const auto& pv : sub.scratch_phys_versions) {
      dst.scratch_phys_versions.push_back(pv);
    }
    for (const auto& sv : sub.scratch_sym_versions) {
      dst.scratch_sym_versions.push_back(sv);
    }
  }

  OperandDep BuildCallReturnedOperandDep(const clang::CallExpr* ce,
                                         const clang::FunctionDecl* callee,
                                         const FuncSummary& sum, const Env& env,
                                         FunctionAnalysisContext& fctx) {
    OperandDep dep;
    dep.creation_loc = ce->getExprLoc();

    bool has_summary_provenance =
        (sum.returned_operand_scratch_phys_mask != 0 ||
         sum.returned_operand_scratch_param_mask != 0 ||
         sum.returned_operand_input_param_mask != 0 ||
         sum.returned_operand_input_phys_mask != 0);

    if (has_summary_provenance) {
      for (int r = 0; r < arch_.num_gprs; ++r) {
        if (sum.returned_operand_scratch_phys_mask & (1u << r)) {
          dep.scratch_phys_versions.push_back({r, env.phys[r].def_version});
        }
        if (sum.returned_operand_input_phys_mask & (1u << r)) {
          dep.phys_mask |= (1u << r);
        }
      }

      unsigned n = std::min<unsigned>(ce->getNumArgs(), 32u);
      for (unsigned i = 0; i < n; ++i) {
        const clang::Expr* arg = ce->getArg(i);
        if (sum.returned_operand_scratch_param_mask & (1u << i)) {
          if (llvm::isa<clang::CXXDefaultArgExpr>(arg)) {
            if (auto def_p = EvaluateRegisterCode(arg, ast_ctx_, arch_)) {
              if (*def_p >= 0 && *def_p < arch_.num_gprs) {
                dep.scratch_phys_versions.push_back(
                    {*def_p, env.phys[*def_p].def_version});
              }
            }
          } else if (auto p = ResolvePhysReg(arg, env)) {
            if (*p >= 0 && *p < arch_.num_gprs) {
              dep.scratch_phys_versions.push_back(
                  {*p, env.phys[*p].def_version});
            }
          } else if (const clang::ValueDecl* sd = ResolveSymDecl(arg, env)) {
            auto it = env.syms.find(sd);
            if (it != env.syms.end()) {
              dep.scratch_sym_versions.push_back({sd, it->second.def_version});
            }
          }
        } else if (sum.returned_operand_input_param_mask & (1u << i)) {
          if (llvm::isa<clang::CXXDefaultArgExpr>(arg)) continue;
          const clang::Expr* uarg = UnwrapExpr(arg);
          if (!uarg) continue;
          if (IsTrackableRegType(uarg->getType())) {
            if (const clang::ValueDecl* sd = ResolveSymDecl(uarg, env)) {
              dep.sym_decls.push_back(sd);
            }
            if (auto pr = ResolvePhysReg(uarg, env)) {
              if (*pr >= 0 && *pr < arch_.num_gprs) {
                dep.phys_mask |= (1u << *pr);
              }
            }
          } else if (IsOperandType(uarg->getType())) {
            Env const_env = env;
            MergeSubOperandDep(dep, ExtractOperandDep(uarg, const_env, fctx));
          }
        }
      }
      return dep;
    }

    // Fallback for unanalyzed constructors/helpers like FieldOperand(base, 8):
    // collect all Register / Operand arguments.
    (void)callee;
    for (const clang::Expr* a : ce->arguments()) {
      if (llvm::isa<clang::CXXDefaultArgExpr>(a)) continue;
      const clang::Expr* ua = UnwrapExpr(a);
      if (!ua) continue;
      if (IsTrackableRegType(ua->getType())) {
        if (const clang::ValueDecl* sd = ResolveSymDecl(ua, env)) {
          dep.sym_decls.push_back(sd);
        }
        if (auto pr = ResolvePhysReg(ua, env)) {
          if (*pr >= 0 && *pr < arch_.num_gprs) {
            dep.phys_mask |= (1u << *pr);
          }
        }
      } else if (IsOperandType(ua->getType())) {
        Env const_env = env;
        MergeSubOperandDep(dep, ExtractOperandDep(ua, const_env, fctx));
      }
    }
    return dep;
  }

  OperandDep ExtractOperandDep(const clang::Expr* expr, Env& env,
                               FunctionAnalysisContext& fctx) {
    OperandDep dep;
    expr = UnwrapExpr(expr);
    if (!expr) return dep;
    dep.creation_loc = expr->getExprLoc();

    if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
      const clang::ValueDecl* d = CanonValueDecl(dre->getDecl());
      auto it = env.operands.find(d);
      if (it != env.operands.end()) {
        return it->second;
      }
      if (env.syms.find(d) != env.syms.end()) {
        dep.sym_decls.push_back(d);
      }
      return dep;
    }

    if (const auto* cond_op =
            llvm::dyn_cast<clang::ConditionalOperator>(expr)) {
      MergeSubOperandDep(dep,
                         ExtractOperandDep(cond_op->getTrueExpr(), env, fctx));
      MergeSubOperandDep(dep,
                         ExtractOperandDep(cond_op->getFalseExpr(), env, fctx));
      return dep;
    }

    if (const auto* ce = llvm::dyn_cast<clang::CallExpr>(expr)) {
      auto cached_it = fctx.call_operand_deps.find(ce);
      if (cached_it != fctx.call_operand_deps.end()) {
        return cached_it->second;
      }
      if (const clang::FunctionDecl* callee = ResolveCalleeDecl(ce)) {
        const FuncSummary& sum = GetOrInferSummary(callee);
        return BuildCallReturnedOperandDep(ce, callee, sum, env, fctx);
      }
    }

    if (const auto* cxx_construct =
            llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
      for (const clang::Expr* a : cxx_construct->arguments()) {
        if (llvm::isa<clang::CXXDefaultArgExpr>(a)) continue;
        const clang::Expr* ua = UnwrapExpr(a);
        if (!ua) continue;
        if (IsTrackableRegType(ua->getType())) {
          if (const clang::ValueDecl* sd = ResolveSymDecl(ua, env)) {
            dep.sym_decls.push_back(sd);
          }
          if (auto pr = ResolvePhysReg(ua, env)) {
            if (*pr >= 0 && *pr < arch_.num_gprs) {
              dep.phys_mask |= (1u << *pr);
            }
          }
        } else if (IsOperandType(ua->getType())) {
          MergeSubOperandDep(dep, ExtractOperandDep(ua, env, fctx));
        }
      }
    }
    return dep;
  }

  void RecordEntryParamReadAndCheckEarlyClobber(unsigned p_idx,
                                                const clang::ValueDecl* sym,
                                                clang::SourceLocation use_loc,
                                                Env& env,
                                                FunctionAnalysisContext& fctx) {
    if (p_idx >= fctx.param_read_entry.size()) return;
    fctx.param_read_entry[p_idx] = true;
    if (p_idx < 32) {
      env.params_read_mask |= (1u << p_idx);
    }
    unsigned lim = std::min<unsigned>(fctx.fn->getNumParams(), 32u);
    for (unsigned w = 0; w < lim; ++w) {
      if (w == p_idx || !(env.params_written_mask & (1u << w))) continue;
      const clang::ValueDecl* w_decl =
          fctx.fn->getParamDecl(w)->getCanonicalDecl();
      if (env.known_no_reg.contains(w_decl)) continue;
      if (env.known_not_equal.contains({w_decl, sym}) ||
          env.known_not_equal.contains({sym, w_decl})) {
        continue;
      }
      fctx.early_clobber_param_pairs.insert({w, p_idx});

      // On ARM64, if the author guarded this overwrite-before-read using
      // size-sensitive `!=` / `!(==)` on CPURegister/Register, warn directly!
      auto ss_it = env.size_sensitive_not_equal.find({w_decl, sym});
      if (ss_it != env.size_sensitive_not_equal.end()) {
        ClobberNote guard_note{
            ss_it->second,
            "size-sensitive '!=' / '==' guard here compares register width "
            "(wN != xN is true on ARM64)"};
        QueueWarning(fctx, use_loc,
                     "Register '" + w_decl->getNameAsString() +
                         "' is overwritten before reading '" +
                         sym->getNameAsString() +
                         "' under a size-sensitive '!=' guard on ARM64; use "
                         "!Aliases() or !AreAliased() instead.",
                     &guard_note);
      }
    }
    fctx.early_clobber_phys_before_param[p_idx] |= env.phys_written_mask;
  }

  static const clang::Expr* UnwrapImplicitRegToOperand(
      const clang::Expr* expr) {
    const clang::Expr* cur = expr;
    while (cur) {
      cur = cur->IgnoreParens();
      if (const auto* clean = llvm::dyn_cast<clang::ExprWithCleanups>(cur)) {
        cur = clean->getSubExpr();
        continue;
      }
      if (const auto* mat =
              llvm::dyn_cast<clang::MaterializeTemporaryExpr>(cur)) {
        cur = mat->getSubExpr();
        continue;
      }
      if (const auto* bind = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(cur)) {
        cur = bind->getSubExpr();
        continue;
      }
      if (const auto* ice = llvm::dyn_cast<clang::ImplicitCastExpr>(cur)) {
        if (ice->getCastKind() == clang::CK_NoOp) {
          cur = ice->getSubExpr();
          continue;
        }
        if (ice->getCastKind() == clang::CK_ConstructorConversion) {
          const clang::Expr* sub = ice->getSubExpr()->IgnoreParens();
          if (const auto* b2 =
                  llvm::dyn_cast<clang::CXXBindTemporaryExpr>(sub)) {
            sub = b2->getSubExpr()->IgnoreParens();
          }
          if (const auto* cce = llvm::dyn_cast<clang::CXXConstructExpr>(sub)) {
            if (cce->getNumArgs() >= 1 &&
                IsTrackableRegType(cce->getArg(0)->getType())) {
              bool rest_default = true;
              for (unsigned i = 1; i < cce->getNumArgs(); ++i) {
                if (!llvm::isa<clang::CXXDefaultArgExpr>(cce->getArg(i))) {
                  rest_default = false;
                  break;
                }
              }
              if (rest_default) return cce->getArg(0);
            }
          }
        }
      }
      break;
    }
    return nullptr;
  }

  void ReadRegisterExpr(const clang::Expr* expr, clang::SourceLocation use_loc,
                        Env& env, FunctionAnalysisContext& fctx) {
    if (!env.reachable || !expr) return;
    if (llvm::isa<clang::CXXDefaultArgExpr>(expr)) return;
    if (const clang::Expr* imp_reg = UnwrapImplicitRegToOperand(expr)) {
      expr = imp_reg;
    }
    const clang::Expr* uexpr = UnwrapExpr(expr);
    if (!uexpr) return;

    if (IsOperandType(uexpr->getType())) {
      ReadOperandExpr(uexpr, use_loc, env, fctx);
      return;
    }
    if (!IsTrackableRegType(uexpr->getType())) return;

    const clang::ValueDecl* sym = ResolveSymDecl(uexpr, env);
    std::optional<int> phys = ResolvePhysReg(expr, env);

    if (sym) {
      if (env.known_no_reg.contains(sym)) return;
      auto it = env.syms.find(sym);
      if (it != env.syms.end()) {
        SymRegState& s = it->second;
        if (s.status == RegStatus::kClobbered) {
          QueueWarning(fctx, use_loc,
                       "Use of register '" + sym->getNameAsString() +
                           "' after it was clobbered.",
                       &s.clobber);
        }
        if (s.status == RegStatus::kInitial && s.param_index >= 0) {
          RecordEntryParamReadAndCheckEarlyClobber(
              static_cast<unsigned>(s.param_index), sym, use_loc, env, fctx);
        }
      }
    }

    if (phys && *phys >= 0 && *phys < arch_.num_gprs) {
      int r = *phys;
      PhysRegState& ps = env.phys[r];
      if (ps.status == RegStatus::kClobbered && !sym) {
        QueueWarning(fctx, use_loc,
                     std::string("Use of physical register '") +
                         arch_.gpr_names[r] + "' after it was clobbered.",
                     &ps.clobber);
      }
      if (ps.status == RegStatus::kInitial && ps.holds_entry_value && !sym) {
        if (!(arch_.pinned_mask & (1u << r))) {
          fctx.phys_read_entry |= (1u << r);
        }
      }
    }
  }

  void ReadOperandExpr(const clang::Expr* expr, clang::SourceLocation use_loc,
                       Env& env, FunctionAnalysisContext& fctx) {
    OperandDep dep = ExtractOperandDep(expr, env, fctx);
    std::string kind_name =
        IsTypeNamed(expr->getType(), "MemOperand") ? "MemOperand" : "Operand";
    std::string op_name = kind_name;
    if (const auto* dre =
            llvm::dyn_cast_or_null<clang::DeclRefExpr>(UnwrapExpr(expr))) {
      op_name = kind_name + " '" + dre->getDecl()->getNameAsString() + "'";
    }

    for (const auto& [r, ver] : dep.scratch_phys_versions) {
      if (r >= 0 && r < arch_.num_gprs && env.phys[r].def_version != ver) {
        QueueWarning(fctx, use_loc,
                     "Use of " + op_name +
                         " after its underlying scratch register '" +
                         arch_.gpr_names[r] + "' was overwritten.",
                     &env.phys[r].clobber);
      }
    }

    for (const auto& [sd, ver] : dep.scratch_sym_versions) {
      if (!sd || env.known_no_reg.contains(sd)) continue;
      auto it = env.syms.find(sd);
      if (it != env.syms.end() && it->second.def_version != ver) {
        QueueWarning(fctx, use_loc,
                     "Use of " + op_name +
                         " after its underlying scratch register '" +
                         sd->getNameAsString() + "' was overwritten.",
                     &it->second.clobber);
      }
    }

    for (const clang::ValueDecl* sd : dep.sym_decls) {
      if (env.known_no_reg.contains(sd)) continue;
      auto it = env.syms.find(sd);
      if (it != env.syms.end()) {
        SymRegState& s = it->second;
        if (s.status == RegStatus::kClobbered) {
          QueueWarning(fctx, use_loc,
                       "Use of " + op_name + " whose base/index register '" +
                           sd->getNameAsString() + "' was clobbered.",
                       &s.clobber);
        }
        if (s.status == RegStatus::kInitial && s.param_index >= 0) {
          RecordEntryParamReadAndCheckEarlyClobber(
              static_cast<unsigned>(s.param_index), sd, use_loc, env, fctx);
        }
      }
    }

    for (int r = 0; r < arch_.num_gprs; ++r) {
      if (dep.phys_mask & (1u << r)) {
        PhysRegState& ps = env.phys[r];
        if (ps.status == RegStatus::kClobbered && dep.sym_decls.empty()) {
          QueueWarning(fctx, use_loc,
                       "Use of " + op_name + " whose physical register '" +
                           arch_.gpr_names[r] + "' was clobbered.",
                       &ps.clobber);
        }
        if (ps.status == RegStatus::kInitial && ps.holds_entry_value &&
            dep.sym_decls.empty()) {
          if (!(arch_.pinned_mask & (1u << r))) {
            fctx.phys_read_entry |= (1u << r);
          }
        }
      }
    }
  }

  void WriteOrClobberSym(const clang::ValueDecl* sym, bool is_clobber,
                         clang::SourceLocation loc, const std::string& reason,
                         Env& env, FunctionAnalysisContext& fctx) {
    if (!sym || env.known_no_reg.contains(sym)) return;
    auto it = env.syms.find(sym);
    if (it == env.syms.end()) return;
    SymRegState& s = it->second;

    if (s.readonly_kind == ReadonlyKind::kExplicitAttr &&
        !IsSymSavedOnStack(sym, s.phys_reg, env)) {
      QueueWarning(fctx, loc,
                   "Cannot modify/clobber read-only register '" +
                       sym->getNameAsString() + "' (" + s.readonly_reason +
                       ").");
    }

    if (s.param_index >= 0) {
      unsigned p_idx = static_cast<unsigned>(s.param_index);
      bool aliases_known_out_param = false;
      if (p_idx < fctx.explicit_sum.param_roles.size() &&
          IsInputLikeRole(fctx.explicit_sum.param_roles[p_idx])) {
        for (unsigned k = 0; k < fctx.fn->getNumParams() &&
                             k < fctx.explicit_sum.param_roles.size();
             ++k) {
          if (k == p_idx) continue;
          ParamRole rk = fctx.explicit_sum.param_roles[k];
          if (rk == ParamRole::kOut || rk == ParamRole::kInOut ||
              rk == ParamRole::kScratch || rk == ParamRole::kInClobber) {
            const clang::ValueDecl* pk =
                fctx.fn->getParamDecl(k)->getCanonicalDecl();
            if (env.known_equal.contains({sym, pk}) ||
                env.known_equal.contains({pk, sym})) {
              aliases_known_out_param = true;
              fctx.param_written_any[k] = true;
              break;
            }
          }
        }
      }
      if (!aliases_known_out_param) {
        if (s.status == RegStatus::kInitial && p_idx < 32 &&
            !(env.params_read_mask & (1u << p_idx))) {
          fctx.param_written_before_read_any[p_idx] = true;
        }
        if (p_idx < 32) {
          env.params_written_mask |= (1u << p_idx);
        }
        if (is_clobber) {
          fctx.param_clobbered_any[p_idx] = true;
        } else {
          fctx.param_written_any[p_idx] = true;
        }
      }
    }
    s.status = is_clobber ? RegStatus::kClobbered : RegStatus::kAlive;
    s.holds_entry_value = false;
    s.def_version = fctx.next_version++;
    if (is_clobber) {
      s.clobber = {loc, reason};
    } else {
      s.clobber = {loc, "overwritten here"};
    }
    if (s.phys_reg >= 0 && s.phys_reg < arch_.num_gprs) {
      WriteOrClobberPhys(s.phys_reg, is_clobber,
                         /*from_sym_or_scratch_scope=*/
                         (s.param_index >= 0 || s.scratch_slot >= 0), loc,
                         reason, env, fctx, /*skip_sym=*/sym);
    }
  }

  void WriteOrClobberPhys(int r, bool is_clobber, bool from_sym_param,
                          clang::SourceLocation loc, const std::string& reason,
                          Env& env, FunctionAnalysisContext& fctx,
                          const clang::ValueDecl* skip_sym = nullptr) {
    if (r < 0 || r >= arch_.num_gprs) return;
    if (arch_.pinned_mask & (1u << r)) return;

    for (auto& [decl, s] : env.syms) {
      if (decl == skip_sym) continue;
      if (s.phys_reg == r) {
        if (s.readonly_kind == ReadonlyKind::kExplicitAttr &&
            !IsSymSavedOnStack(decl, r, env)) {
          QueueWarning(fctx, loc,
                       "Cannot modify/clobber read-only register '" +
                           decl->getNameAsString() + "' (" + s.readonly_reason +
                           ").");
        }
        if (s.param_index >= 0) {
          from_sym_param = true;
          unsigned p_idx = static_cast<unsigned>(s.param_index);
          if (s.status == RegStatus::kInitial && p_idx < 32 &&
              !(env.params_read_mask & (1u << p_idx))) {
            fctx.param_written_before_read_any[p_idx] = true;
          }
          if (p_idx < 32) {
            env.params_written_mask |= (1u << p_idx);
          }
          if (is_clobber) {
            fctx.param_clobbered_any[p_idx] = true;
          } else {
            fctx.param_written_any[p_idx] = true;
          }
        }
        bool sym_clobbered =
            is_clobber || (skip_sym == nullptr && s.scratch_slot >= 0);
        s.status = sym_clobbered ? RegStatus::kClobbered : RegStatus::kAlive;
        s.holds_entry_value = false;
        s.def_version = fctx.next_version++;
        s.clobber = {loc, reason};
      }
    }

    PhysRegState& ps = env.phys[r];
    ps.status = is_clobber ? RegStatus::kClobbered : RegStatus::kAlive;
    ps.holds_entry_value = false;
    ps.def_version = fctx.next_version++;
    ps.clobber = {loc, reason};

    if (!from_sym_param) {
      env.phys_written_mask |= (1u << r);
      if (is_clobber) {
        fctx.phys_clobbered_any |= (1u << r);
      } else {
        fctx.phys_written_any |= (1u << r);
      }
    }
  }

  ScratchScopeKind MatchScratchAcquireCall(const clang::Expr* expr) {
    expr = UnwrapExpr(expr);
    const auto* mce = llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(expr);
    if (!mce) return ScratchScopeKind::kNone;
    const auto* md = mce->getMethodDecl();
    if (!md || !IsCoreGprRegType(mce->getType())) {
      return ScratchScopeKind::kNone;
    }
    ScratchScopeKind sk = GetScratchScopeKind(mce->getObjectType());
    if (sk == ScratchScopeKind::kNone) return ScratchScopeKind::kNone;
    llvm::StringRef mname = SafeDeclName(md);
    if (mname == "AcquireScratch" || mname == "AcquireX" ||
        mname == "AcquireW" || mname == "AcquireSameSizeAs" ||
        mname == "Acquire") {
      return sk;
    }
    return ScratchScopeKind::kNone;
  }

  static bool TakesAssemblerParam(const clang::FunctionDecl* fn) {
    if (!fn) return false;
    for (unsigned i = 0; i < fn->getNumParams(); ++i) {
      clang::QualType pt = fn->getParamDecl(i)->getType();
      clang::QualType deref =
          pt->isPointerType() ? pt->getPointeeType() : pt.getNonReferenceType();
      if (const auto* rec = deref->getAsCXXRecordDecl()) {
        if (GetRecordKind(rec) != RecordKind::kOther) return true;
      }
    }
    return false;
  }

  void ApplyCallEffect(const clang::CallExpr* ce, Env& env,
                       FunctionAnalysisContext& fctx) {
    if (!env.reachable) return;
    const clang::FunctionDecl* callee = ResolveCalleeDecl(ce);
    if (!callee) return;

    std::string callee_name = callee->getNameAsString();
    clang::SourceLocation call_loc = ce->getExprLoc();

    // 1. Handle UNREACHABLE / Abort / Fatals / DCHECK failure handlers
    if (callee_name == "V8_Fatal" || callee_name == "UNREACHABLE" ||
        callee_name == "V8_Dcheck" || callee_name == "FatalNoSecurityImpact") {
      env.reachable = false;
      return;
    }

    // 2. Handle UseScratchRegisterScope::Include / Exclude and Acquire*, and
    // ignore host-side C++ metadata queries on Register/Operand/RegList types
    // (such as `.Aliases()`, `AreAliased()`, `DCHECK_NE()`, `RegList::has()`),
    // as well as `const` or pure `static` methods on Assembler/MacroAssembler
    // (such as `Rd(rd)`, `Rt(rt)`, `AppropriateZeroRegFor(dst)`), which cannot
    // emit instructions into an assembler buffer.
    if (llvm::isa<clang::CXXOperatorCallExpr>(ce)) {
      return;
    }
    if (const auto* md = llvm::dyn_cast<clang::CXXMethodDecl>(callee)) {
      if (GetRecordKind(md->getParent()) != RecordKind::kOther) {
        if (md->isConst() || (md->isStatic() && !TakesAssemblerParam(md))) {
          return;
        }
      }
    }
    if (const auto* mce = llvm::dyn_cast<clang::CXXMemberCallExpr>(ce)) {
      clang::QualType obj_qt = mce->getObjectType();
      ScratchScopeKind sk = GetScratchScopeKind(obj_qt);
      if (sk != ScratchScopeKind::kNone) {
        if (callee_name == "Exclude" || callee_name == "Include" ||
            callee_name == "ExcludeAll") {
          if (callee_name == "ExcludeAll") {
            env.arm64_avail_scratch_mask = 0;
            env.arm64_included_scratch_mask = 0;
            return;
          }
          for (const clang::Expr* arg : ce->arguments()) {
            if (llvm::isa<clang::CXXDefaultArgExpr>(arg)) continue;
            if (auto r = ResolvePhysReg(arg, env)) {
              if (*r >= 0 && *r < arch_.num_gprs) {
                if (callee_name == "Exclude") {
                  env.arm64_avail_scratch_mask &= ~(1u << *r);
                  env.arm64_included_scratch_mask &= ~(1u << *r);
                } else {
                  env.arm64_avail_scratch_mask |= (1u << *r);
                  env.arm64_included_scratch_mask |= (1u << *r);
                }
              }
            }
          }
          return;
        }
        return;  // Acquire* is handled at VarDecl init / assignment; others are
                 // metadata.
      }
      if (IsTrackableRegType(obj_qt) || IsOperandType(obj_qt) ||
          IsTypeNamed(obj_qt, "RegListBase") ||
          IsTypeNamed(obj_qt, "CPURegList") ||
          IsTypeNamed(obj_qt, "CacheState") ||
          IsTypeNamed(obj_qt, "VarState")) {
        return;
      }
    }
    if (const auto* md_static = llvm::dyn_cast<clang::CXXMethodDecl>(callee)) {
      if (md_static->isStatic()) {
        clang::QualType parent_qt =
            ast_ctx_.getCanonicalTagType(md_static->getParent());
        if (IsTrackableRegType(parent_qt) || IsOperandType(parent_qt)) {
          return;
        }
      }
    }
    if (callee_name == "AreAliased" || callee_name == "AreSameSizeAndType" ||
        callee_name == "AreSameFormat" || callee_name == "AreConsecutive" ||
        callee_name == "AreEven" || callee_name == "CheckOpImpl" ||
        callee_name == "MakeCheckOpValueString" ||
        callee_name == "MakeCheckOpString" ||
        callee_name == "GetAllocatableRegisterThatIsNotOneOf" ||
        callee_name == "SafeCall" || callee_name == "CallIf" ||
        callee_name == "CallFrameSetupStub") {
      return;
    }

    // 3. Handle PushCallerSaved / PopCallerSaved / PushAll / PopAll /
    // MaybeSaveRegisters / MaybeRestoreRegisters
    if (callee_name == "PushCallerSaved" || callee_name == "PushAll" ||
        callee_name == "MaybeSaveRegisters") {
      SavedStackEntry entry;
      entry.kind = (callee_name == "PushCallerSaved")
                       ? SavedStackEntry::kCallerSaved
                       : SavedStackEntry::kAllRegs;
      for (int r = 0; r < arch_.num_gprs; ++r) {
        entry.saved_phys_all[r] = env.phys[r];
      }
      entry.saved_syms_all = env.syms;
      entry.saved_clobbered_caller_saved = env.clobbered_caller_saved;
      env.asm_stack.push_back(std::move(entry));
      return;
    }
    if (callee_name == "PopCallerSaved" || callee_name == "PopAll" ||
        callee_name == "MaybeRestoreRegisters") {
      const clang::ValueDecl* excl_sym = nullptr;
      std::optional<int> excl_phys;
      if (callee_name == "PopCallerSaved" && ce->getNumArgs() >= 2 &&
          !llvm::isa<clang::CXXDefaultArgExpr>(ce->getArg(1))) {
        excl_sym = ResolveSymDecl(ce->getArg(1), env);
        excl_phys = ResolvePhysReg(ce->getArg(1), env);
      }
      if (!env.asm_stack.empty() &&
          (env.asm_stack.back().kind == SavedStackEntry::kCallerSaved ||
           env.asm_stack.back().kind == SavedStackEntry::kAllRegs)) {
        SavedStackEntry entry = std::move(env.asm_stack.back());
        env.asm_stack.pop_back();
        env.clobbered_caller_saved = entry.saved_clobbered_caller_saved;
        for (int r = 0; r < arch_.num_gprs; ++r) {
          if (excl_phys && *excl_phys == r) continue;
          env.phys[r] = entry.saved_phys_all[r];
        }
        for (const auto& [d, saved_s] : entry.saved_syms_all) {
          if (excl_sym && d == excl_sym) continue;
          env.syms[d] = saved_s;
        }
      }
      return;
    }

    // 4. Special case: CallBuiltin / near_call / Call with register-preserving
    // write-barrier builtins (RecordWrite, EphemeronKeyBarrier,
    // TrustedPointerBarrier).
    if ((callee_name == "CallBuiltin" || callee_name == "near_call" ||
         callee_name == "Call") &&
        ce->getNumArgs() >= 1) {
      std::string caller_name = fctx.fn->getNameAsString();
      if ((callee_name == "near_call" || callee_name == "Call") &&
          caller_name.find("RecordWrite") != std::string::npos) {
        if (arch_.kind == ArchKind::kArm64) {
          WriteOrClobberPhys(30, /*is_clobber=*/true, /*from_sym_param=*/false,
                             call_loc, "clobbered by '" + callee_name + "'",
                             env, fctx);
        }
        return;
      }
      const clang::Expr* b_arg = UnwrapExpr(ce->getArg(0));
      std::string b_str;
      llvm::raw_string_ostream os(b_str);
      b_arg->printPretty(os, nullptr, ast_ctx_.getPrintingPolicy());
      os.flush();
      if (b_str.find("RecordWrite") != std::string::npos ||
          b_str.find("EphemeronKeyBarrier") != std::string::npos ||
          b_str.find("TrustedPointerBarrier") != std::string::npos ||
          b_str.find("kDoubleToI") != std::string::npos || b_str == "target" ||
          b_str == "builtin") {
        if ((b_str != "target" && b_str != "builtin") ||
            caller_name.find("Barrier") != std::string::npos ||
            caller_name.find("RecordWrite") != std::string::npos) {
          if (arch_.kind == ArchKind::kArm64 &&
              b_str.find("kDoubleToI") == std::string::npos) {
            WriteOrClobberPhys(30, /*is_clobber=*/true,
                               /*from_sym_param=*/false, call_loc,
                               "clobbered by '" + callee_name + "'", env, fctx);
          }
          return;
        }
      }
    }

    const FuncSummary& sum = GetOrInferSummary(callee);

    // 5. Check for Zeroing Idiom:
    // - 2-arg x64: `xorq(reg, reg)` / `subq(reg, reg)`
    // - 3-arg arm64: `eor(rd, rn, Operand(rn))` / `sub(rd, rn, Operand(rn))`
    bool is_same_reg_zeroing = false;
    if (sum.zeroing_if_same && ce->getNumArgs() >= 2) {
      unsigned idx_a = (ce->getNumArgs() >= 3) ? 1 : 0;
      unsigned idx_b = (ce->getNumArgs() >= 3) ? 2 : 1;
      const clang::ValueDecl* sa = ResolveSymDecl(ce->getArg(idx_a), env);
      const clang::ValueDecl* sb = ResolveSymDecl(ce->getArg(idx_b), env);
      std::optional<int> pa = ResolvePhysReg(ce->getArg(idx_a), env);
      std::optional<int> pb = ResolvePhysReg(ce->getArg(idx_b), env);
      if (IsOperandType(UnwrapExpr(ce->getArg(idx_b))->getType())) {
        OperandDep b_dep = ExtractOperandDep(ce->getArg(idx_b), env, fctx);
        if (b_dep.sym_decls.size() == 1 && b_dep.phys_mask == 0) {
          sb = b_dep.sym_decls[0];
        }
      }
      if ((sa && sa == sb) || (pa && pb && *pa >= 0 && *pa == *pb)) {
        is_same_reg_zeroing = true;
      }
    }

    // 6. Check for Early-Clobber / Input-Output Aliasing at Call Site!
    unsigned num_args =
        std::min<unsigned>(ce->getNumArgs(), sum.param_roles.size());
    if (fctx.emit_diagnostics) {
      CheckCallSiteAliasing(ce, callee, sum, num_args, env, fctx);
    }

    // 7. Process all READS first (within a single instruction/call, inputs are
    // read before outputs/clobbers are written).
    for (unsigned i = 0; i < num_args; ++i) {
      ParamRole role = sum.param_roles[i];
      if (is_same_reg_zeroing) {
        if (ce->getNumArgs() == 2 && (i == 0 || i == 1)) continue;
        if (ce->getNumArgs() >= 3 && (i == 1 || i == 2)) continue;
      }
      if (IsInputLikeRole(role) || role == ParamRole::kInOut ||
          role == ParamRole::kInClobber) {
        ReadRegisterExpr(ce->getArg(i), ce->getArg(i)->getExprLoc(), env, fctx);
      }
    }

    // Implicit physical register reads:
    for (int r = 0; r < arch_.num_gprs; ++r) {
      if (sum.implicit_in & (1u << r)) {
        PhysRegState& ps = env.phys[r];
        if (ps.status == RegStatus::kClobbered) {
          QueueWarning(fctx, call_loc,
                       "Call to '" + callee_name +
                           "' implicitly reads register '" +
                           arch_.gpr_names[r] + "' after it was clobbered.",
                       &ps.clobber);
        }
        if (ps.status == RegStatus::kInitial && ps.holds_entry_value) {
          if (!(arch_.pinned_mask & (1u << r))) {
            fctx.phys_read_entry |= (1u << r);
          }
        }
      }
    }

    // 8. Handle V8_ASM_PUSH before writes (supports multi-register Push(r0,
    // r1, ...) in order!)
    if (sum.is_push && ce->getNumArgs() >= 1) {
      for (unsigned i = 0; i < ce->getNumArgs(); ++i) {
        const clang::Expr* argi = ce->getArg(i);
        if (llvm::isa<clang::CXXDefaultArgExpr>(argi)) continue;
        const clang::Expr* uarg = UnwrapExpr(argi);
        if (!uarg || !IsTrackableRegType(uarg->getType())) continue;
        std::optional<int> p = ResolvePhysReg(argi, env);
        if (p && *p == -1) break;  // NoReg terminates ARM64 Push list

        SavedStackEntry entry;
        entry.kind = SavedStackEntry::kSingleReg;
        entry.root_decl = ResolveSymDecl(uarg, env);
        if (entry.root_decl && env.syms.count(entry.root_decl)) {
          entry.saved_sym = env.syms[entry.root_decl];
          if (i == 0 && entry.saved_sym.param_index == 0) {
            fctx.any_push_param0 = true;
          }
        }
        if (p && *p >= 0 && *p < arch_.num_gprs) {
          entry.phys_reg = *p;
          entry.saved_phys = env.phys[*p];
        }
        env.asm_stack.push_back(std::move(entry));
      }
    }

    // 9. Process explicit parameter WRITES and CLOBBERS
    for (unsigned i = 0; i < num_args; ++i) {
      ParamRole role = sum.param_roles[i];
      if (is_same_reg_zeroing && i == 0) {
        role = ParamRole::kOut;
      }
      if (role == ParamRole::kOut || role == ParamRole::kInOut ||
          role == ParamRole::kScratch || role == ParamRole::kInClobber) {
        bool is_clobber =
            (role == ParamRole::kScratch || role == ParamRole::kInClobber);
        const clang::Expr* arg = ce->getArg(i);
        if (llvm::isa<clang::CXXDefaultArgExpr>(arg)) {
          if (auto def_phys = EvaluateRegisterCode(arg, ast_ctx_, arch_)) {
            if (*def_phys >= 0 && *def_phys < arch_.num_gprs) {
              WriteOrClobberPhys(*def_phys, is_clobber,
                                 /*from_sym_param=*/false, call_loc,
                                 "clobbered by default scratch argument in '" +
                                     callee_name + "'",
                                 env, fctx);
            }
          }
          continue;
        }
        const clang::Expr* uarg = UnwrapExpr(arg);
        if (!uarg || !IsTrackableRegType(uarg->getType())) continue;

        const clang::ValueDecl* sym = ResolveSymDecl(uarg, env);
        std::optional<int> phys = ResolvePhysReg(arg, env);

        // Check Maglev read-only input constraints (both bound variables and
        // inline `ToRegister(Input())` calls):
        if (fctx.maglev_node_cls &&
            !IsBenignMaglevSameRegExt(callee_name, ce, i, env)) {
          if (sym && env.syms.count(sym) &&
              env.syms[sym].readonly_kind == ReadonlyKind::kMaglevInput) {
            QueueWarning(fctx, call_loc,
                         "Cannot modify/clobber Maglev input register '" +
                             sym->getNameAsString() + "' in '" + callee_name +
                             "' (" + env.syms[sym].readonly_reason + ").");
          } else if (!sym) {
            std::string reason;
            if (MatchMaglevReadonlyToRegister(arg, fctx.maglev_node_cls,
                                              &reason)) {
              QueueWarning(fctx, call_loc,
                           "Cannot modify/clobber Maglev input register in '" +
                               callee_name + "' (" + reason + ").");
            }
          }
        }

        std::string reason = (is_clobber ? "clobbered as scratch argument to '"
                                         : "overwritten by '") +
                             callee_name + "'";
        if (sym) {
          WriteOrClobberSym(sym, is_clobber, call_loc, reason, env, fctx);
        } else if (phys && *phys >= 0 && *phys < arch_.num_gprs) {
          WriteOrClobberPhys(*phys, is_clobber, /*from_sym_param=*/false,
                             call_loc, reason, env, fctx);
        }
      }
    }

    // 10. Handle V8_ASM_POP (restores saved register state if matching top of
    // stack; on ARM64 `Pop(a, b)` pops `a` then `b` where `Push(b, a)` pushed
    // `b` then `a`, so `Pop(a, b)` pops in argument order!)
    if (sum.is_pop && ce->getNumArgs() >= 1) {
      for (unsigned i = 0; i < ce->getNumArgs(); ++i) {
        const clang::Expr* argi = ce->getArg(i);
        if (llvm::isa<clang::CXXDefaultArgExpr>(argi)) continue;
        const clang::Expr* uarg = UnwrapExpr(argi);
        if (!uarg || !IsTrackableRegType(uarg->getType())) continue;
        std::optional<int> phys = ResolvePhysReg(argi, env);
        if (phys && *phys == -1) break;

        const clang::ValueDecl* sym = ResolveSymDecl(uarg, env);
        if (i == 0 && sym && env.syms.count(sym) &&
            env.syms[sym].param_index == 0) {
          fctx.any_pop_param0 = true;
        }
        if (!env.asm_stack.empty() &&
            env.asm_stack.back().kind == SavedStackEntry::kSingleReg) {
          // If an intervening runtime call popped its pushed stack arguments,
          // scan down `env.asm_stack` to match `sym` when present.
          if (env.clobbered_caller_saved && sym &&
              env.asm_stack.back().root_decl != sym) {
            for (int k = static_cast<int>(env.asm_stack.size()) - 1; k >= 0;
                 --k) {
              if (env.asm_stack[k].kind != SavedStackEntry::kSingleReg) break;
              if (env.asm_stack[k].root_decl == sym) {
                env.asm_stack.resize(static_cast<size_t>(k) + 1);
                break;
              }
            }
          }
          SavedStackEntry top = std::move(env.asm_stack.back());
          env.asm_stack.pop_back();
          if (sym && top.root_decl == sym) {
            env.syms[sym] = top.saved_sym;
          }
          if (phys && *phys >= 0 && top.phys_reg == *phys) {
            env.phys[*phys] = top.saved_phys;
          }
        }
      }
    }

    // 11. Process implicit physical register CLOBBERS and OUTS
    if (sum.clobbers_caller_saved) {
      env.clobbered_caller_saved = true;
      std::string reason =
          "clobbered by caller-saved call '" + callee_name + "'";
      bool ret0_was_initial =
          (env.phys[arch_.ret_reg0].status == RegStatus::kInitial);
      bool is_c_abi_call = (callee_name.find("CallC") != std::string::npos);
      PhysRegState saved_c_callee_phys[kMaxGprs];
      if (is_c_abi_call) {
        for (int r = 0; r < arch_.num_gprs; ++r) {
          saved_c_callee_phys[r] = env.phys[r];
        }
      }
      std::map<const clang::ValueDecl*, SymRegState> saved_c_callee_syms;
      for (const auto& [decl, s] : env.syms) {
        if (s.phys_reg >= 0 &&
            (arch_.c_callee_saved_mask & (1u << s.phys_reg)) &&
            s.scratch_slot < 0) {
          saved_c_callee_syms[decl] = s;
        }
      }
      for (unsigned i = 0; i < num_args; ++i) {
        if (sum.param_roles[i] == ParamRole::kOut ||
            sum.param_roles[i] == ParamRole::kInOut ||
            (i < 32 && (sum.preserved_across_call_param_mask & (1u << i)))) {
          if (const clang::ValueDecl* out_d =
                  ResolveSymDecl(ce->getArg(i), env)) {
            if (env.syms.count(out_d)) {
              SymRegState kept = env.syms[out_d];
              kept.status = RegStatus::kAlive;
              saved_c_callee_syms[out_d] = kept;
            }
          }
        }
      }
      for (int r = 0; r < arch_.num_gprs; ++r) {
        if (arch_.caller_saved_mask & (1u << r)) {
          WriteOrClobberPhys(r, /*is_clobber=*/true, /*from_sym_param=*/false,
                             call_loc, reason, env, fctx);
        }
      }
      if (is_c_abi_call) {
        for (int r = 0; r < arch_.num_gprs; ++r) {
          if (arch_.c_callee_saved_mask & (1u << r)) {
            env.phys[r] = saved_c_callee_phys[r];
          }
        }
      }
      for (const auto& [decl, saved_s] : saved_c_callee_syms) {
        env.syms[decl] = saved_s;
      }
      if (ret0_was_initial || callee_name == "CallBuiltin" || is_c_abi_call) {
        env.phys[arch_.ret_reg0].status = RegStatus::kAlive;
      }
      if (callee_name == "CallBuiltin" || is_c_abi_call) {
        env.phys[arch_.ret_reg1].status = RegStatus::kAlive;
      }
      for (auto& [decl, s] : env.syms) {
        if (IsOperandType(decl->getType())) continue;
        if (s.phys_reg >= 0 && (arch_.pinned_mask & (1u << s.phys_reg))) {
          continue;
        }
        if (s.phys_reg >= 0 &&
            (arch_.c_callee_saved_mask & (1u << s.phys_reg)) &&
            s.scratch_slot < 0) {
          continue;
        }
        if (s.phys_reg == arch_.ret_reg0 &&
            (ret0_was_initial || callee_name == "CallBuiltin" ||
             is_c_abi_call) &&
            s.param_index < 0) {
          s.status = RegStatus::kAlive;
          s.holds_entry_value = false;
          continue;
        }
        bool is_out_of_this_call = false;
        for (unsigned i = 0; i < num_args; ++i) {
          if ((sum.param_roles[i] == ParamRole::kOut ||
               sum.param_roles[i] == ParamRole::kInOut ||
               (i < 32 &&
                (sum.preserved_across_call_param_mask & (1u << i)))) &&
              ResolveSymDecl(ce->getArg(i), env) == decl) {
            is_out_of_this_call = true;
            break;
          }
        }
        if (is_out_of_this_call) continue;
        if (s.readonly_kind == ReadonlyKind::kExplicitAttr &&
            !IsSymSavedOnStack(decl, s.phys_reg, env)) {
          QueueWarning(fctx, call_loc,
                       "Cannot clobber read-only register '" +
                           decl->getNameAsString() + "' via '" + callee_name +
                           "' (" + s.readonly_reason + ").");
        }
        s.status = RegStatus::kClobbered;
        s.holds_entry_value = false;
        s.def_version = fctx.next_version++;
        s.clobber = {call_loc, reason};
        if (s.param_index >= 0) {
          fctx.param_clobbered_any[s.param_index] = true;
        }
      }

      // Mark registers passed via pointer/array out-parameters (`&dst` or
      // `LiftoffRegister result_regs[] = {ret, dst}`) as defined after the
      // call:
      for (const clang::Expr* arg : ce->arguments()) {
        const clang::Expr* ua = UnwrapExpr(arg);
        if (!ua) continue;
        if (const auto* uo = llvm::dyn_cast<clang::UnaryOperator>(ua)) {
          if (uo->getOpcode() == clang::UO_AddrOf) {
            if (const clang::ValueDecl* sd =
                    ResolveSymDecl(uo->getSubExpr(), env)) {
              WriteOrClobberSym(sd, /*is_clobber=*/false, call_loc,
                                "populated by '" + callee_name + "'", env,
                                fctx);
            }
          }
        } else if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(ua)) {
          const clang::ValueDecl* d = CanonValueDecl(dre->getDecl());
          auto arr_it = env.reg_arrays.find(d);
          if (arr_it != env.reg_arrays.end()) {
            for (const clang::ValueDecl* elem_sd : arr_it->second) {
              WriteOrClobberSym(elem_sd, /*is_clobber=*/false, call_loc,
                                "populated by '" + callee_name + "'", env,
                                fctx);
            }
          }
        }
      }
    } else if (sum.clobbers != 0) {
      std::string reason = "clobbered by '" + callee_name + "'";
      for (int r = 0; r < arch_.num_gprs; ++r) {
        if (sum.clobbers & (1u << r)) {
          WriteOrClobberPhys(r, /*is_clobber=*/true, /*from_sym_param=*/false,
                             call_loc, reason, env, fctx);
        }
      }
    }

    if (sum.implicit_out != 0) {
      std::string reason = "written implicitly by '" + callee_name + "'";
      for (int r = 0; r < arch_.num_gprs; ++r) {
        if (sum.implicit_out & (1u << r)) {
          WriteOrClobberPhys(r, /*is_clobber=*/false, /*from_sym_param=*/false,
                             call_loc, reason, env, fctx);
        }
      }
    }

    // 12. Dynamic scratch scope slot & physical pool clobbering / exhaustion
    if (sum.baseline_scratch_used > 0) {
      int start_slot = env.active_scratch_depth;
      int end_slot = start_slot + sum.baseline_scratch_used;
      int must_end_slot = start_slot + sum.must_baseline_scratch_used;
      fctx.used_baseline_scratch_scope = true;
      fctx.max_baseline_scratch_used =
          std::max(fctx.max_baseline_scratch_used, end_slot);
      if (sum.must_baseline_scratch_used > 0) {
        env.path_max_baseline_scratch =
            std::max(env.path_max_baseline_scratch, must_end_slot);
        if (must_end_slot > arch_.baseline_scratch_pool_size) {
          int avail =
              std::max(0, arch_.baseline_scratch_pool_size - start_slot);
          QueueWarning(fctx, call_loc,
                       "Call to '" + callee_name + "' requires " +
                           std::to_string(sum.must_baseline_scratch_used) +
                           " scratch register(s), exhausting "
                           "BaselineAssembler::ScratchRegisterScope pool (" +
                           std::to_string(avail) + " available).");
        }
      }
      std::string reason = "clobbered by internal scratch register scope in '" +
                           callee_name + "'";
      int limit_slot = std::min(end_slot, arch_.baseline_scratch_pool_size);
      for (int sl = std::max(0, start_slot); sl < limit_slot; ++sl) {
        WriteOrClobberPhys(arch_.baseline_scratch_regs[sl], /*is_clobber=*/true,
                           /*from_sym_param=*/true, call_loc, reason, env,
                           fctx);
      }
      for (auto& [decl, s] : env.syms) {
        if (s.scratch_slot >= start_slot && s.scratch_slot < end_slot) {
          s.status = RegStatus::kClobbered;
          s.def_version = fctx.next_version++;
          s.clobber = {call_loc, reason};
        }
      }
    }
    if (sum.arm64_scratch_used > 0) {
      int avail_cnt = llvm::popcount(env.arm64_avail_scratch_mask);
      fctx.used_arm64_scratch_scope = true;
      int inc_avail = llvm::popcount(env.arm64_avail_scratch_mask &
                                     env.arm64_included_scratch_mask);
      int from_caller = std::max(0, sum.arm64_scratch_used - inc_avail);
      fctx.max_arm64_scratch_used =
          std::max(fctx.max_arm64_scratch_used,
                   env.arm64_caller_scratches_held + from_caller);
      if (sum.must_arm64_scratch_used > 0) {
        int must_from_caller =
            std::max(0, sum.must_arm64_scratch_used - inc_avail);
        env.path_max_arm64_scratch =
            std::max(env.path_max_arm64_scratch,
                     env.arm64_caller_scratches_held + must_from_caller);
        if (avail_cnt < sum.must_arm64_scratch_used) {
          QueueWarning(fctx, call_loc,
                       "Call to '" + callee_name + "' requires " +
                           std::to_string(sum.must_arm64_scratch_used) +
                           " scratch register(s), exhausting "
                           "UseScratchRegisterScope pool (" +
                           std::to_string(avail_cnt) + " available).");
        }
      }
      std::string reason = "clobbered by internal scratch register scope in '" +
                           callee_name + "'";
      RegMask rem = env.arm64_avail_scratch_mask;
      for (int k = 0; k < sum.arm64_scratch_used && rem != 0; ++k) {
        int r = llvm::countr_zero(rem);
        rem &= ~(1u << r);
        WriteOrClobberPhys(r, /*is_clobber=*/true, /*from_sym_param=*/true,
                           call_loc, reason, env, fctx);
      }
    }

    // 13. Control-flow annotations: V8_ASM_BRANCH (forward + backward),
    // V8_ASM_BIND, V8_ASM_NO_FALLTHROUGH
    bool is_unconditional_arm64_b = false;
    if (callee_name == "B" && ce->getNumArgs() >= 2) {
      const clang::Expr* type_arg = UnwrapExpr(ce->getArg(1));
      if (const auto* dre =
              llvm::dyn_cast_or_null<clang::DeclRefExpr>(type_arg)) {
        if (SafeDeclName(dre->getDecl()) == "always") {
          is_unconditional_arm64_b = true;
        }
      }
    }

    for (unsigned i = 0; i < num_args; ++i) {
      ParamRole role = sum.param_roles[i];
      if (role == ParamRole::kBranch) {
        if (llvm::isa<clang::CXXDefaultArgExpr>(ce->getArg(i))) continue;
        if (const clang::ValueDecl* lbl = ResolveLabelDecl(ce->getArg(i))) {
          if (fctx.bound_labels.contains(lbl)) {
            auto it = fctx.backward_label_states.find(lbl);
            if (it == fctx.backward_label_states.end()) {
              fctx.backward_label_states[lbl] = env;
            } else {
              it->second = MergeEnvs(it->second, env, arch_.num_gprs);
            }
          } else {
            auto it = fctx.forward_label_states.find(lbl);
            if (it == fctx.forward_label_states.end()) {
              fctx.forward_label_states[lbl] = env;
            } else {
              it->second = MergeEnvs(it->second, env, arch_.num_gprs);
            }
          }
        }
      } else if (role == ParamRole::kBind) {
        if (llvm::isa<clang::CXXDefaultArgExpr>(ce->getArg(i))) continue;
        if (const clang::ValueDecl* lbl = ResolveLabelDecl(ce->getArg(i))) {
          fctx.bound_labels.insert(lbl);
          auto fwd_it = fctx.forward_label_states.find(lbl);
          if (fwd_it != fctx.forward_label_states.end()) {
            env = MergeEnvs(env, fwd_it->second, arch_.num_gprs);
          }
          if (fctx.carried_backedges) {
            auto back_it = fctx.carried_backedges->find(lbl);
            if (back_it != fctx.carried_backedges->end()) {
              env = MergeEnvs(env, back_it->second, arch_.num_gprs);
            }
          }
        }
      }
    }

    if (sum.no_fallthrough || is_unconditional_arm64_b) {
      env.reachable = false;
    }

    // 14. If this call returns an Operand/MemOperand, snapshot its scratch
    // register versions right now before subsequent expressions execute!
    if (IsOperandType(ce->getType())) {
      fctx.call_operand_deps[ce] =
          BuildCallReturnedOperandDep(ce, callee, sum, env, fctx);
    }
  }

  void CheckCallSiteAliasing(const clang::CallExpr* ce,
                             const clang::FunctionDecl* callee,
                             const FuncSummary& sum, unsigned num_args,
                             Env& env, FunctionAnalysisContext& fctx) {
    std::string callee_name = callee->getNameAsString();

    // 1. Check parameter-pair early clobbers: (out_idx, in_idx)
    for (const auto& [out_idx, in_idx] : sum.early_clobber_param_pairs) {
      if (out_idx >= num_args || in_idx >= num_args) continue;
      const clang::Expr* out_arg = ce->getArg(out_idx);
      const clang::Expr* in_arg = ce->getArg(in_idx);
      if (llvm::isa<clang::CXXDefaultArgExpr>(out_arg) ||
          llvm::isa<clang::CXXDefaultArgExpr>(in_arg)) {
        continue;
      }
      const clang::Expr* u_out = UnwrapExpr(out_arg);
      const clang::Expr* u_in = UnwrapExpr(in_arg);
      if (!u_out || !u_in || !IsTrackableRegType(u_out->getType())) continue;

      const clang::ValueDecl* out_sym = ResolveSymDecl(u_out, env);
      std::optional<int> out_phys = ResolvePhysReg(out_arg, env);
      if (out_sym && env.known_no_reg.contains(out_sym)) continue;
      if (out_phys && *out_phys < 0) continue;

      auto aliases_out = [&](const clang::ValueDecl* in_s,
                             std::optional<int> in_p) {
        if (out_sym && in_s && out_sym == in_s) return true;
        if (out_phys && in_p && *out_phys >= 0 && *out_phys == *in_p) {
          return true;
        }
        return false;
      };

      bool aliased = false;
      if (IsOperandType(u_in->getType())) {
        OperandDep dep = ExtractOperandDep(u_in, env, fctx);
        for (const clang::ValueDecl* sd : dep.sym_decls) {
          if (aliases_out(sd, std::nullopt)) aliased = true;
        }
        if (out_phys && *out_phys >= 0 && (dep.phys_mask & (1u << *out_phys))) {
          aliased = true;
        }
      } else if (IsTrackableRegType(u_in->getType())) {
        aliased =
            aliases_out(ResolveSymDecl(u_in, env), ResolvePhysReg(in_arg, env));
      }

      if (aliased) {
        std::string out_param_name =
            callee->getParamDecl(out_idx)->getNameAsString();
        std::string in_param_name =
            callee->getParamDecl(in_idx)->getNameAsString();
        ClobberNote note{callee->getLocation(),
                         "'" + callee_name + "' overwrites '" + out_param_name +
                             "' before reading '" + in_param_name + "'"};
        QueueWarning(fctx, ce->getExprLoc(),
                     "Register passed as '" + out_param_name + "' to '" +
                         callee_name + "' aliases input '" + in_param_name +
                         "' and is clobbered before the input is read.",
                     &note);
      }
    }

    // 2. Check distinct SCRATCH parameters aliasing each other at call site
    for (unsigned i = 0; i < num_args; ++i) {
      if (sum.param_roles[i] != ParamRole::kScratch) {
        continue;
      }
      const clang::Expr* arg_i = ce->getArg(i);
      if (llvm::isa<clang::CXXDefaultArgExpr>(arg_i)) continue;
      const clang::Expr* u_i = UnwrapExpr(arg_i);
      if (!u_i || !IsTrackableRegType(u_i->getType())) continue;
      const clang::ValueDecl* sym_i = ResolveSymDecl(u_i, env);
      std::optional<int> phys_i = ResolvePhysReg(arg_i, env);
      if (sym_i && env.known_no_reg.contains(sym_i)) continue;
      if (phys_i && *phys_i < 0) continue;

      for (unsigned j = i + 1; j < num_args; ++j) {
        if (sum.param_roles[j] != ParamRole::kScratch) {
          continue;
        }
        const clang::Expr* arg_j = ce->getArg(j);
        if (llvm::isa<clang::CXXDefaultArgExpr>(arg_j)) continue;
        const clang::Expr* u_j = UnwrapExpr(arg_j);
        if (!u_j || !IsTrackableRegType(u_j->getType())) continue;
        const clang::ValueDecl* sym_j = ResolveSymDecl(u_j, env);
        std::optional<int> phys_j = ResolvePhysReg(arg_j, env);
        if ((sym_i && sym_i == sym_j) ||
            (phys_i && phys_j && *phys_i >= 0 && *phys_i == *phys_j)) {
          // Allow xzr/wzr (pinned zero register) to appear multiple times on
          // ARM64 (e.g. Pop(xzr, padreg)).
          if (phys_i && *phys_i >= 0 && (arch_.pinned_mask & (1u << *phys_i))) {
            continue;
          }
          QueueWarning(
              fctx, ce->getExprLoc(),
              "Distinct output/scratch parameters '" +
                  callee->getParamDecl(i)->getNameAsString() + "' and '" +
                  callee->getParamDecl(j)->getNameAsString() + "' of '" +
                  callee_name + "' are passed the same register.");
        }
      }
    }

    // 3. Check physical registers overwritten by callee before reading input
    for (unsigned i = 0;
         i < num_args && i < sum.early_clobber_phys_before_param.size(); ++i) {
      RegMask early_phys = sum.early_clobber_phys_before_param[i];
      if (!early_phys) continue;
      const clang::Expr* arg_i = ce->getArg(i);
      if (llvm::isa<clang::CXXDefaultArgExpr>(arg_i)) continue;
      if (auto p = ResolvePhysReg(arg_i, env)) {
        if (*p >= 0 && *p < arch_.num_gprs && (early_phys & (1u << *p))) {
          ClobberNote note{callee->getLocation(),
                           "'" + callee_name + "' clobbers '" +
                               arch_.gpr_names[*p] + "' before reading '" +
                               callee->getParamDecl(i)->getNameAsString() +
                               "'"};
          QueueWarning(
              fctx, ce->getExprLoc(),
              std::string("Physical register '") + arch_.gpr_names[*p] +
                  "' passed as '" + callee->getParamDecl(i)->getNameAsString() +
                  "' to '" + callee_name +
                  "' is clobbered internally before the parameter is read.",
              &note);
        }
      }
    }
  }

  void PromoteEqualOutInBranch(const clang::ValueDecl* sym_l,
                               const clang::ValueDecl* sym_r, Env& eq_branch,
                               FunctionAnalysisContext& fctx) {
    auto promote_eq_out = [&](const clang::ValueDecl* out_d,
                              const clang::ValueDecl* in_d) {
      if (!eq_branch.syms.count(out_d) || !eq_branch.syms.count(in_d)) return;
      SymRegState& out_s = eq_branch.syms[out_d];
      SymRegState& in_s = eq_branch.syms[in_d];
      if (out_s.param_index >= 0 && in_s.param_index >= 0 &&
          static_cast<unsigned>(out_s.param_index) <
              fctx.explicit_sum.param_roles.size() &&
          fctx.explicit_sum.param_roles[out_s.param_index] == ParamRole::kOut &&
          in_s.status != RegStatus::kClobbered) {
        fctx.param_read_entry[in_s.param_index] = true;
        fctx.param_written_any[out_s.param_index] = true;
        out_s.status = RegStatus::kAlive;
        out_s.holds_entry_value = false;
      }
    };
    promote_eq_out(sym_l, sym_r);
    promote_eq_out(sym_r, sym_l);
  }

  void RefineConditionEnv(const clang::Expr* cond, Env& then_env, Env& else_env,
                          FunctionAnalysisContext& fctx) {
    cond = UnwrapExpr(cond);
    if (!cond) return;

    if (const auto* uo = llvm::dyn_cast<clang::UnaryOperator>(cond)) {
      if (uo->getOpcode() == clang::UO_LNot) {
        RefineConditionEnv(uo->getSubExpr(), else_env, then_env, fctx);
        return;
      }
    }

    if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(cond)) {
      if (bo->getOpcode() == clang::BO_LAnd) {
        Env dummy_else1 = else_env;
        Env dummy_else2 = else_env;
        RefineConditionEnv(bo->getLHS(), then_env, dummy_else1, fctx);
        RefineConditionEnv(bo->getRHS(), then_env, dummy_else2, fctx);
        // For `!(a == b && c == d)` or else of `&&`: if only one condition can
        // fail on a 1-conjunct check, handled above; for `A && B`, `then_env`
        // accumulates both refinements!
        return;
      } else if (bo->getOpcode() == clang::BO_LOr) {
        Env then1 = then_env;
        Env then2 = then_env;
        RefineConditionEnv(bo->getLHS(), then1, else_env, fctx);
        RefineConditionEnv(bo->getRHS(), then2, else_env, fctx);

        // Intersect non-aliasing guarantees across `A || B` (e.g.
        // `if (offset.IsImmediate() || !offset.reg().Aliases(dst_object))`):
        auto non_aliasing_in = [](const Env& e, const clang::ValueDecl* x,
                                  const clang::ValueDecl* y) {
          return e.known_no_reg.contains(x) || e.known_no_reg.contains(y) ||
                 e.known_not_equal.contains({x, y}) ||
                 e.known_not_equal.contains({y, x});
        };
        for (const auto& [d1, s1] : then_env.syms) {
          (void)s1;
          if (then1.known_no_reg.contains(d1) &&
              then2.known_no_reg.contains(d1)) {
            then_env.known_no_reg.insert(d1);
          }
          for (const auto& [d2, s2] : then_env.syms) {
            (void)s2;
            if (d1 >= d2) continue;
            if (non_aliasing_in(then1, d1, d2) &&
                non_aliasing_in(then2, d1, d2)) {
              then_env.known_not_equal.insert({d1, d2});
            } else {
              // Propagate ARM64 size-sensitive `!=` if one side is immediate /
              // non-aliasing and the other side used size-sensitive `!=`!
              auto ss1 = then1.size_sensitive_not_equal.find({d1, d2});
              auto ss2 = then2.size_sensitive_not_equal.find({d1, d2});
              if (ss1 != then1.size_sensitive_not_equal.end() &&
                  (non_aliasing_in(then2, d1, d2) ||
                   ss2 != then2.size_sensitive_not_equal.end())) {
                then_env.size_sensitive_not_equal[{d1, d2}] = ss1->second;
                then_env.size_sensitive_not_equal[{d2, d1}] = ss1->second;
              } else if (ss2 != then2.size_sensitive_not_equal.end() &&
                         non_aliasing_in(then1, d1, d2)) {
                then_env.size_sensitive_not_equal[{d1, d2}] = ss2->second;
                then_env.size_sensitive_not_equal[{d2, d1}] = ss2->second;
              }
            }
          }
        }
        return;
      }
    }

    // Handle `AreAliased(r0, r1, ...)` free function calls:
    if (const auto* ce = llvm::dyn_cast<clang::CallExpr>(cond)) {
      if (!llvm::isa<clang::CXXMemberCallExpr>(cond) &&
          !llvm::isa<clang::CXXOperatorCallExpr>(cond)) {
        if (const clang::FunctionDecl* fd = ce->getDirectCallee()) {
          if (fd->getIdentifier() && fd->getName() == "AreAliased") {
            llvm::SmallVector<const clang::ValueDecl*, 4> syms;
            for (const clang::Expr* a : ce->arguments()) {
              if (const clang::ValueDecl* sd = ResolveSymDecl(a, then_env)) {
                syms.push_back(sd);
              }
            }
            for (size_t i = 0; i < syms.size(); ++i) {
              for (size_t j = i + 1; j < syms.size(); ++j) {
                if (syms[i] != syms[j]) {
                  else_env.known_not_equal.insert({syms[i], syms[j]});
                }
              }
            }
            if (syms.size() == 2 && syms[0] != syms[1]) {
              then_env.known_equal.insert({syms[0], syms[1]});
              PromoteEqualOutInBranch(syms[0], syms[1], then_env, fctx);
            }
            return;
          }
        }
      }
    }

    // Handle member predicates (`reg.is_valid()`, `reg.Aliases(other)`,
    // `op.IsImmediate()`):
    if (const auto* mce = llvm::dyn_cast<clang::CXXMemberCallExpr>(cond)) {
      if (const auto* md = mce->getMethodDecl()) {
        llvm::StringRef mname = SafeDeclName(md);
        if (mname == "is_valid" && mce->getNumArgs() == 0) {
          if (const clang::ValueDecl* sym =
                  ResolveSymDecl(mce->getImplicitObjectArgument(), then_env)) {
            else_env.known_no_reg.insert(sym);
          }
          return;
        }
        if (mname == "IsImmediate" && mce->getNumArgs() == 0) {
          if (const clang::ValueDecl* sym =
                  ResolveSymDecl(mce->getImplicitObjectArgument(), then_env)) {
            then_env.known_no_reg.insert(sym);
          }
          return;
        }
        if (mname == "Aliases" && mce->getNumArgs() == 1) {
          const clang::ValueDecl* sym_l =
              ResolveSymDecl(mce->getImplicitObjectArgument(), then_env);
          const clang::ValueDecl* sym_r =
              ResolveSymDecl(mce->getArg(0), then_env);
          if (sym_l && sym_r) {
            then_env.known_equal.insert({sym_l, sym_r});
            else_env.known_not_equal.insert({sym_l, sym_r});
            PromoteEqualOutInBranch(sym_l, sym_r, then_env, fctx);
          }
          return;
        }
      }
    }

    bool is_eq = false;
    bool is_ne = false;
    const clang::Expr* lhs = nullptr;
    const clang::Expr* rhs = nullptr;

    if (const auto* oce = llvm::dyn_cast<clang::CXXOperatorCallExpr>(cond)) {
      if (oce->getNumArgs() == 2) {
        if (oce->getOperator() == clang::OO_EqualEqual) {
          is_eq = true;
          lhs = oce->getArg(0);
          rhs = oce->getArg(1);
        } else if (oce->getOperator() == clang::OO_ExclaimEqual) {
          is_ne = true;
          lhs = oce->getArg(0);
          rhs = oce->getArg(1);
        }
      }
    } else if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(cond)) {
      if (bo->getOpcode() == clang::BO_EQ) {
        is_eq = true;
        lhs = bo->getLHS();
        rhs = bo->getRHS();
      } else if (bo->getOpcode() == clang::BO_NE) {
        is_ne = true;
        lhs = bo->getLHS();
        rhs = bo->getRHS();
      }
    }

    if (!is_eq && !is_ne) return;

    const clang::ValueDecl* sym_l = ResolveSymDecl(lhs, then_env);
    const clang::ValueDecl* sym_r = ResolveSymDecl(rhs, then_env);
    std::optional<int> phys_l = ResolvePhysReg(lhs, then_env);
    std::optional<int> phys_r = ResolvePhysReg(rhs, then_env);

    Env& eq_branch = is_eq ? then_env : else_env;
    Env& ne_branch = is_eq ? else_env : then_env;

    if (sym_l && phys_r && *phys_r == -1) {
      eq_branch.known_no_reg.insert(sym_l);
    } else if (sym_r && phys_l && *phys_l == -1) {
      eq_branch.known_no_reg.insert(sym_r);
    } else if (sym_l && sym_r) {
      eq_branch.known_equal.insert({sym_l, sym_r});
      if (arch_.kind == ArchKind::kArm64) {
        // On ARM64, CPURegister::operator== compares `reg_size_`, so `wN != xN`
        // is true despite aliasing! Record in `size_sensitive_not_equal` rather
        // than `known_not_equal`.
        ne_branch.size_sensitive_not_equal[{sym_l, sym_r}] = cond->getExprLoc();
        ne_branch.size_sensitive_not_equal[{sym_r, sym_l}] = cond->getExprLoc();
      } else {
        ne_branch.known_not_equal.insert({sym_l, sym_r});
      }
      PromoteEqualOutInBranch(sym_l, sym_r, eq_branch, fctx);
    } else if (sym_l && phys_r && *phys_r >= 0) {
      if (eq_branch.syms.count(sym_l)) {
        eq_branch.syms[sym_l].phys_reg = *phys_r;
        if (*phys_r < arch_.num_gprs) {
          if (eq_branch.phys[*phys_r].status == RegStatus::kAlive) {
            eq_branch.syms[sym_l].status = RegStatus::kAlive;
          } else if (eq_branch.syms[sym_l].status == RegStatus::kAlive) {
            eq_branch.phys[*phys_r].status = RegStatus::kAlive;
          }
        }
      }
    } else if (sym_r && phys_l && *phys_l >= 0) {
      if (eq_branch.syms.count(sym_r)) {
        eq_branch.syms[sym_r].phys_reg = *phys_l;
        if (*phys_l < arch_.num_gprs) {
          if (eq_branch.phys[*phys_l].status == RegStatus::kAlive) {
            eq_branch.syms[sym_r].status = RegStatus::kAlive;
          } else if (eq_branch.syms[sym_r].status == RegStatus::kAlive) {
            eq_branch.phys[*phys_l].status = RegStatus::kAlive;
          }
        }
      }
    }
  }

  void BindVariableInit(const clang::VarDecl* vd, const clang::Expr* raw_init,
                        Env& env, FunctionAnalysisContext& fctx) {
    const clang::ValueDecl* canon_vd = vd->getCanonicalDecl();
    const clang::Expr* init = UnwrapExpr(raw_init);

    ScratchScopeKind scope_kind = GetScratchScopeKind(vd->getType());
    if (scope_kind != ScratchScopeKind::kNone) {
      ScratchScopeFrame frame;
      frame.base_depth = env.active_scratch_depth;
      frame.kind = scope_kind;
      frame.saved_arm64_avail = env.arm64_avail_scratch_mask;
      frame.saved_arm64_included = env.arm64_included_scratch_mask;
      frame.saved_arm64_caller_held = env.arm64_caller_scratches_held;
      env.scratch_scope_stack.push_back(frame);
      return;
    }

    if (IsOperandType(vd->getType())) {
      if (raw_init) {
        VisitStmt(raw_init, env, fctx);
        OperandDep dep = ExtractOperandDep(raw_init, env, fctx);
        env.operands[canon_vd] = dep;
      }
      return;
    }

    if (const auto* arr_ty = ast_ctx_.getAsArrayType(vd->getType())) {
      if (IsTrackableRegType(arr_ty->getElementType()) && raw_init) {
        VisitStmt(raw_init, env, fctx);
        if (const auto* ile = llvm::dyn_cast<clang::InitListExpr>(init)) {
          std::vector<const clang::ValueDecl*> elems;
          for (const clang::Expr* e : ile->inits()) {
            if (const clang::ValueDecl* sd = ResolveSymDecl(e, env)) {
              elems.push_back(sd);
            }
          }
          if (!elems.empty()) {
            env.reg_arrays[canon_vd] = std::move(elems);
          }
        }
        return;
      }
    }

    if (!IsTrackableRegType(vd->getType())) {
      if (raw_init) VisitStmt(raw_init, env, fctx);
      return;
    }

    env.known_no_reg.erase(canon_vd);

    if (!raw_init) {
      SymRegState s;
      s.status = RegStatus::kInitial;
      s.root_decl = canon_vd;
      s.def_version = fctx.next_version++;
      env.syms[canon_vd] = s;
      return;
    }

    // Check if initialized from a scratch scope (`AcquireScratch`, `AcquireX`,
    // `AcquireW`, `AcquireSameSizeAs`, `Acquire`).
    ScratchScopeKind acq_kind = MatchScratchAcquireCall(raw_init);
    if (acq_kind != ScratchScopeKind::kNone) {
      int slot = env.active_scratch_depth++;
      int phys = -1;
      if (acq_kind == ScratchScopeKind::kBaseline) {
        fctx.used_baseline_scratch_scope = true;
        fctx.max_baseline_scratch_used =
            std::max(fctx.max_baseline_scratch_used, env.active_scratch_depth);
        env.path_max_baseline_scratch =
            std::max(env.path_max_baseline_scratch, env.active_scratch_depth);
        if (slot < arch_.baseline_scratch_pool_size) {
          phys = arch_.baseline_scratch_regs[slot];
        } else {
          QueueWarning(fctx, raw_init->getExprLoc(),
                       "ScratchRegisterScope acquisition exceeds pool size (" +
                           std::to_string(arch_.baseline_scratch_pool_size) +
                           ").");
        }
      } else if (acq_kind == ScratchScopeKind::kArm64UseScratch) {
        fctx.used_arm64_scratch_scope = true;
        if (env.arm64_avail_scratch_mask != 0) {
          phys = llvm::countr_zero(env.arm64_avail_scratch_mask);
          env.arm64_avail_scratch_mask &= ~(1u << phys);
          if (!(env.arm64_included_scratch_mask & (1u << phys))) {
            env.arm64_caller_scratches_held++;
            fctx.max_arm64_scratch_used = std::max(
                fctx.max_arm64_scratch_used, env.arm64_caller_scratches_held);
            env.path_max_arm64_scratch = std::max(
                env.path_max_arm64_scratch, env.arm64_caller_scratches_held);
          }
        } else {
          QueueWarning(
              fctx, raw_init->getExprLoc(),
              "UseScratchRegisterScope acquisition exhausts available scratch "
              "register pool.");
        }
      }

      SymRegState s;
      s.status = RegStatus::kAlive;
      s.root_decl = canon_vd;
      s.phys_reg = phys;
      s.scratch_slot = slot;
      s.scratch_scope_id = static_cast<int>(env.scratch_scope_stack.size());
      s.def_version = fctx.next_version++;
      env.syms[canon_vd] = s;

      if (phys >= 0 && phys < arch_.num_gprs) {
        WriteOrClobberPhys(phys, /*is_clobber=*/true, /*from_sym_param=*/true,
                           raw_init->getExprLoc(),
                           "acquired by scratch register scope", env, fctx,
                           /*skip_sym=*/canon_vd);
      }

      for (auto& [other_d, other_s] : env.syms) {
        if (other_d != canon_vd && other_s.scratch_slot == slot) {
          other_s.status = RegStatus::kClobbered;
          other_s.def_version = fctx.next_version++;
          std::string reg_desc = (phys >= 0)
                                     ? std::string(arch_.gpr_names[phys])
                                     : "scratch slot " + std::to_string(slot);
          other_s.clobber = {
              raw_init->getExprLoc(),
              "clobbered when scratch scope re-allocated " + reg_desc};
        }
      }
      if (phys >= 0 && phys < arch_.num_gprs) {
        env.phys[phys].status = RegStatus::kAlive;
        env.phys[phys].def_version = fctx.next_version++;
      }
      return;
    }

    VisitStmt(raw_init, env, fctx);

    if (const clang::ValueDecl* rhs_sym = ResolveSymDecl(init, env)) {
      auto it = env.syms.find(rhs_sym);
      if (it != env.syms.end()) {
        env.syms[canon_vd] = it->second;
        return;
      }
    }
    if (const auto* cond_op =
            llvm::dyn_cast_or_null<clang::ConditionalOperator>(init)) {
      const clang::ValueDecl* st = ResolveSymDecl(cond_op->getTrueExpr(), env);
      const clang::ValueDecl* sf = ResolveSymDecl(cond_op->getFalseExpr(), env);
      std::optional<int> pt = ResolvePhysReg(cond_op->getTrueExpr(), env);
      std::optional<int> pf = ResolvePhysReg(cond_op->getFalseExpr(), env);
      auto is_pinned_or_noreg = [&](std::optional<int> p) {
        return p && (*p == -1 || (*p >= 0 && (arch_.pinned_mask & (1u << *p))));
      };
      if (st && !sf && is_pinned_or_noreg(pf) && env.syms.count(st)) {
        env.syms[canon_vd] = env.syms[st];
        return;
      }
      if (sf && !st && is_pinned_or_noreg(pt) && env.syms.count(sf)) {
        env.syms[canon_vd] = env.syms[sf];
        return;
      }
    }

    SymRegState s;
    s.status = RegStatus::kAlive;
    s.root_decl = canon_vd;
    s.def_version = fctx.next_version++;

    // Check Maglev `ToRegister(Input())` provenance:
    if (fctx.maglev_node_cls) {
      std::string maglev_reason;
      if (MatchMaglevReadonlyToRegister(raw_init, fctx.maglev_node_cls,
                                        &maglev_reason)) {
        s.readonly_kind = ReadonlyKind::kMaglevInput;
        s.readonly_reason = std::move(maglev_reason);
      }
    }

    if (auto p = ResolvePhysReg(raw_init, env)) {
      s.phys_reg = *p;
      if (*p == -1) {
        env.known_no_reg.insert(canon_vd);
      } else if (*p >= 0 && *p < arch_.num_gprs) {
        s.status = env.phys[*p].status;
        s.clobber = env.phys[*p].clobber;
      }
    }
    env.syms[canon_vd] = s;
  }

  void PopScratchScopesToDepth(size_t target_depth,
                               clang::SourceLocation end_loc, Env& env,
                               FunctionAnalysisContext& fctx) {
    while (env.scratch_scope_stack.size() > target_depth) {
      ScratchScopeFrame frame = env.scratch_scope_stack.back();
      env.scratch_scope_stack.pop_back();
      env.active_scratch_depth = frame.base_depth;
      if (frame.kind == ScratchScopeKind::kArm64UseScratch) {
        env.arm64_avail_scratch_mask = frame.saved_arm64_avail;
        env.arm64_included_scratch_mask = frame.saved_arm64_included;
        env.arm64_caller_scratches_held = frame.saved_arm64_caller_held;
      }
      for (auto& [decl, s] : env.syms) {
        if (s.scratch_slot >= frame.base_depth) {
          s.status = RegStatus::kClobbered;
          s.def_version = fctx.next_version++;
          s.clobber = {
              end_loc,
              "scratch register scope owning this register ended here"};
        }
      }
    }
  }

  void VisitLoop(const clang::Stmt* init_stmt, const clang::VarDecl* cond_var,
                 const clang::Expr* cond_expr, const clang::Stmt* inc_stmt,
                 const clang::Stmt* body_stmt, Env& env,
                 FunctionAnalysisContext& fctx) {
    size_t saved_scope_depth = env.scratch_scope_stack.size();
    if (init_stmt) VisitStmt(init_stmt, env, fctx);
    if (!env.reachable) {
      PopScratchScopesToDepth(saved_scope_depth, body_stmt->getEndLoc(), env,
                              fctx);
      return;
    }

    CfTarget loop_cf;
    loop_cf.is_loop = true;
    loop_cf.break_env.reachable = false;
    loop_cf.continue_env.reachable = false;
    fctx.cf_stack.push_back(std::move(loop_cf));

    auto run_one_iter = [&](const Env& header_in, Env* out_latch,
                            Env* out_cond_exit) {
      Env cur = header_in;
      fctx.cf_stack.back().continue_env.reachable = false;
      if (cond_var) {
        BindVariableInit(cond_var, cond_var->getInit(), cur, fctx);
      }
      if (cond_expr) {
        VisitStmt(cond_expr, cur, fctx);
        Env body_env = cur;
        Env exit_env = cur;
        RefineConditionEnv(cond_expr, body_env, exit_env, fctx);
        *out_cond_exit = exit_env;
        cur = body_env;
      } else {
        out_cond_exit->reachable = false;
      }
      VisitStmt(body_stmt, cur, fctx);
      cur = MergeEnvs(cur, fctx.cf_stack.back().continue_env, arch_.num_gprs);
      if (inc_stmt && cur.reachable) {
        VisitStmt(inc_stmt, cur, fctx);
      }
      *out_latch = std::move(cur);
    };

    Env latch1;
    Env cond_exit1;
    run_one_iter(env, &latch1, &cond_exit1);

    Env merged_exit = cond_exit1;
    if (latch1.reachable) {
      Env header2 = MergeEnvs(env, latch1, arch_.num_gprs);
      if (!EnvEquivalentForFixpoint(env, header2, arch_.num_gprs)) {
        Env latch2;
        Env cond_exit2;
        run_one_iter(header2, &latch2, &cond_exit2);
        merged_exit = MergeEnvs(merged_exit, cond_exit2, arch_.num_gprs);
      }
    }

    merged_exit =
        MergeEnvs(merged_exit, fctx.cf_stack.back().break_env, arch_.num_gprs);
    fctx.cf_stack.pop_back();
    env = std::move(merged_exit);
    PopScratchScopesToDepth(saved_scope_depth, body_stmt->getEndLoc(), env,
                            fctx);
  }

  void VisitStmt(const clang::Stmt* stmt, Env& env,
                 FunctionAnalysisContext& fctx) {
    if (!stmt || !env.reachable) return;

    if (const auto* compound = llvm::dyn_cast<clang::CompoundStmt>(stmt)) {
      size_t saved_scope_depth = env.scratch_scope_stack.size();
      for (const clang::Stmt* child : compound->body()) {
        VisitStmt(child, env, fctx);
      }
      PopScratchScopesToDepth(saved_scope_depth, compound->getRBracLoc(), env,
                              fctx);
      return;
    }

    if (const auto* ds = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
      for (const clang::Decl* d : ds->decls()) {
        if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(d)) {
          if (vd->isLocalVarDecl() && !vd->isConstexpr()) {
            BindVariableInit(vd, vd->getInit(), env, fctx);
          }
        }
      }
      return;
    }

    if (const auto* ifs = llvm::dyn_cast<clang::IfStmt>(stmt)) {
      if (const clang::Stmt* init = ifs->getInit()) {
        VisitStmt(init, env, fctx);
      }
      if (const clang::VarDecl* cond_var = ifs->getConditionVariable()) {
        BindVariableInit(cond_var, cond_var->getInit(), env, fctx);
      }
      const clang::Expr* cond = ifs->getCond();
      VisitStmt(cond, env, fctx);

      Env then_env = env;
      Env else_env = env;
      RefineConditionEnv(cond, then_env, else_env, fctx);

      VisitStmt(ifs->getThen(), then_env, fctx);
      if (const clang::Stmt* else_stmt = ifs->getElse()) {
        VisitStmt(else_stmt, else_env, fctx);
      }
      env = MergeEnvs(then_env, else_env, arch_.num_gprs);
      return;
    }

    if (const auto* for_stmt = llvm::dyn_cast<clang::ForStmt>(stmt)) {
      VisitLoop(for_stmt->getInit(), for_stmt->getConditionVariable(),
                for_stmt->getCond(), for_stmt->getInc(), for_stmt->getBody(),
                env, fctx);
      return;
    }

    if (const auto* while_stmt = llvm::dyn_cast<clang::WhileStmt>(stmt)) {
      VisitLoop(nullptr, while_stmt->getConditionVariable(),
                while_stmt->getCond(), nullptr, while_stmt->getBody(), env,
                fctx);
      return;
    }

    if (const auto* range_stmt = llvm::dyn_cast<clang::CXXForRangeStmt>(stmt)) {
      size_t saved_scope_depth = env.scratch_scope_stack.size();
      if (range_stmt->getInit()) VisitStmt(range_stmt->getInit(), env, fctx);
      if (range_stmt->getRangeStmt()) {
        VisitStmt(range_stmt->getRangeStmt(), env, fctx);
      }
      VisitLoop(nullptr, nullptr, range_stmt->getCond(), range_stmt->getInc(),
                range_stmt->getBody(), env, fctx);
      PopScratchScopesToDepth(saved_scope_depth, range_stmt->getEndLoc(), env,
                              fctx);
      return;
    }

    if (const auto* do_stmt = llvm::dyn_cast<clang::DoStmt>(stmt)) {
      CfTarget loop_cf;
      loop_cf.is_loop = true;
      loop_cf.break_env.reachable = false;
      loop_cf.continue_env.reachable = false;
      fctx.cf_stack.push_back(std::move(loop_cf));

      auto is_const_false = [&](const clang::Expr* e) {
        if (!e || e->isValueDependent()) return false;
        bool bval = false;
        return e->EvaluateAsBooleanCondition(bval, ast_ctx_) && !bval;
      };

      Env entry_env = env;
      Env cur = env;
      VisitStmt(do_stmt->getBody(), cur, fctx);
      cur = MergeEnvs(cur, fctx.cf_stack.back().continue_env, arch_.num_gprs);

      Env merged_exit;
      merged_exit.reachable = false;
      if (cur.reachable) {
        const clang::Expr* cond = do_stmt->getCond();
        VisitStmt(cond, cur, fctx);
        Env back_env = cur;
        Env exit_env = cur;
        RefineConditionEnv(cond, back_env, exit_env, fctx);
        merged_exit = exit_env;

        if (!is_const_false(cond) && back_env.reachable) {
          Env header2 = MergeEnvs(entry_env, back_env, arch_.num_gprs);
          if (!EnvEquivalentForFixpoint(entry_env, header2, arch_.num_gprs)) {
            fctx.cf_stack.back().continue_env.reachable = false;
            Env cur2 = header2;
            VisitStmt(do_stmt->getBody(), cur2, fctx);
            cur2 = MergeEnvs(cur2, fctx.cf_stack.back().continue_env,
                             arch_.num_gprs);
            if (cur2.reachable) {
              VisitStmt(cond, cur2, fctx);
              Env back2 = cur2;
              Env exit2 = cur2;
              RefineConditionEnv(cond, back2, exit2, fctx);
              merged_exit = MergeEnvs(merged_exit, exit2, arch_.num_gprs);
            }
          }
        }
      }

      merged_exit = MergeEnvs(merged_exit, fctx.cf_stack.back().break_env,
                              arch_.num_gprs);
      fctx.cf_stack.pop_back();
      env = std::move(merged_exit);
      return;
    }

    if (llvm::isa<clang::BreakStmt>(stmt)) {
      if (!fctx.cf_stack.empty()) {
        fctx.cf_stack.back().break_env =
            MergeEnvs(fctx.cf_stack.back().break_env, env, arch_.num_gprs);
      }
      env.reachable = false;
      return;
    }

    if (llvm::isa<clang::ContinueStmt>(stmt)) {
      for (auto it = fctx.cf_stack.rbegin(); it != fctx.cf_stack.rend(); ++it) {
        if (it->is_loop) {
          it->continue_env = MergeEnvs(it->continue_env, env, arch_.num_gprs);
          break;
        }
      }
      env.reachable = false;
      return;
    }

    if (const auto* sw = llvm::dyn_cast<clang::SwitchStmt>(stmt)) {
      if (const clang::Stmt* init = sw->getInit()) {
        VisitStmt(init, env, fctx);
      }
      VisitStmt(sw->getCond(), env, fctx);
      Env entry_env = env;
      bool has_default = false;

      CfTarget sw_cf;
      sw_cf.is_loop = false;
      sw_cf.break_env.reachable = false;
      sw_cf.continue_env.reachable = false;
      fctx.cf_stack.push_back(std::move(sw_cf));

      Env fallthrough_exit;
      fallthrough_exit.reachable = false;

      if (const auto* body =
              llvm::dyn_cast_or_null<clang::CompoundStmt>(sw->getBody())) {
        Env cur_env;
        cur_env.reachable = false;
        unsigned num_cases = 0;
        for (const clang::Stmt* child : body->body()) {
          if (llvm::isa<clang::CaseStmt>(child) ||
              llvm::isa<clang::DefaultStmt>(child)) {
            if (llvm::isa<clang::DefaultStmt>(child)) has_default = true;
            if (llvm::isa<clang::CaseStmt>(child)) ++num_cases;
            cur_env = MergeEnvs(cur_env, entry_env, arch_.num_gprs);
            const clang::Stmt* sub =
                llvm::isa<clang::CaseStmt>(child)
                    ? llvm::cast<clang::CaseStmt>(child)->getSubStmt()
                    : llvm::cast<clang::DefaultStmt>(child)->getSubStmt();
            while (llvm::isa<clang::CaseStmt>(sub) ||
                   llvm::isa<clang::DefaultStmt>(sub)) {
              if (llvm::isa<clang::DefaultStmt>(sub)) has_default = true;
              if (llvm::isa<clang::CaseStmt>(sub)) ++num_cases;
              sub = llvm::isa<clang::CaseStmt>(sub)
                        ? llvm::cast<clang::CaseStmt>(sub)->getSubStmt()
                        : llvm::cast<clang::DefaultStmt>(sub)->getSubStmt();
            }
            VisitStmt(sub, cur_env, fctx);
          } else {
            VisitStmt(child, cur_env, fctx);
          }
        }
        fallthrough_exit = cur_env;
        if (!has_default && sw->getCond()) {
          if (const auto* et =
                  sw->getCond()->getType()->getAs<clang::EnumType>()) {
            unsigned enum_count = 0;
            for (const auto* e : et->getDecl()->enumerators()) {
              (void)e;
              ++enum_count;
            }
            if (enum_count > 0 && num_cases >= enum_count) {
              has_default = true;
            }
          }
        }
      } else {
        VisitStmt(sw->getBody(), env, fctx);
        fallthrough_exit = env;
      }

      Env merged_exit = MergeEnvs(
          fallthrough_exit, fctx.cf_stack.back().break_env, arch_.num_gprs);
      fctx.cf_stack.pop_back();
      if (!has_default) {
        merged_exit = MergeEnvs(merged_exit, entry_env, arch_.num_gprs);
      }
      env = std::move(merged_exit);
      return;
    }

    if (const auto* ret = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
      const clang::Expr* val = ret->getRetValue();
      if (val) {
        VisitStmt(val, env, fctx);
        if (env.reachable && IsOperandType(fctx.fn->getReturnType())) {
          RecordReturnedOperandSummary(val, env, fctx);
        }
      }
      if (env.reachable) {
        fctx.exit_envs.push_back(env);
      }
      env.reachable = false;
      return;
    }

    if (const auto* oce = llvm::dyn_cast<clang::CXXOperatorCallExpr>(stmt)) {
      if (oce->getOperator() == clang::OO_Equal && oce->getNumArgs() == 2) {
        const clang::Expr* lhs = UnwrapExpr(oce->getArg(0));
        const clang::Expr* rhs = oce->getArg(1);
        if (const auto* dre = llvm::dyn_cast_or_null<clang::DeclRefExpr>(lhs)) {
          if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl())) {
            BindVariableInit(vd, rhs, env, fctx);
            return;
          }
        }
      }
    }

    if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(stmt)) {
      if (bo->getOpcode() == clang::BO_Assign) {
        const clang::Expr* lhs = UnwrapExpr(bo->getLHS());
        const clang::Expr* rhs = bo->getRHS();
        if (const auto* dre = llvm::dyn_cast_or_null<clang::DeclRefExpr>(lhs)) {
          if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl())) {
            BindVariableInit(vd, rhs, env, fctx);
            return;
          }
        }
      }
    }

    if (const auto* ce = llvm::dyn_cast<clang::CallExpr>(stmt)) {
      if (const auto* mce = llvm::dyn_cast<clang::CXXMemberCallExpr>(ce)) {
        if (const clang::Expr* obj = mce->getImplicitObjectArgument()) {
          VisitStmt(obj, env, fctx);
        }
      }
      for (const clang::Expr* arg : ce->arguments()) {
        VisitStmt(arg, env, fctx);
      }
      ApplyCallEffect(ce, env, fctx);
      return;
    }

    for (const clang::Stmt* child : stmt->children()) {
      if (child) VisitStmt(child, env, fctx);
    }
  }

  void RecordReturnedOperandSummary(const clang::Expr* ret_expr, Env& env,
                                    FunctionAnalysisContext& fctx) {
    OperandDep dep = ExtractOperandDep(ret_expr, env, fctx);
    for (const auto& [r, ver] : dep.scratch_phys_versions) {
      (void)ver;
      if (r >= 0 && r < arch_.num_gprs) {
        fctx.returned_operand_scratch_phys_mask |= (1u << r);
      }
    }
    for (const auto& [sd, ver] : dep.scratch_sym_versions) {
      (void)ver;
      auto it = env.syms.find(sd);
      if (it != env.syms.end()) {
        if (it->second.param_index >= 0 && it->second.param_index < 32) {
          fctx.returned_operand_scratch_param_mask |=
              (1u << it->second.param_index);
        } else if (it->second.phys_reg >= 0 &&
                   it->second.phys_reg < arch_.num_gprs) {
          fctx.returned_operand_scratch_phys_mask |=
              (1u << it->second.phys_reg);
        }
      }
    }
    for (int r = 0; r < arch_.num_gprs; ++r) {
      if (dep.phys_mask & (1u << r)) {
        if (!env.phys[r].holds_entry_value) {
          fctx.returned_operand_scratch_phys_mask |= (1u << r);
        } else if (!(arch_.pinned_mask & (1u << r))) {
          fctx.returned_operand_input_phys_mask |= (1u << r);
        }
      }
    }
    for (const clang::ValueDecl* sd : dep.sym_decls) {
      auto it = env.syms.find(sd);
      if (it == env.syms.end()) continue;
      const SymRegState& s = it->second;
      if (s.param_index >= 0 && s.param_index < 32) {
        if (!s.holds_entry_value) {
          fctx.returned_operand_scratch_param_mask |= (1u << s.param_index);
        } else {
          fctx.returned_operand_input_param_mask |= (1u << s.param_index);
        }
      } else if (s.phys_reg >= 0 && s.phys_reg < arch_.num_gprs &&
                 !env.phys[s.phys_reg].holds_entry_value) {
        fctx.returned_operand_scratch_phys_mask |= (1u << s.phys_reg);
      }
    }
  }

  FuncSummary BuildInferredSummary(const FunctionAnalysisContext& fctx) {
    FuncSummary inf;
    unsigned num_params = fctx.fn->getNumParams();
    inf.param_roles.assign(num_params, ParamRole::kNone);
    inf.early_clobber_param_pairs = fctx.early_clobber_param_pairs;
    inf.early_clobber_phys_before_param = fctx.early_clobber_phys_before_param;
    inf.baseline_scratch_used = fctx.max_baseline_scratch_used;
    inf.arm64_scratch_used = fctx.max_arm64_scratch_used;
    inf.returned_operand_scratch_phys_mask =
        fctx.returned_operand_scratch_phys_mask;
    inf.returned_operand_scratch_param_mask =
        fctx.returned_operand_scratch_param_mask;
    inf.returned_operand_input_param_mask =
        fctx.returned_operand_input_param_mask;
    inf.returned_operand_input_phys_mask =
        fctx.returned_operand_input_phys_mask;

    Env merged_exit;
    merged_exit.reachable = false;
    for (const Env& e : fctx.exit_envs) {
      merged_exit = MergeEnvs(merged_exit, e, arch_.num_gprs);
    }

    if (merged_exit.reachable) {
      inf.must_baseline_scratch_used = merged_exit.path_max_baseline_scratch;
      inf.must_arm64_scratch_used = merged_exit.path_max_arm64_scratch;
    }

    inf.no_fallthrough = !merged_exit.reachable;
    inf.is_push = fctx.any_push_param0 && !fctx.any_pop_param0;
    inf.is_pop = fctx.any_pop_param0 && !fctx.any_push_param0;
    inf.clobbers_caller_saved =
        merged_exit.reachable && merged_exit.clobbered_caller_saved;

    for (unsigned i = 0; i < num_params; ++i) {
      const clang::ParmVarDecl* p = fctx.fn->getParamDecl(i);
      const clang::ValueDecl* canon_p = p->getCanonicalDecl();
      if (IsLabelPtrType(p->getType())) {
        if (fctx.bound_labels.contains(canon_p)) {
          inf.param_roles[i] = ParamRole::kBind;
        } else if (fctx.forward_label_states.count(canon_p) ||
                   fctx.backward_label_states.count(canon_p)) {
          inf.param_roles[i] = ParamRole::kBranch;
        }
        continue;
      }
      if (!IsTrackableRegType(p->getType()) && !IsOperandType(p->getType())) {
        continue;
      }
      if (IsOperandType(p->getType())) {
        inf.param_roles[i] = ParamRole::kIn;
        continue;
      }

      bool read_in = fctx.param_read_entry[i];
      bool restored_on_exit = false;
      RegStatus exit_status = RegStatus::kInitial;
      if (merged_exit.reachable && merged_exit.syms.count(canon_p)) {
        const SymRegState& es = merged_exit.syms.at(canon_p);
        exit_status = es.status;
        restored_on_exit = es.holds_entry_value;
      }

      if (restored_on_exit ||
          (!fctx.param_written_any[i] && !fctx.param_clobbered_any[i])) {
        inf.param_roles[i] = ParamRole::kIn;
        if (restored_on_exit && inf.clobbers_caller_saved && i < 32) {
          inf.preserved_across_call_param_mask |= (1u << i);
        }
      } else if (read_in) {
        inf.param_roles[i] = (exit_status == RegStatus::kClobbered)
                                 ? ParamRole::kInClobber
                                 : ParamRole::kInOut;
      } else {
        inf.param_roles[i] = (exit_status == RegStatus::kClobbered)
                                 ? ParamRole::kScratch
                                 : ParamRole::kOut;
      }
    }

    if (merged_exit.reachable) {
      for (int r = 0; r < arch_.num_gprs; ++r) {
        if (arch_.pinned_mask & (1u << r)) continue;
        if (fctx.used_baseline_scratch_scope &&
            (arch_.baseline_scratch_mask & (1u << r))) {
          continue;
        }
        if (fctx.used_arm64_scratch_scope &&
            (arch_.arm64_default_tmp_mask & (1u << r))) {
          continue;
        }
        const PhysRegState& ps = merged_exit.phys[r];
        if (!ps.holds_entry_value &&
            ((fctx.phys_written_any | fctx.phys_clobbered_any) & (1u << r))) {
          if (ps.status == RegStatus::kClobbered) {
            inf.clobbers |= (1u << r);
          } else if (ps.status == RegStatus::kAlive) {
            inf.implicit_out |= (1u << r);
          }
        }
      }
    }
    return inf;
  }

  void VerifyAnnotations(const clang::FunctionDecl* fn, const FuncSummary& exp,
                         const FuncSummary& inf) {
    const clang::CXXMethodDecl* method =
        llvm::dyn_cast<clang::CXXMethodDecl>(fn);
    bool verify_unannotated =
        method && ShouldVerifyUnannotatedRecord(method->getParent());
    if (!verify_unannotated && !exp.has_explicit_annotations) {
      return;
    }

    bool body_is_trivial = (inf.clobbers == 0 && inf.implicit_out == 0 &&
                            !inf.clobbers_caller_saved && !inf.no_fallthrough);
    if (body_is_trivial) {
      bool any_non_in = false;
      for (unsigned i = 0; i < fn->getNumParams(); ++i) {
        if (inf.param_roles[i] != ParamRole::kNone &&
            inf.param_roles[i] != ParamRole::kIn) {
          any_non_in = true;
        }
      }
      // Check if body has zero calls at all (e.g. NOOP_UNLESS_DEBUG_CODE or
      // `#ifdef` stubbed out):
      bool has_any_call = false;
      if (const clang::Stmt* body = fn->getBody()) {
        class AnyCallFinder : public clang::RecursiveASTVisitor<AnyCallFinder> {
         public:
          bool found = false;
          bool VisitCallExpr(const clang::CallExpr*) {
            found = true;
            return false;
          }
        } cf;
        cf.TraverseStmt(const_cast<clang::Stmt*>(body));
        has_any_call = cf.found;
      }
      if (!any_non_in && !has_any_call) return;
    }

    std::string fn_name = fn->getQualifiedNameAsString();

    for (unsigned i = 0; i < fn->getNumParams(); ++i) {
      const clang::ParmVarDecl* p = fn->getParamDecl(i);
      if (!IsCoreGprRegType(p->getType())) continue;

      ParamRole e_role = exp.param_roles[i];
      ParamRole i_role = inf.param_roles[i];
      std::string p_name = p->getNameAsString();

      // Skip `CPURegister` overloads when instantiated/used purely for FP regs
      // if nothing wrote the GPR.
      if (e_role == ParamRole::kIn && i_role != ParamRole::kIn &&
          !exp.clobbers_caller_saved) {
        EmitImmediateWarning(
            p->getLocation(),
            "Parameter '" + p_name + "' of '" + fn_name +
                "' is modified/clobbered in body (inferred " +
                ParamRoleToString(i_role) +
                ") but is annotated as input-only (V8_REG_IN).");
      } else if ((e_role == ParamRole::kOut || e_role == ParamRole::kScratch) &&
                 (i_role == ParamRole::kInOut ||
                  i_role == ParamRole::kInClobber)) {
        EmitImmediateWarning(
            p->getLocation(),
            "Parameter '" + p_name + "' of '" + fn_name + "' is annotated " +
                ParamRoleToString(e_role) +
                " but its entry value is read before being written (inferred " +
                ParamRoleToString(i_role) + ").");
      } else if ((e_role == ParamRole::kOut || e_role == ParamRole::kInOut) &&
                 i_role == ParamRole::kIn &&
                 !(e_role == ParamRole::kInOut && exp.clobbers_caller_saved) &&
                 !IsTypeNamed(p->getType(), "CPURegister")) {
        EmitImmediateWarning(p->getLocation(),
                             "Parameter '" + p_name + "' of '" + fn_name +
                                 "' is annotated " + ParamRoleToString(e_role) +
                                 " but is never written in the method body.");
      }
    }

    if (inf.clobbers_caller_saved && !exp.clobbers_caller_saved &&
        !inf.no_fallthrough && !exp.no_fallthrough) {
      EmitImmediateWarning(
          fn->getLocation(),
          "Method '" + fn_name +
              "' clobbers caller-saved registers in its body but is missing "
              "V8_REG_CLOBBERS_CALLER_SAVED.");
    }

    RegMask unannotated_clobbers =
        (inf.clobbers | inf.implicit_out) & ~(exp.clobbers | exp.implicit_out);
    if (exp.clobbers_caller_saved) {
      unannotated_clobbers &= ~arch_.caller_saved_mask;
    }
    if (unannotated_clobbers != 0 && !inf.no_fallthrough &&
        !exp.no_fallthrough) {
      EmitImmediateWarning(
          fn->getLocation(),
          "Method '" + fn_name + "' modifies/clobbers physical register(s) {" +
              FormatRegMask(unannotated_clobbers, arch_) +
              "} without V8_REG_CLOBBERS / V8_REG_IMPLICIT_OUT annotation.");
    }
  }

  clang::ASTContext& ast_ctx_;
  clang::DiagnosticsEngine& diag_;
  clang::SourceManager& sm_;
  ArchInfo arch_;
  unsigned diag_warn_ = 0;
  unsigned diag_note_ = 0;

  std::map<const clang::FunctionDecl*, FuncSummary> summary_cache_;
  std::map<const clang::CXXRecordDecl*, MaglevConstraints> maglev_cache_;
  llvm::SmallPtrSet<const clang::FunctionDecl*, 16> active_inferences_;
};

class RegmoleConsumer : public clang::ASTConsumer,
                        public clang::RecursiveASTVisitor<RegmoleConsumer> {
 public:
  RegmoleConsumer(clang::DiagnosticsEngine& d, clang::SourceManager& sm,
                  const std::vector<std::string>& args)
      : d_(d), sm_(sm) {
    for (const std::string& arg : args) {
      if (arg == "--no-verify-annotations") g_verify_annotations = false;
      if (arg == "--verify-annotations") g_verify_annotations = true;
      if (arg == "--no-check-uses") g_check_uses = false;
      if (arg == "--check-uses") g_check_uses = true;
      if (arg == "--check-headers") g_check_headers = true;
      if (arg == "--arch=x64") g_arch_override = ArchKind::kX64;
      if (arg == "--arch=arm64") g_arch_override = ArchKind::kArm64;
    }
  }

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    ArchInfo arch = DetectArchFromAST(ctx, g_arch_override);
    analyzer_ = std::make_unique<RegmoleAnalyzer>(ctx, d_, sm_, arch);
    TraverseDecl(ctx.getTranslationUnitDecl());
  }

  bool VisitFunctionDecl(clang::FunctionDecl* decl) {
    if (!decl->doesThisDeclarationHaveABody()) return true;
    clang::SourceLocation loc = decl->getLocation();
    if (!loc.isValid()) return true;

    if (!g_check_headers && sm_.getFileID(loc) != sm_.getMainFileID()) {
      return true;
    }
    analyzer_->AnalyzeTopLevelFunction(decl);
    return true;
  }

 private:
  clang::DiagnosticsEngine& d_;
  clang::SourceManager& sm_;
  std::unique_ptr<RegmoleAnalyzer> analyzer_;
};

class RegmoleAction : public clang::PluginASTAction {
 protected:
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance& CI, llvm::StringRef InFile) override {
    (void)InFile;
    return std::make_unique<RegmoleConsumer>(CI.getDiagnostics(),
                                             CI.getSourceManager(), args_);
  }

  bool ParseArgs(const clang::CompilerInstance& CI,
                 const std::vector<std::string>& args) override {
    (void)CI;
    args_ = args;
    return true;
  }

 private:
  std::vector<std::string> args_;
};

}  // namespace

static clang::FrontendPluginRegistry::Add<RegmoleAction> RegisterRegmole(
    "regmole", "Static register clobber and control-flow analyzer for V8");
