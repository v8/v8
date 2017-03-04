// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict"

let codeKinds = [
    "UNKNOWN",
    "CPPCOMP",
    "CPPGC",
    "CPPEXT",
    "CPP",
    "LIB",
    "IC",
    "BC",
    "STUB",
    "BUILTIN",
    "REGEXP",
    "JSOPT",
    "JSUNOPT"
];

function resolveCodeKind(code) {
  if (!code || !code.type) {
    return "UNKNOWN";
  } else if (code.type === "CPP") {
    return "CPP";
  } else if (code.type === "SHARED_LIB") {
    return "LIB";
  } else if (code.type === "CODE") {
    if (code.kind === "LoadIC" ||
        code.kind === "StoreIC" ||
        code.kind === "KeyedStoreIC" ||
        code.kind === "KeyedLoadIC" ||
        code.kind === "LoadGlobalIC" ||
        code.kind === "Handler") {
      return "IC";
    } else if (code.kind === "BytecodeHandler") {
      return "BC";
    } else if (code.kind === "Stub") {
      return "STUB";
    } else if (code.kind === "Builtin") {
      return "BUILTIN";
    } else if (code.kind === "RegExp") {
      return "REGEXP";
    }
    console.log("Unknown CODE: '" + code.kind + "'.");
    return "CODE";
  } else if (code.type === "JS") {
    if (code.kind === "Builtin") {
      return "JSUNOPT";
    } else if (code.kind === "Opt") {
      return "JSOPT";
    } else if (code.kind === "Unopt") {
      return "JSUNOPT";
    }
  }
  console.log("Unknown code type '" + type + "'.");
}

function resolveCodeKindAndVmState(code, vmState) {
  let kind = resolveCodeKind(code);
  if (kind === "CPP") {
    if (vmState === 1) {
      kind = "CPPGC";
    } else if (vmState === 2) {
      kind = "CPPCOMP";
    } else if (vmState === 4) {
      kind = "CPPEXT";
    }
  }
  return kind;
}

function createNodeFromStackEntry(code) {
  let name = code ? code.name : "UNKNOWN";

  return { name, type : resolveCodeKind(code),
           children : [], ownTicks : 0, ticks : 0 };
}

function addStackToTree(file, stack, tree, filter, ascending, start) {
  if (start === undefined) {
    start = ascending ? 0 : stack.length - 2;
  }
  tree.ticks++;
  for (let i = start;
       ascending ? (i < stack.length) : (i >= 0);
       i += ascending ? 2 : -2) {
    let codeId = stack[i];
    let code = codeId >= 0 ? file.code[codeId] : undefined;
    if (filter) {
      let type = code ? code.type : undefined;
      let kind = code ? code.kind : undefined;
      if (!filter(type, kind)) continue;
    }

    // For JavaScript function, pretend there is one instance of optimized
    // function and one instance of unoptimized function per SFI.
    let type = resolveCodeKind(code);
    let childId;
    if (type === "JSOPT") {
      childId = code.func * 4 + 1;
    } else if (type === "JSUNOPT") {
      childId = code.func * 4 + 2;
    } else {
      childId = codeId * 4;
    }
    let child = tree.children[childId];
    if (!child) {
      child = createNodeFromStackEntry(code);
      tree.children[childId] = child;
    }
    child.ticks++;
    tree = child;
  }
  tree.ownTicks++;
}

function createEmptyNode(name) {
  return {
      name : name,
      type : "CAT",
      children : [],
      ownTicks : 0,
      ticks : 0
  };
}

class PlainCallTreeProcessor {
  constructor(filter, isBottomUp) {
    this.filter = filter;
    this.tree = createEmptyNode("root");
    this.isBottomUp = isBottomUp;
  }

  addStack(file, timestamp, vmState, stack) {
    addStackToTree(file, stack, this.tree, this.filter, this.isBottomUp);
  }
}

