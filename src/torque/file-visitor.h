// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_FILE_VISITOR_H_
#define V8_TORQUE_FILE_VISITOR_H_

#include <string>

#include "src/torque/ast.h"
#include "src/torque/global-context.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

#include "src/torque/TorqueBaseVisitor.h"

namespace v8 {
namespace internal {
namespace torque {

class FileVisitor {
 public:
  explicit FileVisitor(GlobalContext& global_context)
      : global_context_(global_context),
        module_(global_context.GetDefaultModule()) {
    global_context_.PushScope(module_->scope());
  }

  Type GetType(const std::string& s) { return GetTypeOracle().GetType(s); }
  TypeVector GetTypeVector(const std::vector<std::string>& v) {
    TypeVector result;
    for (const std::string& s : v) {
      result.push_back(GetType(s));
    }
    return result;
  }

  Scope* TopScope() { return global_context_.TopScope(); }

  Ast* ast() { return global_context_.ast(); }

 protected:
  static constexpr const char* kTrueLabelName = "True";
  static constexpr const char* kFalseLabelName = "False";
  static constexpr const char* kReturnValueVariable = "return";
  static constexpr const char* kConditionValueVariable = "condition";
  static constexpr const char* kDoneLabelName = "done";
  static constexpr const char* kForIndexValueVariable = "for_index";

  Module* CurrentModule() const { return module_; }

  TypeOracle& GetTypeOracle() { return global_context_.GetTypeOracle(); }

  bool IsValueDeclared(const std::string& id) {
    return global_context_.Lookup(id) != nullptr;
  }

  Value* LookupValue(SourcePosition pos, const std::string& id) {
    Declarable* declarable = global_context_.Lookup(id);
    if (declarable != nullptr) {
      if (declarable->IsValue()) {
        return Value::cast(declarable);
      }
      ReportError(id + " referenced at " + PositionAsString(pos) +
                  " is not a variable, parameter or verbatim");
    }
    ReportError(std::string("identifier ") + id + " referenced at " +
                PositionAsString(pos) + " is not defined");
    return nullptr;
  }

  Callable* LookupCall(SourcePosition pos, const std::string& name,
                       const TypeVector& parameter_types) {
    Callable* result = nullptr;
    Declarable* declarable = global_context_.Lookup(name);
    if (declarable != nullptr) {
      if (declarable->IsBuiltin()) {
        result = Builtin::cast(declarable);
      } else if (declarable->IsRuntime()) {
        result = Runtime::cast(declarable);
      } else if (declarable->IsMacroList()) {
        for (auto& m : MacroList::cast(declarable)->list()) {
          if (GetTypeOracle().IsCompatibleSignature(
                  m->signature().parameter_types, parameter_types)) {
            result = m.get();
            break;
          }
        }
      }
    }
    if (result == nullptr) {
      std::stringstream stream;
      stream << "cannot find macro, builtin or runtime call " << name
             << " matching parameter types " << parameter_types;
      ReportError(stream.str());
    }
    size_t caller_size = parameter_types.size();
    size_t callee_size = result->signature().types().size();
    if (caller_size != callee_size &&
        !result->signature().parameter_types.var_args) {
      std::stringstream stream;
      stream << "parameter count mismatch calling " << *result << ": expected "
             << std::to_string(callee_size) << ", found "
             << std::to_string(caller_size);
      ReportError(stream.str());
    }

    return result;
  }

  Macro* LookupMacro(SourcePosition pos, const std::string& name,
                     const TypeVector& types) {
    Declarable* declarable = global_context_.Lookup(name);
    if (declarable != nullptr) {
      if (declarable->IsMacroList()) {
        for (auto& m : MacroList::cast(declarable)->list()) {
          if (m->signature().parameter_types.types == types &&
              !m->signature().parameter_types.var_args) {
            return m.get();
          }
        }
      }
    }
    std::stringstream stream;
    stream << "macro " << name << " with parameter types " << types
           << " referenced at " << PositionAsString(pos) << " is not defined";
    ReportError(stream.str());
    return nullptr;
  }

  Builtin* LookupBuiltin(const SourcePosition& pos, const std::string& name) {
    Declarable* declarable = global_context_.Lookup(name);
    if (declarable != nullptr) {
      if (declarable->IsBuiltin()) {
        return Builtin::cast(declarable);
      }
      ReportError(name + " referenced at " + PositionAsString(pos) +
                  " is not a builtin");
    }
    ReportError(std::string("builtin ") + name + " referenced at " +
                PositionAsString(pos) + " is not defined");
    return nullptr;
  }

  std::string GetParameterVariableFromName(const std::string& name) {
    return std::string("p_") + name;
  }

  std::string PositionAsString(SourcePosition pos) {
    return global_context_.PositionAsString(pos);
  }

  Signature MakeSignature(const ParameterList& parameters,
                          const std::string& return_type,
                          const LabelAndTypesVector& labels);

  GlobalContext& global_context_;
  Callable* current_callable_;
  Module* module_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_FILE_VISITOR_H_
