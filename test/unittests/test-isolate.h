// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_TEST_ISOLATE_H_
#define V8_UNITTESTS_TEST_ISOLATE_H_

#include "test/unittests/unittests.h"

namespace v8 {
namespace internal {

class IsolateTest : public EngineTest {
 public:
  IsolateTest();
  virtual ~IsolateTest();

  v8::Isolate* isolate() const;
  Isolate* i_isolate() const { return reinterpret_cast<Isolate*>(isolate()); }

 private:
  v8::Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(IsolateTest);
};


class ContextTest : public virtual IsolateTest {
 public:
  ContextTest()
      : handle_scope_(isolate()), context_scope_(v8::Context::New(isolate())) {}
  virtual ~ContextTest() {}

 private:
  v8::HandleScope handle_scope_;
  v8::Context::Scope context_scope_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_TEST_ISOLATE_H_