class CategorizedCallTreeProcessor {
  constructor(filter, isBottomUp) {
    this.filter = filter;
    let root = createEmptyNode("root");
    let categories = {};
    function addCategory(name, types) {
      let n = createEmptyNode(name);
      for (let i = 0; i < types.length; i++) {
        categories[types[i]] = n;
      }
      root.children.push(n);
    }
    addCategory("JS Optimized", [ "JSOPT" ]);
    addCategory("JS Unoptimized", [ "JSUNOPT", "BC" ]);
    addCategory("IC", [ "IC" ]);
    addCategory("RegExp", [ "REGEXP" ]);
    addCategory("Other generated", [ "STUB", "BUILTIN" ]);
    addCategory("C++", [ "CPP", "LIB" ]);
    addCategory("C++/GC", [ "CPPGC" ]);
    addCategory("C++/Compiler", [ "CPPCOMP" ]);
    addCategory("C++/External", [ "CPPEXT" ]);
    addCategory("Unknown", [ "UNKNOWN" ]);

    this.tree = root;
    this.categories = categories;
    this.isBottomUp = isBottomUp;
  }

  addStack(file, timestamp, vmState, stack) {
    if (stack.length === 0) return;
    let codeId = stack[0];
    let code = codeId >= 0 ? file.code[codeId] : undefined;
    let kind = resolveCodeKindAndVmState(code, vmState);
    let node = this.categories[kind];

    this.tree.ticks++;

    console.assert(node);

    addStackToTree(file, stack, node, this.filter, this.isBottomUp);
  }
}

class FunctionListTree {
  constructor(filter) {
    this.tree = { name : "root", children : [], ownTicks : 0, ticks : 0 };
    this.codeVisited = [];
    this.filter = filter;
  }

  addStack(file, timestamp, vmState, stack) {
    this.tree.ticks++;
    let child = null;
    for (let i = stack.length - 2; i >= 0; i -= 2) {
      let codeId = stack[i];
      if (codeId < 0 || this.codeVisited[codeId]) continue;

      let code = codeId >= 0 ? file.code[codeId] : undefined;
      if (this.filter) {
        let type = code ? code.type : undefined;
        let kind = code ? code.kind : undefined;
        if (!this.filter(type, kind)) continue;
      }
      child = this.tree.children[codeId];
      if (!child) {
        child = createNodeFromStackEntry(code);
        this.tree.children[codeId] = child;
      }
      child.ticks++;
      this.codeVisited[codeId] = true;
    }
    if (child) {
      child.ownTicks++;
    }

    for (let i = 0; i < stack.length; i += 2) {
      let codeId = stack[i];
      if (codeId >= 0) this.codeVisited[codeId] = false;
    }
  }
}


class CategorySampler {
  constructor(file, bucketCount) {
    this.bucketCount = bucketCount;

    this.firstTime = file.ticks[0].tm;
    let lastTime = file.ticks[file.ticks.length - 1].tm;
    this.step = (lastTime - this.firstTime) / bucketCount;

    this.buckets = [];
    let bucket = {};
    for (let i = 0; i < codeKinds.length; i++) {
      bucket[codeKinds[i]] = 0;
    }
    for (let i = 0; i < bucketCount; i++) {
      this.buckets.push(Object.assign({ total : 0 }, bucket));
    }
  }

  addStack(file, timestamp, vmState, stack) {
    let i = Math.floor((timestamp - this.firstTime) / this.step);
    if (i == this.buckets.length) i--;
    console.assert(i >= 0 && i < this.buckets.length);

    let bucket = this.buckets[i];
    bucket.total++;

    let codeId = (stack.length > 0) ? stack[0] : -1;
    let code = codeId >= 0 ? file.code[codeId] : undefined;
    let kind = resolveCodeKindAndVmState(code, vmState);
    bucket[kind]++;
  }
}

// Generates a tree out of a ticks sequence.
// {file} is the JSON files with the ticks and code objects.
// {startTime}, {endTime} is the interval.
// {tree} is the processor of stacks.
function generateTree(
    file, startTime, endTime, tree) {
  let ticks = file.ticks;
  let i = 0;
  while (i < ticks.length && ticks[i].tm < startTime) {
    i++;
  }

  let tickCount = 0;
  while (i < ticks.length && ticks[i].tm < endTime) {
    tree.addStack(file, ticks[i].tm, ticks[i].vm, ticks[i].s);
    i++;
    tickCount++;
  }

  return tickCount;
}
