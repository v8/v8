// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_TEST_ZONE_H_
#define V8_UNITTESTS_TEST_ZONE_H_

#include "src/zone.h"
#include "test/unittests/test-isolate.h"

namespace v8 {
namespace internal {

class ZoneTest : public virtual IsolateTest {
 public:
  ZoneTest() : zone_(i_isolate()) {}
  virtual ~ZoneTest() {}

  Isolate* isolate() const { return i_isolate(); }
  Zone* zone() { return &zone_; }

 private:
  Zone zone_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_TEST_ZONE_H_
