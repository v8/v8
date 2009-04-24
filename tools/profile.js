// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// Initlialize namespaces
var devtools = devtools || {};
devtools.profiler = devtools.profiler || {};


/**
 * Creates a profile object for processing profiling-related events
 * and calculating function execution times.
 *
 * @constructor
 */
devtools.profiler.Profile = function() {
  this.codeMap_ = new devtools.profiler.CodeMap();
  this.topDownTree_ = new devtools.profiler.CallTree();
  this.bottomUpTree_ = new devtools.profiler.CallTree();
};


/**
 * Returns whether a function with the specified name must be skipped.
 * Should be overriden by subclasses.
 *
 * @param {string} name Function name.
 */
devtools.profiler.Profile.prototype.skipThisFunction = function(name) {
  return false;
};


/**
 * Enum for profiler operations that involve looking up existing
 * code entries.
 *
 * @enum {number}
 */
devtools.profiler.Profile.Operation = {
  MOVE: 0,
  DELETE: 1,
  TICK: 2
};


/**
 * Called whenever the specified operation has failed finding a function
 * containing the specified address. Should be overriden by subclasses.
 * See the devtools.profiler.Profile.Operation enum for the list of
 * possible operations.
 *
 * @param {number} operation Operation.
 * @param {number} addr Address of the unknown code.
 * @param {number} opt_stackPos If an unknown address is encountered
 *     during stack strace processing, specifies a position of the frame
 *     containing the address.
 */
devtools.profiler.Profile.prototype.handleUnknownCode = function(
    operation, addr, opt_stackPos) {
};


/**
 * Registers static (library) code entry.
 *
 * @param {string} name Code entry name.
 * @param {number} startAddr Starting address.
 * @param {number} endAddr Ending address.
 */
devtools.profiler.Profile.prototype.addStaticCode = function(
    name, startAddr, endAddr) {
  var entry = new devtools.profiler.CodeMap.CodeEntry(
      endAddr - startAddr, name);
  this.codeMap_.addStaticCode(startAddr, entry);
  return entry;
};


/**
 * Registers dynamic (JIT-compiled) code entry.
 *
 * @param {string} type Code entry type.
 * @param {string} name Code entry name.
 * @param {number} start Starting address.
 * @param {number} size Code entry size.
 */
devtools.profiler.Profile.prototype.addCode = function(
    type, name, start, size) {
  var entry = new devtools.profiler.Profile.DynamicCodeEntry(size, type, name);
  this.codeMap_.addCode(start, entry);
  return entry;
};


/**
 * Reports about moving of a dynamic code entry.
 *
 * @param {number} from Current code entry address.
 * @param {number} to New code entry address.
 */
devtools.profiler.Profile.prototype.moveCode = function(from, to) {
  try {
    this.codeMap_.moveCode(from, to);
  } catch (e) {
    this.handleUnknownCode(devtools.profiler.Profile.Operation.MOVE, from);
  }
};


/**
 * Reports about deletion of a dynamic code entry.
 *
 * @param {number} start Starting address.
 */
devtools.profiler.Profile.prototype.deleteCode = function(start) {
  try {
    this.codeMap_.deleteCode(start);
  } catch (e) {
    this.handleUnknownCode(devtools.profiler.Profile.Operation.DELETE, start);
  }
};


/**
 * Records a tick event. Stack must contain a sequence of
 * addresses starting with the program counter value.
 *
 * @param {Array<number>} stack Stack sample.
 */
devtools.profiler.Profile.prototype.recordTick = function(stack) {
  var processedStack = this.resolveAndFilterFuncs_(stack);
  this.bottomUpTree_.addPath(processedStack);
  processedStack.reverse();
  this.topDownTree_.addPath(processedStack);
};


/**
 * Translates addresses into function names and filters unneeded
 * functions.
 *
 * @param {Array<number>} stack Stack sample.
 */
devtools.profiler.Profile.prototype.resolveAndFilterFuncs_ = function(stack) {
  var result = [];
  for (var i = 0; i < stack.length; ++i) {
    var entry = this.codeMap_.findEntry(stack[i]);
    if (entry) {
      var name = entry.getName();
      if (!this.skipThisFunction(name)) {
        result.push(name);
      }
    } else {
      this.handleUnknownCode(
          devtools.profiler.Profile.Operation.TICK, stack[i], i);
    }
  }
  return result;
};


/**
 * Returns the root of the top down call graph.
 */
devtools.profiler.Profile.prototype.getTopDownTreeRoot = function() {
  this.topDownTree_.computeTotalWeights();
  return this.topDownTree_.getRoot();
};


/**
 * Returns the root of the bottom up call graph.
 */
