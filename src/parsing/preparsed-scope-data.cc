// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/preparsed-scope-data.h"

#include "src/ast/scopes.h"
#include "src/ast/variables.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

class VariableIsUsedField : public BitField16<bool, 0, 1> {};
class VariableMaybeAssignedField
    : public BitField16<bool, VariableIsUsedField::kNext, 1> {};
class VariableContextAllocatedField
    : public BitField16<bool, VariableMaybeAssignedField::kNext, 1> {};

}  // namespace

void PreParsedScopeData::SaveData(Scope* scope) {
  size_t old_size = backing_store_.size();

  if (!scope->is_hidden()) {
    for (Variable* var : *scope->locals()) {
      if (var->mode() == VAR || var->mode() == LET || var->mode() == CONST) {
        SaveDataForVariable(var);
      }
    }
  }
  for (Scope* inner = scope->inner_scope(); inner != nullptr;
       inner = inner->sibling()) {
    SaveData(inner);
  }

  if (old_size != backing_store_.size()) {
#ifdef DEBUG
    backing_store_.push_back(scope->scope_type());
#endif

    backing_store_.push_back(scope->inner_scope_calls_eval());
  }
}

void PreParsedScopeData::RestoreData(Scope* scope, int* index_ptr) const {
  int& index = *index_ptr;
  int old_index = index;

  if (!scope->is_hidden()) {
    for (Variable* var : *scope->locals()) {
      if (var->mode() == VAR || var->mode() == LET || var->mode() == CONST) {
        RestoreDataForVariable(var, index_ptr);
      }
    }
  }
  for (Scope* inner = scope->inner_scope(); inner != nullptr;
       inner = inner->sibling()) {
    RestoreData(inner, index_ptr);
  }

  if (index != old_index) {
// Some data was read, i.e., there's data for the Scope.

#ifdef DEBUG
    DCHECK_EQ(backing_store_[index++], scope->scope_type());
#endif

    if (backing_store_[index++]) {
      scope->RecordEvalCall();
    }
  }
}

void PreParsedScopeData::SaveDataForVariable(Variable* var) {
#ifdef DEBUG
  // Store the variable name in debug mode; this way we can check that we
  // restore data to the correct variable.
  const AstRawString* name = var->raw_name();
  backing_store_.push_back(name->length());
  for (int i = 0; i < name->length(); ++i) {
    backing_store_.push_back(name->raw_data()[i]);
  }
#endif
  int variable_data = VariableIsUsedField::encode(var->is_used()) |
                      VariableMaybeAssignedField::encode(
                          var->maybe_assigned() == kMaybeAssigned) |
                      VariableContextAllocatedField::encode(
                          var->has_forced_context_allocation());

  backing_store_.push_back(variable_data);
}

void PreParsedScopeData::RestoreDataForVariable(Variable* var,
                                                int* index_ptr) const {
  int& index = *index_ptr;
#ifdef DEBUG
  const AstRawString* name = var->raw_name();
  DCHECK_EQ(backing_store_[index++], name->length());
  for (int i = 0; i < name->length(); ++i) {
    DCHECK_EQ(backing_store_[index++], name->raw_data()[i]);
  }
#endif
  int variable_data = backing_store_[index++];
  if (VariableIsUsedField::decode(variable_data)) {
    var->set_is_used();
  }
  if (VariableMaybeAssignedField::decode(variable_data)) {
    var->set_maybe_assigned();
  }
  if (VariableContextAllocatedField::decode(variable_data)) {
    var->ForceContextAllocation();
  }
}

}  // namespace internal
}  // namespace v8
