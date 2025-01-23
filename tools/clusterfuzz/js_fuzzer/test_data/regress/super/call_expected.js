// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original: regress/super/input.js
class A {
  constructor() {
    console.log(42);

    /* CrossOverMutator: Crossover from foo */
    super();
  }

  method() {
    console.log(42);

    /* CrossOverMutator: Crossover from foo */
    super();
  }

}

class B extends A {
  constructor() {
    console.log(42);

    /* CrossOverMutator: Crossover from foo */
    super();
  }

  method() {
    console.log(42);

    /* CrossOverMutator: Crossover from foo */
    super();
  }

}
