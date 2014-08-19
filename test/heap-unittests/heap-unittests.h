// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_UNITTESTS_HEAP_UNITTESTS_H_
#define V8_HEAP_UNITTESTS_HEAP_UNITTESTS_H_

#include "include/v8.h"
#include "src/zone.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Factory;
class Heap;

class RuntimeTest : public ::testing::Test {
 public:
  RuntimeTest();
  virtual ~RuntimeTest();

  Factory* factory() const;
  Heap* heap() const;
  Isolate* isolate() const { return reinterpret_cast<Isolate*>(isolate_); }
  Zone* zone() { return &zone_; }

  static void SetUpTestCase();
  static void TearDownTestCase();

 private:
  static v8::Isolate* isolate_;
  v8::Isolate::Scope isolate_scope_;
  v8::HandleScope handle_scope_;
  Zone zone_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_UNITTESTS_HEAP_UNITTESTS_H_
