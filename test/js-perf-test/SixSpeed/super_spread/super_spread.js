// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This benchmark is based on the six-speed benchmark build output.
// Copyright 2014 Kevin Decker <https://github.com/kpdecker/six-speed/>

new BenchmarkSuite('SuperSpread-ES5', [1000], [
  new Benchmark('ES5', true, true, 150000, ES5),
]);

new BenchmarkSuite('SuperSpread-Babel', [1000], [
  new Benchmark('Babel', true, true, 150000, Babel),
]);

new BenchmarkSuite('SuperSpread-ES6', [1000], [
  new Benchmark('ES6', true, true, 150000, ES6),
]);

// ----------------------------------------------------------------------------
// Benchmark: ES5
// ----------------------------------------------------------------------------

function ES5() {
  'use strict';

  function Point(x, y) {
    this.x = x;
    this.y = y;
  }

  function MyPoint() {
    Point.apply(this, arguments);
  }

  function makePoint(x, y) {
    return new MyPoint(x, y);
  }

  return makePoint(1, 2);
}

// ----------------------------------------------------------------------------
// Benchmark: Babel
// ----------------------------------------------------------------------------

function _possibleConstructorReturn(self, call) {
  if (!self) {
    throw new ReferenceError(
        'this hasn\'t been initialised - super() hasn\'t been called');
  }
  return call && (typeof call === 'object' || typeof call === 'function') ?
      call :
      self;
}

function _inherits(subClass, superClass) {
  if (typeof superClass !== 'function' && superClass !== null) {
    throw new TypeError(
        'Super expression must either be null or a function, not ' +
        typeof superClass);
  }
  subClass.prototype = Object.create(superClass && superClass.prototype, {
    constructor: {
      value: subClass,
      enumerable: false,
      writable: true,
      configurable: true
    }
  });
  if (superClass)
    Object.setPrototypeOf ? Object.setPrototypeOf(subClass, superClass) :
                            subClass.__proto__ = superClass;
}

function _classCallCheck(instance, Constructor) {
  if (!(instance instanceof Constructor)) {
    throw new TypeError('Cannot call a class as a function');
  }
}

function Babel() {
  'use strict';
  var Point = function Point(x, y) {
    _classCallCheck(this, Point);

    this.x = x;
    this.y = y;
  };

  var MyPoint = function(_Point) {
    _inherits(MyPoint, _Point);

    function MyPoint() {
      _classCallCheck(this, MyPoint);

      return _possibleConstructorReturn(
          this, (MyPoint.__proto__ || Object.getPrototypeOf(MyPoint))
                    .apply(this, arguments));
    }

    return MyPoint;
  }(Point);

  function makePoint(x, y) {
    return new MyPoint(x, y);
  }

  return makePoint(1, 2);
}

// ----------------------------------------------------------------------------
// Benchmark: ES6
// ----------------------------------------------------------------------------

function ES6() {
  'use strict';

  class Point {
    constructor(x, y) {
      this.x = x;
      this.y = y;
    }
  }

  class MyPoint extends Point {}

  function makePoint(x, y) {
    return new MyPoint(x, y);
  }

  return makePoint(1, 2);
}
