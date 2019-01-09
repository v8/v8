// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function createSuite(name, count, fn) {
  new BenchmarkSuite(name, [count], [new Benchmark(name, true, false, 0, fn)]);
}

const inputs = [
  'I\xF1t\xEBrn\xE2ti\xF4n\xE0liz\xE6ti\xF8n\u2603\uD83D\uDCA9\uFFFD',
  'Lorem ipsum dolor sit amet, consectetur adipiscing elit.',
  'Integer eu augue suscipit, accumsan ipsum nec, sagittis sem.',
  'In vitae pellentesque dolor. Curabitur leo nunc, luctus vitae',
  'risus eget, fermentum hendrerit justo.',
  'hello'.repeat(1024),
  'h',
  ''
];
const firsts = ['I', 'Integer', 'Lorem', 'risus', 'hello'];

function simpleHelper() {
  let sum = 0;
  for (input of inputs) {
    for (first of firsts) {
      sum += input.startsWith(first);
    }
  }
  return sum;
}

function consInputHelper() {
  let sum = 0;
  for (inputOne of inputs) {
    for (inputTwo of inputs) {
      for (first of firsts) {
        sum += %ConstructConsString(inputOne, inputTwo).startsWith(first);
      }
    }
  }
  return sum;
}

function consFirstHelper() {
  let sum = 0;
  for (input of inputs) {
    for (firstOne of firsts) {
      for (firstTwo of firsts) {
        sum += input.startsWith(%ConstructConsString(firstOne, firstTwo));
      }
    }
  }
  return sum;
}

function doubleConsHelper() {
  let sum = 0;
  for (inputOne of inputs) {
    for (inputTwo of inputs) {
      for (firstOne of firsts) {
        for (firstTwo of firsts) {
          sum += %ConstructConsString(inputOne, inputTwo).startsWith(
            %ConstructConsString(firstOne, firstTwo)
          );
        }
      }
    }
  }
}

createSuite('DirectStringsDirectSearch', 1000, simpleHelper);
createSuite('ConsStringsDirectSearch', 1000, consInputHelper);
createSuite('DirectStringsConsSearch', 1000, consFirstHelper);
createSuite('ConsStringsConsSearch', 1000, doubleConsHelper);