devtools.profiler.Profile.prototype.getBottomUpTreeRoot = function() {
  this.bottomUpTree_.computeTotalWeights();
  return this.bottomUpTree_.getRoot();
};


/**
 * Traverses the top down call graph in preorder.
 *
 * @param {function(devtools.profiler.CallTree.Node)} f Visitor function.
 */
devtools.profiler.Profile.prototype.traverseTopDownTree = function(f) {
  this.topDownTree_.traverse(f);
};


/**
 * Traverses the bottom up call graph in preorder.
 *
 * @param {function(devtools.profiler.CallTree.Node)} f Visitor function.
 */
devtools.profiler.Profile.prototype.traverseBottomUpTree = function(f) {
  this.bottomUpTree_.traverse(f);
};


/**
 * Calculates a top down profile starting from the specified node.
 *
 * @param {devtools.profiler.CallTree.Node} opt_root Starting node.
 */
devtools.profiler.Profile.prototype.getTopDownProfile = function(opt_root) {
  if (!opt_root) {
    this.topDownTree_.computeTotalWeights();
    return this.topDownTree_;
  } else {
    throw Error('not implemented');
  }
};


/**
 * Calculates a bottom up profile starting from the specified node.
 *
 * @param {devtools.profiler.CallTree.Node} opt_root Starting node.
 */
devtools.profiler.Profile.prototype.getBottomUpProfile = function(opt_root) {
  if (!opt_root) {
    this.bottomUpTree_.computeTotalWeights();
    return this.bottomUpTree_;
  } else {
    throw Error('not implemented');
  }
};


/**
 * Calculates a flat profile of callees starting from the specified node.
 *
 * @param {devtools.profiler.CallTree.Node} opt_root Starting node.
 */
devtools.profiler.Profile.prototype.getFlatProfile = function(opt_root) {
  var counters = new devtools.profiler.CallTree();
  var precs = {};
  this.topDownTree_.computeTotalWeights();
  this.topDownTree_.traverseInDepth(
    function onEnter(node) {
      if (!(node.label in precs)) {
        precs[node.label] = 0;
      }
      var rec = counters.findOrAddChild(node.label);
      rec.selfWeight += node.selfWeight;
      if (precs[node.label] == 0) {
        rec.totalWeight += node.totalWeight;
      }
      precs[node.label]++;
    },
    function onExit(node) {
      precs[node.label]--;
    },
    opt_root);
  return counters;
};


/**
 * Creates a dynamic code entry.
 *
 * @param {number} size Code size.
 * @param {string} type Code type.
 * @param {string} name Function name.
 * @constructor
 */
devtools.profiler.Profile.DynamicCodeEntry = function(size, type, name) {
  devtools.profiler.CodeMap.CodeEntry.call(this, size, name);
  this.type = type;
};


/**
 * Returns node name.
 */
devtools.profiler.Profile.DynamicCodeEntry.prototype.getName = function() {
  var name = this.name;
  if (name.length == 0) {
    name = '<anonymous>';
  } else if (name.charAt(0) == ' ') {
    // An anonymous function with location: " aaa.js:10".
    name = '<anonymous>' + name;
  }
  return this.type + ': ' + name;
};


/**
 * Constructs a call graph.
 *
 * @constructor
 */
devtools.profiler.CallTree = function() {
  this.root_ = new devtools.profiler.CallTree.Node('');
};


/**
 * @private
 */
devtools.profiler.CallTree.prototype.totalsComputed_ = false;


/**
 * Returns the tree root.
 */
devtools.profiler.CallTree.prototype.getRoot = function() {
  return this.root_;
};


/**
 * Adds the specified call path, constructing nodes as necessary.
 *
 * @param {Array<string>} path Call path.
 */
devtools.profiler.CallTree.prototype.addPath = function(path) {
  if (path.length == 0) {
    return;
  }
  var curr = this.root_;
  for (var i = 0; i < path.length; ++i) {
    curr = curr.findOrAddChild(path[i]);
  }
  curr.selfWeight++;
  this.totalsComputed_ = false;
};


/**
 * Finds an immediate child of the specified parent with the specified
 * label, creates a child node if necessary. If a parent node isn't
 * specified, uses tree root.
 *
 * @param {string} label Child node label.
 */
devtools.profiler.CallTree.prototype.findOrAddChild = function(
    label, opt_parent) {
  var parent = opt_parent || this.root_;
  return parent.findOrAddChild(label);
};


/**
 * Computes total weights in the call graph.
 */
devtools.profiler.CallTree.prototype.computeTotalWeights = function() {
  if (this.totalsComputed_) {
    return;
  }
  this.root_.computeTotalWeight();
  this.totalsComputed_ = true;
};


