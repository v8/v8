// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PREPARSED_SCOPE_DATA_H_
#define V8_PARSING_PREPARSED_SCOPE_DATA_H_

#include <vector>

#include "src/globals.h"

namespace v8 {
namespace internal {

class PreParsedScopeData {
 public:
  PreParsedScopeData() {}
  ~PreParsedScopeData() {}

  // Saves the information needed for allocating the Scope's (and its
  // subscopes') variables.
  void SaveData(Scope* scope);

  // Restores the information needed for allocating the Scopes's (and its
  // subscopes') variables.
  void RestoreData(Scope* scope, int* index_ptr) const;

 private:
  friend class ScopeTestHelper;

  void SaveDataForVariable(Variable* var);
  void RestoreDataForVariable(Variable* var, int* index_ptr) const;

  // TODO(marja): Make the backing store more efficient once we know exactly
  // what data is needed.
  std::vector<byte> backing_store_;

  DISALLOW_COPY_AND_ASSIGN(PreParsedScopeData);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PREPARSED_SCOPE_DATA_H_
