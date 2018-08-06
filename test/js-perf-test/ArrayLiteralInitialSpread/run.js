// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Comparing different copy schemes against spread initial literals

const smallHoleyArray = Array(100);
const smallArray = Array.from(Array(100).keys());

for (var i = 0; i < 10; i++) {
  smallHoleyArray[i] = i;
}
for (var i = 90; i < 99; i++) {
  smallHoleyArray[i] = i;
}

const largeHoleyArray = new Array(100000);
const largeArray = Array.from(largeHoleyArray.keys());

for (var i = 0; i < 100; i++) {
  largeHoleyArray[i] = i;
}

for (var i = 5000; i < 5500; i++) {
  largeHoleyArray[i] = i;
}

// ----------------------------------------------------------------------------
// Benchmark: Spread
// ----------------------------------------------------------------------------

function SpreadSmall() {
  var newArr = [...smallArray];
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function SpreadLarge() {
  var newArr = [...largeArray];
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function SpreadSmallHoley() {
  var newArr = [...smallHoleyArray];
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function SpreadLargeHoley() {
  var newArr = [...largeHoleyArray];
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ForLength
// ----------------------------------------------------------------------------

function ForLengthSmall() {
  var newArr = new Array(smallArray.length);
  for (let i = 0; i < smallArray.length; i++) {
    newArr[i] = smallArray[i];
  }
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function ForLengthLarge() {
  var newArr = new Array(largeArray.length);
  for (let i = 0; i < largeArray.length; i++) {
    newArr[i] = largeArray[i];
  }
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function ForLengthSmallHoley() {
  var newArr = new Array(smallHoleyArray.length);
  for (let i = 0; i < smallHoleyArray.length; i++) {
    newArr[i] = smallHoleyArray[i];
  }
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function ForLengthLargeHoley() {
  var newArr = new Array(largeHoleyArray.length);
  for (let i = 0; i < largeHoleyArray.length; i++) {
    newArr[i] = largeHoleyArray[i];
  }
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ForLengthEmpty
// ----------------------------------------------------------------------------

function ForLengthEmptySmall() {
  var newArr = [];
  for (let i = 0; i < smallArray.length; i++) {
    newArr[i] = smallArray[i];
  }
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function ForLengthEmptyLarge() {
  var newArr = [];
  for (let i = 0; i < largeArray.length; i++) {
    newArr[i] = largeArray[i];
  }
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function ForLengthEmptySmallHoley() {
  var newArr = [];
  for (let i = 0; i < smallHoleyArray.length; i++) {
    newArr[i] = smallHoleyArray[i];
  }
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function ForLengthEmptyLargeHoley() {
  var newArr = [];
  for (let i = 0; i < largeHoleyArray.length; i++) {
    newArr[i] = largeHoleyArray[i];
  }
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Slice
// ----------------------------------------------------------------------------

function SliceSmall() {
  var newArr = smallArray.slice();
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function SliceLarge() {
  var newArr = largeArray.slice();
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function SliceSmallHoley() {
  var newArr = smallHoleyArray.slice();
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function SliceLargeHoley() {
  var newArr = largeHoleyArray.slice();
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: Slice0
// ----------------------------------------------------------------------------

function Slice0Small() {
  var newArr = smallArray.slice(0);
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function Slice0Large() {
  var newArr = largeArray.slice(0);
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function Slice0SmallHoley() {
  var newArr = smallHoleyArray.slice(0);
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function Slice0LargeHoley() {
  var newArr = largeHoleyArray.slice(0);
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ConcatReceive
// ----------------------------------------------------------------------------

function ConcatReceiveSmall() {
  var newArr = smallArray.concat();
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function ConcatReceiveLarge() {
  var newArr = largeArray.concat();
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function ConcatReceiveSmallHoley() {
  var newArr = smallHoleyArray.concat();
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function ConcatReceiveLargeHoley() {
  var newArr = largeHoleyArray.concat();
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ConcatArg
// ----------------------------------------------------------------------------

function ConcatArgSmall() {
  var newArr = [].concat(smallArray);
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function ConcatArgLarge() {
  var newArr = [].concat(largeArray);
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function ConcatArgSmallHoley() {
  var newArr = [].concat(smallHoleyArray);
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function ConcatArgLargeHoley() {
  var newArr = [].concat(largeHoleyArray);
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: ForOfPush
// ----------------------------------------------------------------------------

function ForOfPushSmall() {
  var newArr = [];
  for (let x of smallArray) {
    newArr.push(x)
  }
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function ForOfPushLarge() {
  var newArr = [];
  for (let x of largeArray) {
    newArr.push(x)
  }
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function ForOfPushSmallHoley() {
  var newArr = [];
  for (let x of smallHoleyArray) {
    newArr.push(x)
  }
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function ForOfPushLargeHoley() {
  var newArr = [];
  for (let x of largeHoleyArray) {
    newArr.push(x)
  }
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Benchmark: MapId
// ----------------------------------------------------------------------------

function MapIdSmall() {
  var newArr = smallArray.map(x => x);
  // basic sanity check
  if (newArr.length != smallArray.length) throw 666;
  return newArr;
}

function MapIdLarge() {
  var newArr = largeArray.map(x => x);
  // basic sanity check
  if (newArr.length != largeArray.length) throw 666;
  return newArr;
}

function MapIdSmallHoley() {
  var newArr = smallHoleyArray.map(x => x);
  // basic sanity check
  if (newArr.length != smallHoleyArray.length) throw 666;
  return newArr;
}

function MapIdLargeHoley() {
  var newArr = largeHoleyArray.map(x => x);
  // basic sanity check
  if (newArr.length != largeHoleyArray.length) throw 666;
  return newArr;
}

// ----------------------------------------------------------------------------
// Setup and Run
// ----------------------------------------------------------------------------

load('../base.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-ArrayLiteralInitialSpread(Score): ' + result);
}

function PrintError(name, error) {
  PrintResult('Error: ' + name, error);
  success = false;
}

function CreateBenchmark(name, f) {
  new BenchmarkSuite(name, [1000], [ new Benchmark(name, false, false, 0, f) ]);
}

CreateBenchmark('Spread-Small', SpreadSmall);
CreateBenchmark('ForLength-Small', ForLengthSmall);
CreateBenchmark('ForLengthEmpty-Small', ForLengthEmptySmall);
CreateBenchmark('Slice-Small', SliceSmall);
CreateBenchmark('Slice0-Small', Slice0Small);
CreateBenchmark('ConcatReceive-Small', ConcatReceiveSmall);
CreateBenchmark('ConcatArg-Small', ConcatArgSmall);
CreateBenchmark('ForOfPush-Small', ForOfPushSmall);
CreateBenchmark('MapId-Small', MapIdSmall);


CreateBenchmark('Spread-Large', SpreadLarge);
CreateBenchmark('ForLength-Large', ForLengthLarge);
CreateBenchmark('ForLengthEmpty-Large', ForLengthEmptyLarge);
CreateBenchmark('Slice-Large', SliceLarge);
CreateBenchmark('Slice0-Large', Slice0Large);
CreateBenchmark('ConcatReceive-Large', ConcatReceiveLarge);
CreateBenchmark('ConcatArg-Large', ConcatArgLarge);
CreateBenchmark('ForOfPush-Large', ForOfPushLarge);
CreateBenchmark('MapId-Large', MapIdLarge);


CreateBenchmark('Spread-SmallHoley', SpreadSmallHoley);
CreateBenchmark('ForLength-SmallHoley', ForLengthSmallHoley);
CreateBenchmark('ForLengthEmpty-SmallHoley', ForLengthEmptySmallHoley);
CreateBenchmark('Slice-SmallHoley', SliceSmallHoley);
CreateBenchmark('Slice0-SmallHoley', Slice0SmallHoley);
CreateBenchmark('ConcatReceive-SmallHoley', ConcatReceiveSmallHoley);
CreateBenchmark('ConcatArg-SmallHoley', ConcatArgSmallHoley);
CreateBenchmark('ForOfPush-SmallHoley', ForOfPushSmallHoley);
CreateBenchmark('MapId-SmallHoley', MapIdSmallHoley);


CreateBenchmark('Spread-LargeHoley', SpreadLargeHoley);
CreateBenchmark('ForLength-LargeHoley', ForLengthLargeHoley);
CreateBenchmark('ForLengthEmpty-LargeHoley', ForLengthEmptyLargeHoley);
CreateBenchmark('Slice-LargeHoley', SliceLargeHoley);
CreateBenchmark('Slice0-LargeHoley', Slice0LargeHoley);
CreateBenchmark('ConcatReceive-LargeHoley', ConcatReceiveLargeHoley);
CreateBenchmark('ConcatArg-LargeHoley', ConcatArgLargeHoley);
CreateBenchmark('ForOfPush-LargeHoley', ForOfPushLargeHoley);
CreateBenchmark('MapId-LargeHoley', MapIdLargeHoley);


BenchmarkSuite.config.doWarmup = undefined;
BenchmarkSuite.config.doDeterministic = undefined;
BenchmarkSuite.RunSuites({NotifyResult: PrintResult, NotifyError: PrintError});
