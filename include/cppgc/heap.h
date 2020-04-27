// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_HEAP_H_
#define INCLUDE_CPPGC_HEAP_H_

#include <memory>

#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {
class Heap;
}  // namespace internal

class V8_EXPORT Heap {
 public:
  // Normal spaces are used to store objects of different size classes:
  // - kNormal1:  < 32 bytes
  // - kNormal2:  < 64 bytes
  // - kNormal3:  < 128 bytes
  // - kNormal4: >= 128 bytes
  // Objects of size greater than 2^16 get stored in the large space. Users can
  // register up to 4 arenas for application specific needs.
  enum class SpaceType {
    kNormal1,
    kNormal2,
    kNormal3,
    kNormal4,
    kLarge,
    kUserDefined1,
    kUserDefined2,
    kUserDefined3,
    kUserDefined4,
  };

  static std::unique_ptr<Heap> Create();

  virtual ~Heap() = default;

 private:
  Heap() = default;

  friend class internal::Heap;
};

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_HEAP_H_
