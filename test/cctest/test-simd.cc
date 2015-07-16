// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/objects.h"
#include "src/ostreams.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;


TEST(SameValue) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();

  HandleScope sc(isolate);

  float nan = std::numeric_limits<float>::quiet_NaN();

  Handle<Float32x4> a = factory->NewFloat32x4(0, 0, 0, 0);
  Handle<Float32x4> b = factory->NewFloat32x4(0, 0, 0, 0);
  CHECK(a->SameValue(*b));
  for (int i = 0; i < 4; i++) {
    a->set_lane(i, nan);
    CHECK(!a->SameValue(*b));
    CHECK(!a->SameValueZero(*b));
    b->set_lane(i, nan);
    CHECK(a->SameValue(*b));
    CHECK(a->SameValueZero(*b));
    a->set_lane(i, -0.0);
    CHECK(!a->SameValue(*b));
    b->set_lane(i, 0);
    CHECK(!a->SameValue(*b));
    CHECK(a->SameValueZero(*b));
    b->set_lane(i, -0.0);
    CHECK(a->SameValue(*b));
    CHECK(a->SameValueZero(*b));

    a->set_lane(i, 0);
    b->set_lane(i, 0);
  }
}