/**
 * Traverses the call graph in preorder. This function can be used for
 * building optionally modified tree clones. This is the boilerplate code
 * for this scenario:
 *
 * callTree.traverse(function(node, parentClone) {
 *   var nodeClone = cloneNode(node);
 *   if (parentClone)
 *     parentClone.addChild(nodeClone);
 *   return nodeClone;
 * });
 *
 * @param {function(devtools.profiler.CallTree.Node, *)} f Visitor function.
 *    The second parameter is the result of calling 'f' on the parent node.
 * @param {devtools.profiler.CallTree.Node} opt_start Starting node.
 */
devtools.profiler.CallTree.prototype.traverse = function(f, opt_start) {
  var pairsToProcess = [{node: opt_start || this.root_, param: null}];
  while (pairsToProcess.length > 0) {
    var pair = pairsToProcess.shift();
    var node = pair.node;
    var newParam = f(node, pair.param);
    node.forEachChild(
      function (child) { pairsToProcess.push({node: child, param: newParam}); }
    );
  }
};


/**
 * Performs an indepth call graph traversal.
 *
 * @param {function(devtools.profiler.CallTree.Node)} enter A function called
 *     prior to visiting node's children.
 * @param {function(devtools.profiler.CallTree.Node)} exit A function called
 *     after visiting node's children.
 * @param {devtools.profiler.CallTree.Node} opt_start Starting node.
 */
devtools.profiler.CallTree.prototype.traverseInDepth = function(
    enter, exit, opt_start) {
  function traverse(node) {
    enter(node);
    node.forEachChild(traverse);
    exit(node);
  }
  traverse(opt_start || this.root_);
};


/**
 * Constructs a call graph node.
 *
 * @param {string} label Node label.
 * @param {devtools.profiler.CallTree.Node} opt_parent Node parent.
 */
devtools.profiler.CallTree.Node = function(label, opt_parent) {
  this.label = label;
  this.parent = opt_parent;
  this.children = {};
};


/**
 * Node self weight (how many times this node was the last node in
 * a call path).
 * @type {number}
 */
devtools.profiler.CallTree.Node.prototype.selfWeight = 0;


/**
 * Node total weight (includes weights of all children).
 * @type {number}
 */
devtools.profiler.CallTree.Node.prototype.totalWeight = 0;


/**
 * Adds a child node.
 *
 * @param {string} label Child node label.
 */
devtools.profiler.CallTree.Node.prototype.addChild = function(label) {
  var child = new devtools.profiler.CallTree.Node(label, this);
  this.children[label] = child;
  return child;
};


/**
 * Computes node's total weight.
 */
devtools.profiler.CallTree.Node.prototype.computeTotalWeight =
    function() {
  var totalWeight = this.selfWeight;
  this.forEachChild(function(child) {
      totalWeight += child.computeTotalWeight(); });
  return this.totalWeight = totalWeight;
};


/**
 * Returns all node's children as an array.
 */
devtools.profiler.CallTree.Node.prototype.exportChildren = function() {
  var result = [];
  this.forEachChild(function (node) { result.push(node); });
  return result;
};


/**
 * Finds an immediate child with the specified label.
 *
 * @param {string} label Child node label.
 */
devtools.profiler.CallTree.Node.prototype.findChild = function(label) {
  return this.children[label] || null;
};


/**
 * Finds an immediate child with the specified label, creates a child
 * node if necessary.
 *
 * @param {string} label Child node label.
 */
devtools.profiler.CallTree.Node.prototype.findOrAddChild = function(
    label) {
  return this.findChild(label) || this.addChild(label);
};


/**
 * Calls the specified function for every child.
 *
 * @param {function(devtools.profiler.CallTree.Node)} f Visitor function.
 */
devtools.profiler.CallTree.Node.prototype.forEachChild = function(f) {
  for (var c in this.children) {
    f(this.children[c]);
  }
};


/**
 * Walks up from the current node up to the call tree root.
 *
 * @param {function(devtools.profiler.CallTree.Node)} f Visitor function.
 */
devtools.profiler.CallTree.Node.prototype.walkUpToRoot = function(f) {
  for (var curr = this; curr != null; curr = curr.parent) {
    f(curr);
  }
};


/**
 * Tries to find a node with the specified path.
 *
 * @param {Array<string>} labels The path.
 * @param {function(devtools.profiler.CallTree.Node)} opt_f Visitor function.
 */
devtools.profiler.CallTree.Node.prototype.descendToChild = function(
    labels, opt_f) {
  for (var pos = 0, curr = this; pos < labels.length && curr != null; pos++) {
    var child = curr.findChild(labels[pos]);
    if (opt_f) {
      opt_f(child, pos);
    }
    curr = child;
  }
  return curr;
};
