// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

new BenchmarkSuite('Filter', [1000], [
  new Benchmark('SmiFilter', false, false, 0,
                Filter, SmiFilterSetup, ()=>{}),
  new Benchmark('DoubleFilter', false, false, 0,
                Filter, DoubleFilterSetup, ()=>{}),
  new Benchmark('FastFilter', false, false, 0,
                Filter, FastFilterSetup, ()=>{}),
  new Benchmark('HoleySmiFilter', false, false, 0,
                Filter, HoleySmiFilterSetup, ()=>{}),
  new Benchmark('HoleyDoubleFilter', false, false, 0,
                Filter, HoleyDoubleFilterSetup, ()=>{}),
  new Benchmark('HoleyFastFilter', false, false, 0,
                Filter, HoleyFastFilterSetup, ()=>{}),
  new Benchmark('ObjectFilter', false, false, 0,
                GenericFilter, ObjectFilterSetup, ()=>{}),
]);

var array;
var func;
var this_arg;
var result;
var array_size = 100;

function Filter() {
  result = array.filter(func, this_arg);
}

function GenericFilter() {
  result = Array.prototype.filter.call(array, func, this_arg);
}

function SmiFilterSetup() {
  array = new Array();
  for (var i = 0; i < array_size; i++) array[i] = i;
  func = (value, index, object) => { return value % 2 === 0; };
}

function HoleySmiFilterSetup() {
  array = new Array(array_size);
  for (var i = 0; i < array_size; i++) {
    if (i % 2 === 0) array[i] = i;
  }
  func = (value, index, object) => { return value % 2 === 0; };
}

function DoubleFilterSetup() {
  array = new Array();
  for (var i = 0; i < array_size; i++) array[i] = (i + 0.5);
  func = (value, index, object) => { return Math.floor(value) % 2 === 0; };
}

function HoleyDoubleFilterSetup() {
  array = new Array(array_size);
  for (var i = 0; i < array_size; i++) {
    if (i != 3) {
      array[i] = (i + 0.5);
    }
  }
  func = (value, index, object) => { return Math.floor(value) % 2 === 0; };
}

function FastFilterSetup() {
  array = new Array();
  for (var i = 0; i < array_size; i++) array[i] = 'value ' + i;
  func = (value, index, object) => { return index % 2 === 0; };
}

function HoleyFastFilterSetup() {
  array = new Array(array_size);
  for (var i = 0; i < array_size; i++) {
    if (i % 2 != 0) {
      array[i] = 'value ' + i;
    }
  }
  func = (value, index, object) => { return index % 2 === 0; };
}

function ObjectFilterSetup() {
  array = { length: array_size };
  for (var i = 0; i < array_size; i++) {
    array[i] = i;
  }
  func = (value, index, object) => { return index % 2 === 0; };
}
