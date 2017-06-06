// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_COMMON_FLAG_UTILS_H
#define V8_TEST_COMMON_FLAG_UTILS_H

class EnableFlagScope {
 public:
  EnableFlagScope(bool* flag, bool new_value = true)
      : flag_(flag), previous_value_(flag) {
    *flag = new_value;
  }
  ~EnableFlagScope() { *flag_ = previous_value_; }

 private:
  bool* flag_;
  bool previous_value_;
};

#define EXPERIMENTAL_FLAG_SCOPE(flag) \
  EnableFlagScope __flag_scope_##__LINE__(&FLAG_experimental_wasm_##flag)

#endif  // V8_TEST_COMMON_FLAG_UTILS_H
