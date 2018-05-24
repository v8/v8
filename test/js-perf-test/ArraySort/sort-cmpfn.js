// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load('sort-base.js');

// Each benchmark calls sort with multiple different comparison functions
// to create polyomorphic call sites. Most/all of the
// other sort benchmarks have monomorphic call sites.
let sortfn = CreateSortFn([cmp_smaller, cmp_greater]);

createSuite('PackedSmi', 1000, sortfn, CreatePackedSmiArray);
createSuite('PackedDouble', 1000, sortfn, CreatePackedDoubleArray);
createSuite('PackedElement', 1000, sortfn, CreatePackedObjectArray);

createSuite('HoleySmi', 1000, sortfn, CreateHoleySmiArray);
createSuite('HoleyDouble', 1000, sortfn, CreateHoleyDoubleArray);
createSuite('HoleyElement', 1000, sortfn, CreateHoleyObjectArray);

createSuite('Dictionary', 1000, sortfn, CreateDictionaryArray);
