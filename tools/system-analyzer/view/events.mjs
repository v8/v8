// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export class SelectionEvent extends CustomEvent {
  // TODO: turn into static class fields once Safari supports it.
  static get name() {
    return 'showentries';
  }
  constructor(entries) {
    super(SelectionEvent.name, {bubbles: true, composed: true});
    if (!Array.isArray(entries) || entries.length == 0) {
      throw new Error('No valid entries selected!');
    }
    this.entries = entries;
  }
}

export class FocusEvent extends CustomEvent {
  static get name() {
    return 'showentrydetail';
  }
  constructor(entry) {
    super(FocusEvent.name, {bubbles: true, composed: true});
    this.entry = entry;
  }
}

export class SelectTimeEvent extends CustomEvent {
  static get name() {
    return 'timerangeselect';
  }
  constructor(start = 0, end = Infinity) {
    super(SelectTimeEvent.name, {bubbles: true, composed: true});
    this.start = start;
    this.end = end;
  }
}

export class SynchronizeSelectionEvent extends CustomEvent {
  static get name() {
    return 'syncselection';
  }
  constructor(start, end) {
    super(SynchronizeSelectionEvent.name, {bubbles: true, composed: true});
    this.start = start;
    this.end = end;
  }
}

export class ToolTipEvent extends CustomEvent {
  static get name() {
    return 'showtooltip';
  }

  constructor(content, positionOrTargetNode) {
    super(ToolTipEvent.name, {bubbles: true, composed: true});
    this._content = content;
    if (!positionOrTargetNode && !node) {
      throw Error('Either provide a valid position or targetNode');
    }
    this._positionOrTargetNode = positionOrTargetNode;
  }

  get content() {
    return this._content;
  }

  get positionOrTargetNode() {
    return this._positionOrTargetNode;
  }
}
