// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/ast.h"
#include "src/ast-value-factory.h"
#include "src/func-name-inferrer.h"
#include "src/list-inl.h"

namespace v8 {
namespace internal {

FuncNameInferrer::FuncNameInferrer(AstValueFactory* ast_value_factory,
                                   Zone* zone)
    : ast_value_factory_(ast_value_factory),
      entries_stack_(10, zone),
      names_stack_(5, zone),
      funcs_to_infer_(4, zone),
      zone_(zone) {
}


void FuncNameInferrer::PushEnclosingName(const AstString* name) {
  // Enclosing name is a name of a constructor function. To check
  // that it is really a constructor, we check that it is not empty
  // and starts with a capital letter.
  if (!name->IsEmpty() && unibrow::Uppercase::Is(name->FirstCharacter())) {
    names_stack_.Add(Name(name, kEnclosingConstructorName), zone());
  }
}


void FuncNameInferrer::PushLiteralName(const AstString* name) {
  if (IsOpen() && name != ast_value_factory_->prototype_string()) {
    names_stack_.Add(Name(name, kLiteralName), zone());
  }
}


void FuncNameInferrer::PushVariableName(const AstString* name) {
  if (IsOpen() && name != ast_value_factory_->dot_result_string()) {
    names_stack_.Add(Name(name, kVariableName), zone());
  }
}


const AstString* FuncNameInferrer::MakeNameFromStack() {
  // First see how many names we will use.
  int length = 0;
  bool one_byte = true;
  int pos = 0;
  while (pos < names_stack_.length()) {
    if (pos < names_stack_.length() - 1 &&
        names_stack_.at(pos).type == kVariableName &&
        names_stack_.at(pos + 1).type == kVariableName) {
      // Skip consecutive variable declarations.
      ++pos;
      continue;
    }
    int cur_length = names_stack_.at(pos).name->length();
    if (length + 1 + cur_length > String::kMaxLength) {
      break;
    }
    if (length == 0) {
      length = cur_length;
    } else {  // Add the . between names.
      length += (1 + cur_length);
    }
    one_byte = one_byte && names_stack_.at(pos).name->is_one_byte();
    ++pos;
  }
  const AstString* to_return = NULL;
  const char* dot = ".";
  if (one_byte) {
    Vector<uint8_t> new_name = Vector<uint8_t>::New(length);
    int name_pos = 0;
    for (int i = 0; i < pos; ++i) {
      if (i < names_stack_.length() - 1 &&
          names_stack_.at(i).type == kVariableName &&
          names_stack_.at(i + 1).type == kVariableName) {
        // Skip consecutive variable declarations.
        continue;
      }
      if (name_pos != 0) {
        CopyChars(new_name.start() + name_pos, dot, 1);
        ++name_pos;
      }
      CopyChars(new_name.start() + name_pos,
                names_stack_.at(i).name->raw_data(),
                names_stack_.at(i).name->length());
      name_pos += names_stack_.at(i).name->length();
    }
    to_return = ast_value_factory_->GetOneByteString(Vector<const uint8_t>(
        reinterpret_cast<const uint8_t*>(new_name.start()),
        new_name.length()));
    new_name.Dispose();
  } else {
    Vector<uint16_t> new_name = Vector<uint16_t>::New(length);
    int name_pos = 0;
    for (int i = 0; i < pos; ++i) {
      if (i < names_stack_.length() - 1 &&
          names_stack_.at(i).type == kVariableName &&
          names_stack_.at(i + 1).type == kVariableName) {
        // Skip consecutive variable declarations.
        continue;
      }
      if (name_pos != 0) {
        CopyChars(new_name.start() + name_pos, dot, 1);
        ++name_pos;
      }
      if (names_stack_.at(i).name->is_one_byte()) {
        CopyChars(new_name.start() + name_pos,
                  names_stack_.at(i).name->raw_data(),
                  names_stack_.at(i).name->length());
      } else {
        CopyChars(new_name.start() + name_pos,
                  reinterpret_cast<const uint16_t*>(
                      names_stack_.at(i).name->raw_data()),
                  names_stack_.at(i).name->length());
      }
      name_pos += names_stack_.at(i).name->length();
    }
    to_return = ast_value_factory_->GetTwoByteString(Vector<const uint16_t>(
        reinterpret_cast<const uint16_t*>(new_name.start()),
        new_name.length()));
    new_name.Dispose();
  }
  return to_return;
}


void FuncNameInferrer::InferFunctionsNames() {
  const AstString* func_name = MakeNameFromStack();
  for (int i = 0; i < funcs_to_infer_.length(); ++i) {
    funcs_to_infer_[i]->set_raw_inferred_name(func_name);
  }
  funcs_to_infer_.Rewind(0);
}


} }  // namespace v8::internal
