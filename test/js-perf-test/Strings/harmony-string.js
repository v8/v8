// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

new BenchmarkSuite('StringFunctions', [1000], [
  new Benchmark('StringRepeat', false, false, 0,
                StringRepeat, StringRepeatSetup, StringRepeatTearDown),
  new Benchmark('StringStartsWith', false, false, 0,
                StringStartsWith, StringWithSetup, StringWithTearDown),
  new Benchmark('StringEndsWith', false, false, 0,
                StringEndsWith, StringWithSetup, StringWithTearDown),
  new Benchmark('StringContains', false, false, 0,
                StringContains, StringContainsSetup, StringWithTearDown),
]);


var result;

var stringRepeatSource = "abc";

function StringRepeatSetup() {
  result = undefined;
}

function StringRepeat() {
  result = stringRepeatSource.repeat(500);
}

function StringRepeatTearDown() {
  var expected = "";
  for(var i = 0; i < 1000; i++) {
    expected += stringRepeatSource;
  }
  return result === expected;
}


var str;
var substr;

function StringWithSetup() {
  str = "abc".repeat(500);
  substr = "abc".repeat(200);
  result = undefined;
}

function StringWithTearDown() {
  return !!result;
}

function StringStartsWith() {
  result = str.startsWith(substr);
}

function StringEndsWith() {
  result = str.endsWith(substr);
}

function StringContainsSetup() {
  str = "def".repeat(100) + "abc".repeat(100) + "qqq".repeat(100);
  substr = "abc".repeat(100);
}

function StringContains() {
  result = str.contains(substr);
}
