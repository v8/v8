// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-classes
'use strict';

class Stack extends Array { }

assertThrows(function() { new Stack(); }, TypeError);

class Stack1 extends Array {
  constructor() { super(); }
}

assertThrows(function() { new Stack1(); }, TypeError);

class Stack2 extends Array {
  constructor() { super(1, 25); }
}

assertThrows(function() { new Stack2(); }, TypeError);

let X = Array;

class Stack4 extends X { }

assertThrows(function() { new Stack2(); }, TypeError);
