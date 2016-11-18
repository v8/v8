// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// from test/webkit/fast/js/kde/parse.js
assertThrows("function test() { while(0) break lab; } lab: 1");
assertThrows("function test() { while(0) continue lab; } lab: 1");
assertThrows("function test() { while(0) break lab } lab: 1");
assertThrows("function test() { while(0) continue lab } lab: 1");

// from test/webkit/fast/js/parser-syntax-check.js
assertThrows("break ; break your_limits ; continue ; continue living ; debugger");
assertThrows("function f() { break ; break your_limits ; continue ; continue living ; debugger }");
assertThrows("try { break } catch(e) {}");
assertThrows("function f() { try { break } catch(e) {} }");
assertThrows("L: L: ;");
assertThrows("function f() { L: L: ; }");
assertThrows("L: L1: L: ;");
assertThrows("function f() { L: L1: L: ; }");
assertThrows("L: L1: L2: L3: L4: L: ;");
assertThrows("function f() { L: L1: L2: L3: L4: L: ; }");
