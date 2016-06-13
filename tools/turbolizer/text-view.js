// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

class TextView extends View {
  constructor(id, broker, patterns, allowSpanSelection) {
    super(id, broker);
    let view = this;
    view.sortedPositionList = [];
    view.nodePositionMap = [];
    view.positionNodeMap = [];
    view.textListNode = view.divNode.getElementsByTagName('ul')[0];
    view.fillerSvgElement = view.divElement.append("svg").attr('version','1.1').attr("width", "0");
    view.patterns = patterns;
    view.allowSpanSelection = allowSpanSelection;
    view.nodeToLineMap = [];
    var selectionHandler = {
      clear: function() {
        broker.clear(selectionHandler);
      },
      select: function(items, selected) {
        for (let i of items) {
          if (selected) {
            i.classList.add("selected");
          } else {
            i.classList.remove("selected");
          }
        }
        broker.select(selectionHandler, view.getRanges(items), selected);
      },
      selectionDifference: function(span1, inclusive1, span2, inclusive2) {
        return null;
      },
      brokeredSelect: function(ranges, selected) {
        let locations = view.rangesToLocations(ranges);
        view.selectLocations(locations, selected, true);
      },
      brokeredClear: function() {
        view.selection.clear();
      }
    };
    view.selection = new Selection(selectionHandler);
    broker.addSelectionHandler(selectionHandler);
  }

  setPatterns(patterns) {
    let view = this;
    view.patterns = patterns;
  }

  clearText() {
    let view = this;
    while (view.textListNode.firstChild) {
      view.textListNode.removeChild(view.textListNode.firstChild);
    }
  }

  rangeToLocation(range) {
    return range;
  }

  rangesToLocations(ranges) {
    let view = this;
    let nodes = new Set();
    let result = [];
    for (let range of ranges) {
      let start = range[0];
      let end = range[1];
      let location = { pos_start: start, pos_end: end };
      if (range[2] !== null && range[2] != -1) {
        location.node_id = range[2];
        if (range[0] == -1 && range[1] == -1) {
          location.pos_start = view.nodePositionMap[location.node_id];
          location.pos_end = location.pos_start + 1;
        }
      } else {
        if (range[0] != undefined) {
          location.pos_start = range[0];
          location.pos_end = range[1];
        }
      }
      result.push(location);
    }
    return result;
  }

  sameLocation(l1, l2) {
    let view = this;
    if (l1.block_id != undefined && l2.block_id != undefined &&
      l1.block_id == l2.block_id && l1.node_id === undefined) {
      return true;
    }

    if (l1.address != undefined && l1.address == l2.address) {
      return true;
    }

    let node1 = l1.node_id;
    let node2 = l2.node_id;

    if (node1 === undefined && node2 == undefined) {
      if (l1.pos_start === undefined || l2.pos_start == undefined) {
        return false;
      }
      if (l1.pos_start == -1 || l2.pos_start == -1) {
        return false;
      }
      if (l1.pos_start < l2.pos_start) {
        return l1.pos_end > l2.pos_start;
      } {
        return l1.pos_start < l2.pos_end;
      }
    }

    if (node1 === undefined) {
      let lower = lowerBound(view.positionNodeMap, l1.pos_start, undefined, function(a, b) {
        var node = a[b];
        return view.nodePositionMap[node];
      } );
      while (++lower < view.positionNodeMap.length &&
             view.nodePositionMap[view.positionNodeMap[lower]] < l1.pos_end) {
        if (view.positionNodeMap[lower] == node2) {
          return true;
        }
      }
      return false;
    }

    if (node2 === undefined) {
      let lower = lowerBound(view.positionNodeMap, l2.pos_start, undefined, function(a, b) {
        var node = a[b];
        return view.nodePositionMap[node];
      } );
      while (++lower < view.positionNodeMap.length &&
             view.nodePositionMap[view.positionNodeMap[lower]] < l2.pos_end) {
        if (view.positionNodeMap[lower] == node1) {
          return true;
        }
      }
      return false;
    }

    return l1.node_id == l2.node_id;
  }

  setNodePositionMap(map) {
    let view = this;
    view.nodePositionMap = map;
    view.positionNodeMap = [];
    view.sortedPositionList = [];
    let next = 0;
    for (let i in view.nodePositionMap) {
      view.sortedPositionList[next] = Number(view.nodePositionMap[i]);
      view.positionNodeMap[next++] = i;
    }
    view.sortedPositionList = sortUnique(view.sortedPositionList,
                                         function(a,b) { return a - b; });
    this.positionNodeMap.sort(function(a,b) {
      let result = view.nodePositionMap[a] - view.nodePositionMap[b];
      if (result != 0) return result;
      return a - b;
    });
  }

  selectLocations(locations, selected, makeVisible) {
    let view = this;
    for (let l of locations) {
      for (let i = 0; i < view.textListNode.children.length; ++i) {
        let child = view.textListNode.children[i];
        if (child.location != undefined && view.sameLocation(l, child.location)) {
          view.selectCommon(child, selected, makeVisible);
        }
      }
    }
  }

