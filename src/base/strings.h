// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_STRINGS_H_
#define V8_BASE_STRINGS_H_

#include "src/base/base-export.h"
#include "src/base/macros.h"
#include "src/base/vector.h"

namespace v8 {
namespace base {

V8_BASE_EXPORT int PRINTF_FORMAT(2, 0)
    VSNPrintF(Vector<char> str, const char* format, va_list args);

// Safe formatting print. Ensures that str is always null-terminated.
// Returns the number of chars written, or -1 if output was truncated.
V8_BASE_EXPORT int PRINTF_FORMAT(2, 3)
    SNPrintF(Vector<char> str, const char* format, ...);

V8_BASE_EXPORT void StrNCpy(base::Vector<char> dest, const char* src, size_t n);

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_STRINGS_H_
