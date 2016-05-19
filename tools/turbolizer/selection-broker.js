// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var SelectionBroker = function() {
  this.brokers = [];
  this.dispatching = false;
  this.lastDispatchingHandler = null;
};

SelectionBroker.prototype.addSelectionHandler = function(handler) {
  this.brokers.push(handler);
}

SelectionBroker.prototype.select = function(from, ranges, selected) {
  if (!this.dispatching) {
    this.lastDispatchingHandler = from;
    try {
      this.dispatching = true;
      for (var b of this.brokers) {
        if (b != from) {
          b.brokeredSelect(ranges, selected);
        }
      }
    }
    finally {
      this.dispatching = false;
    }
  }
}

SelectionBroker.prototype.clear = function(from) {
  this.lastDispatchingHandler = null;
  if (!this.dispatching) {
    try {
      this.dispatching = true;
      this.brokers.forEach(function(b) {
        if (b != from) {
          b.brokeredClear();
        }
      });
    } finally {
      this.dispatching = false;
    }
  }
}
