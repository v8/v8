// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

assertEquals(0.6840442338072671 ** 2.4, 0.4019777798321958);

const constants = {
  '0': 1,
  '-1': 0.1,
  '-2': 0.01,
  '-3': 0.001,
  '-4': 0.0001,
  '-5': 0.00001,
  '-6': 0.000001,
  '-7': 0.0000001,
  '-8': 0.00000001,
  '-9': 0.000000001,
};

for (let i = 0; i > -10; i -= 1) {
  assertEquals(10 ** i, constants[i]);
  assertEquals(10 ** i, 1 / (10 ** -i));
}
