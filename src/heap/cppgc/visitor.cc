// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/visitor.h"

namespace cppgc {

#ifdef V8_ENABLE_CHECKS
void Visitor::CheckObjectNotInConstruction(const void* address) {
  // TODO(chromium:1056170): |address| is an inner pointer of an object. Check
  // that the object is not in construction.
}
#endif  // V8_ENABLE_CHECKS

}  // namespace cppgc