  getRanges(items) {
    let result = [];
    let lastObject = null;
    for (let i of items) {
      if (i.location) {
        let location = i.location;
        let start = -1;
        let end = -1;
        let node_id = -1;
        if (location.node_id !== undefined) {
          node_id = location.node_id;
        }
        if (location.pos_start !== undefined) {
          start = location.pos_start;
          end = location.pos_end;
        } else {
          if (this.nodePositionMap && this.nodePositionMap[node_id]) {
            start = this.nodePositionMap[node_id];
            end = start + 1;
          }
        }
        if (lastObject == null ||
            (lastObject[2] != node_id ||
             lastObject[0] != start ||
             lastObject[1] != end)) {
          lastObject = [start, end, node_id];
          result.push(lastObject);
        }
      }
    }
    return result;
  }

  createFragment(text, style) {
    let view = this;
    let span = document.createElement("SPAN");
    span.onmousedown = function(e) {
      view.mouseDownSpan(span, e);
    }
    if (style != undefined) {
      span.classList.add(style);
    }
    span.innerText = text;
    return span;
  }

  appendFragment(li, fragment) {
    li.appendChild(fragment);
  }

  processLine(line) {
    let view = this;
    let result = [];
    let patternSet = 0;
    while (true) {
      let beforeLine = line;
      for (let pattern of view.patterns[patternSet]) {
        let matches = line.match(pattern[0]);
        if (matches != null) {
          if (matches[0] != '') {
            let style = pattern[1] != null ? pattern[1] : {};
            let text = matches[0];
            if (text != '') {
              let fragment = view.createFragment(matches[0], style.css);
              if (style.link) {
                fragment.classList.add('linkable-text');
                fragment.link = style.link;
              }
              result.push(fragment);
              if (style.location != undefined) {
                let location = style.location(text);
                if (location != undefined) {
                  fragment.location = location;
                }
              }
            }
            line = line.substr(matches[0].length);
          }
          let nextPatternSet = patternSet;
          if (pattern.length > 2) {
            nextPatternSet = pattern[2];
          }
          if (line == "") {
            if (nextPatternSet != -1) {
              throw("illegal parsing state in text-view in patternSet" + patternSet);
            }
            return result;
          }
          patternSet = nextPatternSet;
          break;
        }
      }
      if (beforeLine == line) {
        throw("input not consumed in text-view in patternSet" + patternSet);
      }
    }
  }

  select(s, selected, makeVisible) {
    let view = this;
    view.selection.clear();
    view.selectCommon(s, selected, makeVisible);
  }

  selectCommon(s, selected, makeVisible) {
    let view = this;
    let firstSelect = makeVisible && view.selection.isEmpty();
    if ((typeof s) === 'function') {
      for (let i = 0; i < view.textListNode.children.length; ++i) {
        let child = view.textListNode.children[i];
        if (child.location && s(child.location)) {
          if (firstSelect) {
            makeContainerPosVisible(view.parentNode, child.offsetTop);
            firstSelect = false;
          }
          view.selection.select(child, selected);
        }
      }
    } else if (s.length) {
      for (let i of s) {
        if (firstSelect) {
          makeContainerPosVisible(view.parentNode, i.offsetTop);
          firstSelect = false;
        }
        view.selection.select(i, selected);
      }
    } else {
      if (firstSelect) {
        makeContainerPosVisible(view.parentNode, s.offsetTop);
        firstSelect = false;
      }
      view.selection.select(s, selected);
    }
  }

  mouseDownLine(li, e) {
    let view = this;
    e.stopPropagation();
    if (!e.shiftKey) {
      view.selection.clear();
    }
    if (li.location != undefined) {
      view.selectLocations([li.location], true, false);
    }
  }

  mouseDownSpan(span, e) {
    let view = this;
    if (view.allowSpanSelection) {
      e.stopPropagation();
      if (!e.shiftKey) {
        view.selection.clear();
      }
      select(li, true);
    } else if (span.link) {
      span.link(span.textContent);
      e.stopPropagation();
    }
  }

  processText(text) {
    let view = this;
    let textLines = text.split(/[\n]/);
    let lineNo = 0;
    for (let line of textLines) {
      let li = document.createElement("LI");
      li.onmousedown = function(e) {
        view.mouseDownLine(li, e);
      }
      li.className = "nolinenums";
      li.lineNo = lineNo++;
      let fragments = view.processLine(line);
      for (let fragment of fragments) {
        view.appendFragment(li, fragment);
      }
      let lineLocation = view.lineLocation(li);
      if (lineLocation != undefined) {
        li.location = lineLocation;
      }
      view.textListNode.appendChild(li);
    }
  }

  initializeContent(data, rememberedSelection) {
    let view = this;
    view.clearText();
    view.processText(data);
    var fillerSize = document.documentElement.clientHeight -
        view.textListNode.clientHeight;
    if (fillerSize < 0) {
      fillerSize = 0;
    }
    view.fillerSvgElement.attr("height", fillerSize);
  }

  deleteContent() {
  }

  isScrollable() {
    return true;
  }

  detachSelection() {
    return null;
  }

  lineLocation(li) {
    let view = this;
    for (let i = 0; i < li.children.length; ++i) {
      let fragment = li.children[i];
      if (fragment.location != undefined && !view.allowSpanSelection) {
        return fragment.location;
      }
    }
  }
}
