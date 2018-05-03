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
        declarations_(global_context.declarations()),
        module_(global_context.GetDefaultModule()) {}

  TypeVector GetTypeVector(SourcePosition pos,
                           const std::vector<std::string>& v) {
    TypeVector result;
    for (const std::string& s : v) {
      result.push_back(declarations()->LookupType(pos, s));
    }
    return result;
  }

  Ast* ast() { return global_context_.ast(); }
  Declarations* declarations() { return global_context_.declarations(); }

 protected:
  static constexpr const char* kTrueLabelName = "True";
  static constexpr const char* kFalseLabelName = "False";
  static constexpr const char* kReturnValueVariable = "return";
  static constexpr const char* kConditionValueVariable = "condition";
  static constexpr const char* kDoneLabelName = "done";
  static constexpr const char* kForIndexValueVariable = "for_index";

  Module* CurrentModule() const { return module_; }

  TypeOracle& GetTypeOracle() { return global_context_.GetTypeOracle(); }

  std::string GetParameterVariableFromName(const std::string& name) {
    return std::string("p_") + name;
  }

  std::string PositionAsString(SourcePosition pos) {
    return global_context_.ast()->source_file_map()->PositionAsString(pos);
  }

  Callable* LookupCall(SourcePosition pos, const std::string& name,
                       const TypeVector& parameter_types);

  Signature MakeSignature(SourcePosition pos, const ParameterList& parameters,
                          const std::string& return_type,
                          const LabelAndTypesVector& labels);

  GlobalContext& global_context_;
  Declarations* declarations_;
  Callable* current_callable_;
  Module* module_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_FILE_VISITOR_H_
