// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FLAGS_FLAGS_IMPL_H_
#define V8_FLAGS_FLAGS_IMPL_H_

#include "src/base/macros.h"

namespace v8::internal {

class V8_EXPORT_PRIVATE FlagHelpers {
 public:
  static char NormalizeChar(char ch);

  static int FlagNamesCmp(const char* a, const char* b);

  static bool EqualNames(const char* a, const char* b);
};

}  // namespace v8::internal

#endif  // V8_FLAGS_FLAGS_IMPL_H_
