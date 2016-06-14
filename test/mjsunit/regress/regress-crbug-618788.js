// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var a = new Array();
a.constructor = Int32Array;
a.length = 1000; // Make the length >= 1000 so UseSparseVariant returns true.
assertThrows(() => a.slice());
