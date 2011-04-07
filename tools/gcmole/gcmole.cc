// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This is clang plugin used by gcmole tool. See README for more details.

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"

#include <bitset>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stack>

namespace {

typedef std::string MangledName;
typedef std::set<MangledName> CalleesSet;

static bool GetMangledName(clang::MangleContext* ctx,
                           const clang::NamedDecl* decl,
                           MangledName* result) {
  if (!isa<clang::CXXConstructorDecl>(decl) &&
      !isa<clang::CXXDestructorDecl>(decl)) {
    llvm::SmallVector<char, 512> output;
    llvm::raw_svector_ostream out(output);
    ctx->mangleName(decl, out);
    *result = out.str().str();
    return true;
  }

  return false;
}


static bool InV8Namespace(const clang::NamedDecl* decl) {
  return decl->getQualifiedNameAsString().compare(0, 4, "v8::") == 0;
}


class CalleesPrinter : public clang::RecursiveASTVisitor<CalleesPrinter> {
 public:
  explicit CalleesPrinter(clang::MangleContext* ctx) : ctx_(ctx) {
  }

  virtual bool VisitCallExpr(clang::CallExpr* expr) {
    const clang::FunctionDecl* callee = expr->getDirectCallee();
    if (callee != NULL) AnalyzeFunction(callee);
    return true;
  }

  void AnalyzeFunction(const clang::FunctionDecl* f) {
    MangledName name;
    if (InV8Namespace(f) && GetMangledName(ctx_, f, &name)) {
      AddCallee(name);

      const clang::FunctionDecl* body = NULL;
      if (f->hasBody(body) && !Analyzed(name)) {
        EnterScope(name);
        TraverseStmt(body->getBody());
        LeaveScope();
      }
    }
  }

  typedef std::map<MangledName, CalleesSet* > Callgraph;

  bool Analyzed(const MangledName& name) {
    return callgraph_[name] != NULL;
  }

  void EnterScope(const MangledName& name) {
    CalleesSet* callees = callgraph_[name];

    if (callees == NULL) {
      callgraph_[name] = callees = new CalleesSet();
    }

    scopes_.push(callees);
  }

  void LeaveScope() {
    scopes_.pop();
  }

  void AddCallee(const MangledName& name) {
    if (!scopes_.empty()) scopes_.top()->insert(name);
  }

  void PrintCallGraph() {
    for (Callgraph::const_iterator i = callgraph_.begin(), e = callgraph_.end();
         i != e;
         ++i) {
      std::cout << i->first << "\n";

      CalleesSet* callees = i->second;
      for (CalleesSet::const_iterator j = callees->begin(), e = callees->end();
           j != e;
           ++j) {
        std::cout << "\t" << *j << "\n";
      }
    }
  }

 private:
  clang::MangleContext* ctx_;

