// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var CodeView = function(divID, PR, sourceText, sourcePosition, broker) {
  "use strict";
  var view = this;

  view.divElement = document.getElementById(divID);
  view.broker = broker;
  view.codeSelection = null;
  view.allSpans = [];

  var selectionHandler = {
    clear: function() {
      broker.clear(selectionHandler);
    },
    select: function(items, selected) {
      var handler = this;
      var divElement = view.divElement;
      var broker = view.broker;
      for (let span of items) {
        if (selected) {
          span.classList.add("selected");
        } else {
          span.classList.remove("selected");
        }
      }
      var ranges = [];
      for (var span of items) {
        ranges.push([span.start, span.end, null]);
      }
      broker.select(selectionHandler, ranges, selected);
    },
    selectionDifference: function(span1, inclusive1, span2, inclusive2) {
      var pos1 = span1.start;
      var pos2 = span2.start;
      var result = [];
      var lineListDiv = view.divElement.firstChild.firstChild.childNodes;
      for (var i=0; i < lineListDiv.length; i++) {
        var currentLineElement = lineListDiv[i];
        var spans = currentLineElement.childNodes;
        for (var j=0; j < spans.length; ++j) {
          var currentSpan = spans[j];
          if (currentSpan.start > pos1 || (inclusive1 && currentSpan.start == pos1)) {
            if (currentSpan.start < pos2 || (inclusive2 && currentSpan.start == pos2)) {
              result.push(currentSpan);
            }
          }
        }
      }
      return result;
    },
    brokeredSelect: function(ranges, selected) {
      var firstSelect = view.codeSelection.isEmpty();
      for (var range of ranges) {
        var start = range[0];
        var end = range[1];
        var lower = 0;
        var upper = view.allSpans.length;
        if (upper > 0) {
          while ((upper - lower) > 1) {
            var middle = Math.floor((upper + lower) / 2);
            var lineStart = view.allSpans[middle].start;
            if (lineStart < start) {
              lower = middle;
            } else if (lineStart > start) {
              upper = middle;
            } else {
              lower = middle;
              break;
            }
          }
          var currentSpan = view.allSpans[lower];
          var currentLineElement = currentSpan.parentNode;
          if ((currentSpan.start <= start && start < currentSpan.end) ||
              (currentSpan.start <= end && end < currentSpan.end)) {
            if (firstSelect) {
              makeContainerPosVisible(view.divElement, currentLineElement.offsetTop);
              firstSelect = false;
            }
            view.codeSelection.select(currentSpan, selected);
          }
        }
      }
    },
    brokeredClear: function() {
      view.codeSelection.clear();
    },
  };

  view.codeSelection = new Selection(selectionHandler);
  broker.addSelectionHandler(selectionHandler);

  var mouseDown = false;

  this.handleSpanMouseDown = function(e) {
    e.stopPropagation();
    if (!e.shiftKey) {
      view.codeSelection.clear();
    }
    view.codeSelection.select(this, true);
    mouseDown = true;
  }

  this.handleSpanMouseMove = function(e) {
    if (mouseDown) {
      view.codeSelection.extendTo(this);
    }
  }

  this.handleCodeMouseDown = function(e) {
    view.codeSelection.clear();
  }

  document.addEventListener('mouseup', function(e){
    mouseDown = false;
  }, false);

  this.initializeCode(sourceText, sourcePosition);
}

CodeView.prototype.initializeCode = function(sourceText, sourcePosition) {
  var view = this;
  if (sourceText == "") {
    var newHtml = "<pre class=\"prettyprint\"</pre>";
    view.divElement.innerHTML = newHtml;
  } else {
    var newHtml = "<pre class=\"prettyprint linenums\">"
      + sourceText + "</pre>";
    view.divElement.innerHTML = newHtml;
    try {
      // Wrap in try to work when offline.
      PR.prettyPrint();
    } catch (e) {
    }

    view.divElement.onmousedown = this.handleCodeMouseDown;

    var base = sourcePosition;
    var current = 0;
    var lineListDiv = view.divElement.firstChild.firstChild.childNodes;
    for (i=0; i < lineListDiv.length; i++) {
      var currentLineElement = lineListDiv[i];
      currentLineElement.id = "li" + i;
      var pos = base + current;
      currentLineElement.pos = pos;
      var spans = currentLineElement.childNodes;
      for (j=0; j < spans.length; ++j) {
        var currentSpan = spans[j];
        if (currentSpan.nodeType == 1) {
          currentSpan.start = pos;
          currentSpan.end = pos + currentSpan.textContent.length;
          currentSpan.onmousedown = this.handleSpanMouseDown;
          currentSpan.onmousemove = this.handleSpanMouseMove;
          view.allSpans.push(currentSpan);
        }
        current += currentSpan.textContent.length;
        pos = base + current;
      }
      while ((current < sourceText.length) && (
        sourceText[current] == '\n' ||
          sourceText[current] == '\r')) {
        ++current;
      }
    }
  }

  this.resizeToParent();
}

CodeView.prototype.resizeToParent = function() {
  var view = this;
  var documentElement = document.documentElement;
  var y = view.divElement.parentNode.clientHeight || documentElement.clientHeight;
  view.divElement.style.height = y + "px";
}
