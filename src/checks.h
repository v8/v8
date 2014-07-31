// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CHECKS_H_
#define V8_CHECKS_H_

#include "src/base/logging.h"

// Simulator specific helpers.
// We can't use USE_SIMULATOR here because it isn't defined yet.
#if V8_TARGET_ARCH_ARM64 && !V8_HOST_ARCH_ARM64
  // TODO(all): If possible automatically prepend an indicator like
  // UNIMPLEMENTED or LOCATION.
  #define ASM_UNIMPLEMENTED(message)                                         \
  __ Debug(message, __LINE__, NO_PARAM)
  #define ASM_UNIMPLEMENTED_BREAK(message)                                   \
  __ Debug(message, __LINE__,                                                \
           FLAG_ignore_asm_unimplemented_break ? NO_PARAM : BREAK)
  #define ASM_LOCATION(message)                                              \
  __ Debug("LOCATION: " message, __LINE__, NO_PARAM)
#else
  #define ASM_UNIMPLEMENTED(message)
  #define ASM_UNIMPLEMENTED_BREAK(message)
  #define ASM_LOCATION(message)
#endif


#ifdef DEBUG
#ifndef OPTIMIZED_DEBUG
#define ENABLE_SLOW_ASSERTS    1
#endif
#endif

namespace v8 {

class Value;
template <class T> class Handle;

namespace internal {

intptr_t HeapObjectTagMask();

#ifdef ENABLE_SLOW_ASSERTS
#define SLOW_ASSERT(condition) \
  CHECK(!v8::internal::FLAG_enable_slow_asserts || (condition))
extern bool FLAG_enable_slow_asserts;
#else
#define SLOW_ASSERT(condition) ((void) 0)
const bool FLAG_enable_slow_asserts = false;
#endif

} }  // namespace v8::internal


void CheckNonEqualsHelper(const char* file, int line,
                          const char* expected_source, double expected,
                          const char* value_source, double value);

void CheckEqualsHelper(const char* file, int line, const char* expected_source,
                       double expected, const char* value_source, double value);

void CheckNonEqualsHelper(const char* file, int line,
                          const char* unexpected_source,
                          v8::Handle<v8::Value> unexpected,
                          const char* value_source,
                          v8::Handle<v8::Value> value);

void CheckEqualsHelper(const char* file,
                       int line,
                       const char* expected_source,
                       v8::Handle<v8::Value> expected,
                       const char* value_source,
                       v8::Handle<v8::Value> value);

#define ASSERT_TAG_ALIGNED(address) \
  ASSERT((reinterpret_cast<intptr_t>(address) & HeapObjectTagMask()) == 0)

#define ASSERT_SIZE_TAG_ALIGNED(size) ASSERT((size & HeapObjectTagMask()) == 0)

#endif  // V8_CHECKS_H_
