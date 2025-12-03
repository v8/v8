// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let cases = Array.from({length:400000}, (_,i)=>`case ${i}:`);
let f = Function('x', `switch(x){${cases.join(' ')} default:0;}`);
for (let i = 0; i < 20000; i++) f(i % 400000);
