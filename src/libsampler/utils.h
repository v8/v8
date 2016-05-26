// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LIBSAMPLER_UTILS_H_
#define V8_LIBSAMPLER_UTILS_H_

#include "include/v8.h"

namespace v8 {
namespace sampler {

class Malloced {
 public:
  static void* New(size_t size) {
    return malloc(size);
  }

  static void Delete(void* p) {
    free(p);
  }
};

}  // namespace sampler
}  // namespace v8

#endif  // V8_LIBSAMPLER_UTILS_H_
