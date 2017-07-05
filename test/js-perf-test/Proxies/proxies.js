// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function newBenchmark(name, handlers) {
  new BenchmarkSuite(name, [1000], [
    new Benchmark(name, false, false, 0,
                  handlers.run, handlers.setup, handlers.teardown)
  ]);
}

// ----------------------------------------------------------------------------

var result;
var foo = () => {}

newBenchmark("ProxyConstructorWithArrowFunc", {
  setup() { },
  run() {
    var proxy = new Proxy(foo, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

class Class {};

newBenchmark("ProxyConstructorWithClass", {
  setup() { },
  run() {
    var proxy = new Proxy(Class, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

var obj = {};

newBenchmark("ProxyConstructorWithObject", {
  setup() { },
  run() {
    var proxy = new Proxy(obj, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

var p = new Proxy({}, {});

newBenchmark("ProxyConstructorWithProxy", {
  setup() { },
  run() {
    var proxy = new Proxy(p, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});
