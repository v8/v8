// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_WRAPPERS_H_
#define V8_BASE_PLATFORM_WRAPPERS_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/base-export.h"
#include "src/base/platform/memory.h"

#if defined(V8_OS_STARBOARD)
#include "starboard/memory.h"
#include "starboard/string.h"
#endif

namespace v8 {
namespace base {

#if !defined(V8_OS_STARBOARD)

// Common libstd implementations.
// inline implementations are preferred here due to performance concerns.
inline char* Strdup(const char* source) { return strdup(source); }

inline FILE* Fopen(const char* filename, const char* mode) {
  return fopen(filename, mode);
}

inline int Fclose(FILE* stream) { return fclose(stream); }

#else  // V8_OS_STARBOARD

inline char* Strdup(const char* source) { return SbStringDuplicate(source); }

inline FILE* Fopen(const char* filename, const char* mode) { return NULL; }

inline int Fclose(FILE* stream) { return -1; }

#endif  // V8_OS_STARBOARD

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_WRAPPERS_H_
