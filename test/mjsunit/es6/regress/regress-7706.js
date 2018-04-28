// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

class Test extends Number {}
Test.prototype[Symbol.toStringTag] = "Test";

assertEquals("[object Test]", Object.prototype.toString.call(new Test));

%ToFastProperties(Test.prototype);
assertEquals("[object Test]", Object.prototype.toString.call(new Test));
