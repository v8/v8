// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_TRANSLATION_ARRAY_H_
#define V8_DEOPTIMIZER_TRANSLATION_ARRAY_H_

#include "src/objects/fixed-array.h"

namespace v8 {
namespace internal {

// The TranslationArray is the on-heap representation of translations created
// during code generation in a (zone-allocated) TranslationBuffer. The
// translation array specifies how to transform an optimized frame back into
// one or more unoptimized frames.
// TODO(jgruber): Consider a real type instead of this type alias.
using TranslationArray = ByteArray;

class TranslationArrayIterator {
 public:
  TranslationArrayIterator(TranslationArray buffer, int index);

  int32_t Next();

  bool HasNext() const;

  void Skip(int n) {
    for (int i = 0; i < n; i++) Next();
  }

 private:
  TranslationArray buffer_;
  int index_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_TRANSLATION_ARRAY_H_
