// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function __f_1(__v_19) {
    var __v_17 = 0;
    var __v_18 = __v_19 ? 1 : 2147483648;
    for (var __v_19 = 0; __v_19 < 100000; __v_19++) {
        __v_17 += __v_18;
    }
  return __v_17 + __v_18;
}
function __f_2(__v_20, __v_22) {
    __v_20(true);
    assertEquals(214750512283648, __v_20());
}
  __f_2(__f_1);
