// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

function f(a, b, c) { return arguments }
function g(...args) { return args }

var length = 65534;
var args = new Array(length);
assertEquals(length, f.apply(null, args).length);
assertEquals(length, g.apply(null, args).length);

// On 32-bit machines this produces an equally sized array, however it might in
// turn trigger a stack overflow on 64-bit machines, which we need to catch.
var length = Math.pow(2, 16) * 3;
var args = new Array(length);
try { f.apply(null, args) } catch(e) {}
try { g.apply(null, args) } catch(e) {}
