// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
(() => {

function benchy(name, test, testSetup) {
  new BenchmarkSuite(name, [1000],
      [
        new Benchmark(name, false, false, 0, test, testSetup, ()=>{})
      ]);
}

benchy('NaiveFindIndexReplacement', Naive, NaiveSetup);
benchy('DoubleFindIndex', Double, DoubleSetup);
benchy('SmiFindIndex', Smi, SmiSetup);
benchy('FastFindIndex', Fast, FastSetup);
benchy('GenericFindIndex', Generic, ObjectSetup);
benchy('OptFastFindIndex', OptFast, FastSetup);

let array;
// Initialize func variable to ensure the first test doesn't benefit from
// global object property tracking.
let func = 0;
let result;
const array_size = 100;
const max_index = array_size - 1;

// Although these functions have the same code, they are separated for
// clean IC feedback.
function Double() {
  result = array.findIndex(func);
}
function Smi() {
  result = array.findIndex(func);
}
function Fast() {
  result = array.findIndex(func);
}

// Make sure we inline the callback, pick up all possible TurboFan
// optimizations.
function RunOptFast(multiple) {
  // Use of variable multiple in the callback function forces
  // context creation without escape analysis.
  //
  // Also, the arrow function requires inlining based on
  // SharedFunctionInfo.
  result = array.findIndex((v, i, a) => v === `value ${multiple}`);
}

// Don't optimize because I want to optimize RunOptFast with a parameter
// to be used in the callback.
%NeverOptimizeFunction(OptFast);
function OptFast() { RunOptFast(max_index); }

function Naive() {
  let index = -1;
  const length = array == null ? 0 : array.length;

  for (let index = 0; index < length; index++) {
    const value = array[index];
    if (func(value, index, array)) {
      result = value;
      break;
    }
  }
}

function Generic() {
  result = Array.prototype.findIndex.call(array, func);
}

function NaiveSetup() {
  // Prime Naive with polymorphic cases.
  array = [1, 2, 3];
  func = (v, i, a) => v === max_index;
  Naive();
  Naive();
  array = [3.4]; Naive();
  array = new Array(10); array[0] = 'hello'; Naive();
  SmiSetup();
  delete array[1];
}

function SmiSetup() {
  array = Array.from({ length: array_size }, (_, i) => i);
  func = (value, index, object) => value === max_index;
}

function DoubleSetup() {
  array = Array.from({ length: array_size }, (_, i) => i + 0.5);
  func = (value, index, object) => value === max_index + 0.5;
}

function FastSetup() {
  array = Array.from({ length: array_size }, (_, i) => `value ${i}`);
  func = (value, index, object) => value === `value ${max_index}`;
}

function ObjectSetup() {
  array = { length: array_size };
  for (let i = 0; i < array_size; i++) {
    array[i] = i;
  }
  func = (value, index, object) => value === max_index;
}

})();