  std::stack<CalleesSet* > scopes_;
  Callgraph callgraph_;
};

class FunctionDeclarationFinder
    : public clang::ASTConsumer,
      public clang::RecursiveASTVisitor<FunctionDeclarationFinder> {
 public:
  explicit FunctionDeclarationFinder(clang::Diagnostic& d,
                                     clang::SourceManager& sm)
      : d_(d), sm_(sm) { }

  virtual void HandleTranslationUnit(clang::ASTContext &ctx) {
    mangle_context_ = clang::createItaniumMangleContext(ctx, d_);
    callees_printer_ = new CalleesPrinter(mangle_context_);

    TraverseDecl(ctx.getTranslationUnitDecl());

    callees_printer_->PrintCallGraph();
  }

  virtual bool VisitFunctionDecl(clang::FunctionDecl* decl) {
    callees_printer_->AnalyzeFunction(decl);
    return true;
  }

 private:
  clang::Diagnostic& d_;
  clang::SourceManager& sm_;
  clang::MangleContext* mangle_context_;

  CalleesPrinter* callees_printer_;
};


static bool loaded = false;
static CalleesSet gc_suspects;


static void LoadGCSuspects() {
  if (loaded) return;

  std::ifstream fin("gcsuspects");
  std::string s;

  while (fin >> s) gc_suspects.insert(s);

  loaded = true;
}


static bool KnownToCauseGC(clang::MangleContext* ctx,
                           const clang::FunctionDecl* decl) {
  LoadGCSuspects();

  if (!InV8Namespace(decl)) return false;

  MangledName name;
  if (GetMangledName(ctx, decl, &name)) {
    return gc_suspects.find(name) != gc_suspects.end();
  }

  return false;
}


static bool IsHandleType(const clang::DeclarationName& handleDeclName,
                         const clang::QualType& qtype) {
  const clang::Type* canonical_type =
      qtype.getTypePtr()->getCanonicalTypeUnqualified().getTypePtr();

  if (const clang::TemplateSpecializationType* type =
          canonical_type->getAs<clang::TemplateSpecializationType>()) {
    if (clang::TemplateDecl* decl =
            type->getTemplateName().getAsTemplateDecl()) {
      if (decl->getTemplatedDecl()->getDeclName() == handleDeclName) {
        return true;
      }
    }
  } else if (const clang::RecordType* type =
                 canonical_type->getAs<clang::RecordType>()) {
    if (const clang::ClassTemplateSpecializationDecl* t =
        dyn_cast<clang::ClassTemplateSpecializationDecl>(type->getDecl())) {
      if (t->getSpecializedTemplate()->getDeclName() == handleDeclName) {
        return true;
      }
    }
  }

  return false;
}


class ExpressionClassifier :
    public clang::RecursiveASTVisitor<ExpressionClassifier> {
 public:
  ExpressionClassifier(clang::DeclarationName handleDeclName,
                       clang::MangleContext* ctx,
                       clang::CXXRecordDecl* objectDecl)
      : handleDeclName_(handleDeclName),
        ctx_(ctx),
        objectDecl_(objectDecl) {
  }

  bool IsBadExpression(clang::Expr* expr) {
    has_derefs_ = has_gc_ = false;
    TraverseStmt(expr);
    return has_derefs_ && has_gc_;
  }

  bool IsBadCallSite(clang::Expr* expr) {
    if (isa<clang::CallExpr>(expr)) {
      clang::CallExpr* call = cast<clang::CallExpr>(expr);

      MarkGCSuspectAsArgument(call);
      MarkHandleDereferenceAsArgument(call);

      return derefs_.any() &&
          ((gc_.count() > 1) || (gc_.any() && (gc_ ^ derefs_).any()));
    }
    return false;
  }

  virtual bool VisitExpr(clang::Expr* expr) {
    has_derefs_ = has_derefs_ || IsRawPointerType(expr);
    return !has_gc_ || !has_derefs_;
  }

  virtual bool VisitCallExpr(clang::CallExpr* expr) {
    has_gc_ = has_gc_ || CanCauseGC(expr);
    return !has_gc_ || !has_derefs_;
  }
 private:
  void MarkHandleDereferenceAsArgument(clang::CallExpr* call) {
    derefs_.reset();

    if (clang::CXXMemberCallExpr* memcall =
            dyn_cast<clang::CXXMemberCallExpr>(call)) {
      if (ManipulatesRawPointers(memcall->getImplicitObjectArgument())) {
        derefs_.set(0);
      }
    }

    for (unsigned arg = 0; arg < call->getNumArgs(); arg++) {
      if (ManipulatesRawPointers(call->getArg(arg))) derefs_.set(arg + 1);
    }
  }

  void MarkGCSuspectAsArgument(clang::CallExpr* call) {
    gc_.reset();

    clang::CXXMemberCallExpr* memcall =
        dyn_cast_or_null<clang::CXXMemberCallExpr>(call);
    if (memcall != NULL && CanCauseGC(memcall->getImplicitObjectArgument())) {
      gc_.set(0);
    }

    for (unsigned arg = 0; arg < call->getNumArgs(); arg++) {
      if (CanCauseGC(call->getArg(arg))) gc_.set(arg + 1);
    }
  }

  const clang::TagType* ToTagType(const clang::Type* t) {
    if (t == NULL) {
      return NULL;
    } else if (isa<clang::TagType>(t)) {
      return cast<clang::TagType>(t);
    } else if (isa<clang::SubstTemplateTypeParmType>(t)) {
      return ToTagType(cast<clang::SubstTemplateTypeParmType>(t)->
                           getReplacementType().getTypePtr());
    } else {
      return NULL;
    }
  }

  bool IsRawPointerType(clang::Expr* expr) {
    clang::QualType result = expr->getType();

    const clang::PointerType* type =
        dyn_cast_or_null<clang::PointerType>(expr->getType().getTypePtr());
    if (type == NULL) return false;

    const clang::TagType* pointee =
        ToTagType(type->getPointeeType().getTypePtr());
    if (pointee == NULL) return false;

    clang::CXXRecordDecl* record =
        dyn_cast_or_null<clang::CXXRecordDecl>(pointee->getDecl());
    if (record == NULL) return false;

    return InV8Namespace(record) &&
        record->hasDefinition() &&
        ((record == objectDecl_) || record->isDerivedFrom(objectDecl_));
  }

  bool IsHandleDereference(clang::Expr* expr) {
    if (expr == NULL) {
      return false;
    } else if (isa<clang::UnaryOperator>(expr)) {
      clang::UnaryOperator* unop = cast<clang::UnaryOperator>(expr);
      return unop->getOpcode() == clang::UO_Deref &&
          IsHandleType(handleDeclName_, unop->getSubExpr()->getType());
    } else if (isa<clang::CXXOperatorCallExpr>(expr)) {
      clang::CXXOperatorCallExpr* op = cast<clang::CXXOperatorCallExpr>(expr);
      return (op->getOperator() == clang::OO_Star ||
              op->getOperator() == clang::OO_Arrow) &&
          IsHandleType(handleDeclName_, op->getArg(0)->getType());
    } else {
      return false;
    }
  }

  bool CanCauseGC(clang::Expr* expr) {
    if (expr == NULL) return false;

    has_gc_ = false;
    has_derefs_ = true;
    TraverseStmt(expr);
    return has_gc_;
  }

  bool ManipulatesRawPointers(clang::Expr* expr) {
    if (expr == NULL) return false;

    has_gc_ = true;
    has_derefs_ = false;
    TraverseStmt(expr);
    return has_derefs_;
  }

  bool CanCauseGC(const clang::CallExpr* call) {
    const clang::FunctionDecl* fn = call->getDirectCallee();
    return (fn != NULL) && KnownToCauseGC(ctx_, fn);
  }

  // For generic expression classification.
  bool has_derefs_;
  bool has_gc_;

  // For callsite classification.
  static const int kMaxNumberOfArguments = 64;
  std::bitset<kMaxNumberOfArguments> derefs_;
  std::bitset<kMaxNumberOfArguments> gc_;

  clang::DeclarationName handleDeclName_;
  clang::MangleContext* ctx_;
  clang::CXXRecordDecl* objectDecl_;
};

const std::string BAD_EXPRESSION_MSG("Possible problem with evaluation order.");

class ExpressionsFinder : public clang::ASTConsumer,
                          public clang::RecursiveASTVisitor<ExpressionsFinder> {
 public:
  explicit ExpressionsFinder(clang::Diagnostic& d, clang::SourceManager& sm)
      : d_(d), sm_(sm) { }

  struct Resolver {
    explicit Resolver(clang::ASTContext& ctx)
        : ctx_(ctx), decl_ctx_(ctx.getTranslationUnitDecl()) {
    }

    Resolver(clang::ASTContext& ctx, clang::DeclContext* decl_ctx)
        : ctx_(ctx), decl_ctx_(decl_ctx) {
    }

    clang::DeclarationName ResolveName(const char* n) {
      clang::IdentifierInfo* ident = &ctx_.Idents.get(n);
      return ctx_.DeclarationNames.getIdentifier(ident);
    }

    Resolver ResolveNamespace(const char* n) {
      return Resolver(ctx_, Resolve<clang::NamespaceDecl>(n));
    }

    template<typename T>
    T* Resolve(const char* n) {
      if (decl_ctx_ == NULL) return NULL;

      clang::DeclContext::lookup_result result =
          decl_ctx_->lookup(ResolveName(n));

      for (clang::DeclContext::lookup_iterator i = result.first,
               e = result.second;
           i != e;
           i++) {
        if (isa<T>(*i)) return cast<T>(*i);
      }

      return NULL;
    }

   private:
    clang::ASTContext& ctx_;
    clang::DeclContext* decl_ctx_;
  };

  virtual void HandleTranslationUnit(clang::ASTContext &ctx) {
    Resolver r(ctx);

    clang::CXXRecordDecl* objectDecl =
        r.ResolveNamespace("v8").ResolveNamespace("internal").
            Resolve<clang::CXXRecordDecl>("Object");

    if (objectDecl != NULL) {
      expression_classifier_ =
          new ExpressionClassifier(r.ResolveName("Handle"),
                                   clang::createItaniumMangleContext(ctx, d_),
                                   objectDecl);
      TraverseDecl(ctx.getTranslationUnitDecl());
    } else {
      std::cerr << "Failed to resolve v8::internal::Object" << std::endl;
    }
  }

  virtual bool VisitExpr(clang::Expr* expr) {
    if ( expression_classifier_->IsBadCallSite(expr) ) {
      d_.Report(clang::FullSourceLoc(expr->getExprLoc(), sm_),
                d_.getCustomDiagID(clang::Diagnostic::Warning,
                                   BAD_EXPRESSION_MSG));
    }

    return true;
  }

 private:
  clang::Diagnostic& d_;
  clang::SourceManager& sm_;

  ExpressionClassifier* expression_classifier_;
};


template<typename ConsumerType>
class Action : public clang::PluginASTAction {
 protected:
  clang::ASTConsumer *CreateASTConsumer(clang::CompilerInstance &CI,
                                        llvm::StringRef InFile) {
    return new ConsumerType(CI.getDiagnostics(), CI.getSourceManager());
  }

  bool ParseArgs(const clang::CompilerInstance &CI,
                 const std::vector<std::string>& args) {
    return true;
  }

  void PrintHelp(llvm::raw_ostream& ros) { }
};


}

static clang::FrontendPluginRegistry::Add<Action<ExpressionsFinder> >
FindProblems("find-problems", "Find possible problems with evaluations order.");

static clang::FrontendPluginRegistry::Add<Action<FunctionDeclarationFinder> >
DumpCallees("dump-callees", "Dump callees for each function.");
