// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function benchy(name, test, testSetup) {
  new BenchmarkSuite(name, [1000],
      [
        new Benchmark(name, false, false, 0, test, testSetup, ()=>{})
      ]);
}

benchy('NaiveFindReplacement', NaiveFind, NaiveFindSetup);
benchy('DoubleFind', DoubleFind, DoubleFindSetup);
benchy('SmiFind', SmiFind, SmiFindSetup);
benchy('FastFind', FastFind, FastFindSetup);
benchy('GenericFind', GenericFind, ObjectFindSetup);
benchy('OptFastFind', OptFastFind, FastFindSetup);

let find_array;
// Initialize func variable to ensure the first test doesn't benefit from
// global object property tracking.
let find_func = 0;
let find_result;
const find_array_size = 100;
const find_max_index = find_array_size - 1;

// Although these functions have the same code, they are separated for
// clean IC feedback.
function DoubleFind() {
  find_result = find_array.find(find_func);
}
function SmiFind() {
  find_result = find_array.find(find_func);
}
function FastFind() {
  find_result = find_array.find(find_func);
}

// Make sure we inline the callback, pick up all possible TurboFan
// optimizations.
function RunOptFastFind(multiple) {
  // Use of variable multiple in the callback function forces
  // context creation without escape analysis.
  //
  // Also, the arrow function requires inlining based on
  // SharedFunctionInfo.
  find_result = find_array.find((v, i, a) => v === `value ${multiple}`);
}

// Don't optimize because I want to optimize RunOptFastFind with a parameter
// to be used in the callback.
%NeverOptimizeFunction(OptFastFind);
function OptFastFind() { RunOptFastFind(find_max_index); }

function NaiveFind() {
  let index = -1;
  const length = find_array == null ? 0 : find_array.length;

  for (let index = 0; index < length; index++) {
    const value = find_array[index];
    if (find_func(value, index, find_array)) {
      find_result = value;
      break;
    }
  }
}

function GenericFind() {
  find_result = Array.prototype.find.call(find_array, find_func);
}

function NaiveFindSetup() {
  // Prime NaiveFind with polymorphic cases.
  find_array = [1, 2, 3];
  find_func = (v, i, a) => v === find_max_index;
  NaiveFind();
  NaiveFind();
  find_array = [3.4]; NaiveFind();
  find_array = new Array(10); find_array[0] = 'hello'; NaiveFind();
  SmiFindSetup();
  delete find_array[1];
}

function SmiFindSetup() {
  find_array = Array.from({ length: find_array_size }, (_, i) => i);
  find_func = (value, index, object) => value === find_max_index;
}

function DoubleFindSetup() {
  find_array = Array.from({ length: find_array_size }, (_, i) => i + 0.5);
  find_func = (value, index, object) => value === find_max_index + 0.5;
}

function FastFindSetup() {
  find_array = Array.from({ length: find_array_size }, (_, i) => `value ${i}`);
  find_func = (value, index, object) => value === `value ${find_max_index}`;
}

function ObjectFindSetup() {
  find_array = { length: find_array_size };
  for (let i = 0; i < find_array_size; i++) {
    find_array[i] = i;
  }
  find_func = (value, index, object) => value === find_max_index;
}
