// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

assertTrue(%SetIteratorProtector());
assertTrue(%MapIteratorProtector());
assertTrue(%StringIteratorProtector());
assertTrue(%ArrayIteratorProtector());
const arrayIteratorPrototype = Object.getPrototypeOf([].values());
const iteratorPrototype = Object.getPrototypeOf(arrayIteratorPrototype);
Object.setPrototypeOf(iteratorPrototype, {});
// All protectors must be invalidated.
assertFalse(%SetIteratorProtector());
assertFalse(%MapIteratorProtector());
assertFalse(%StringIteratorProtector());
assertFalse(%ArrayIteratorProtector());
