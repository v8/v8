// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --js-sum-precise

assertEquals(-0, Math.sumPrecise([]));
assertEquals(6, Math.sumPrecise([1, 2, 3]));
assertEquals(-0, Math.sumPrecise([-0, -0]));
