// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_PLATFORM_H_
#define INCLUDE_CPPGC_PLATFORM_H_

#include "include/v8-platform.h"

namespace cppgc {

// TODO(v8:10346): Put PageAllocator in a non-V8 include header to avoid
// depending on namespace v8.
using PageAllocator = v8::PageAllocator;

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_PLATFORM_H_
