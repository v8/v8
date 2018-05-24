// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

load('sort-base.js');

createSuite('PackedSmi', 1000, Sort, CreatePackedSmiArray);
createSuite('PackedDouble', 1000, Sort, CreatePackedDoubleArray);
createSuite('PackedElement', 1000, Sort, CreatePackedObjectArray);

createSuite('HoleySmi', 1000, Sort, CreateHoleySmiArray);
createSuite('HoleyDouble', 1000, Sort, CreateHoleyDoubleArray);
createSuite('HoleyElement', 1000, Sort, CreateHoleyObjectArray);

createSuite('Dictionary', 1000, Sort, CreateDictionaryArray);
