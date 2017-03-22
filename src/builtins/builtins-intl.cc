// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_I18N_SUPPORT

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/i18n.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

BUILTIN(StringPrototypeToLowerCaseI18N) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.toLowerCase");
  string = String::Flatten(string);
  return ConvertCase(string, false, isolate);
}

BUILTIN(StringPrototypeToUpperCaseI18N) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.toUpperCase");
  string = String::Flatten(string);
  return ConvertCase(string, true, isolate);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_I18N_SUPPORT
