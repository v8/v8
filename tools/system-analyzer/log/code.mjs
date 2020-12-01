// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {LogEntry} from './log.mjs';

export class DeoptLogEntry extends LogEntry {
  constructor(
      type, time, deoptReason, deoptLocation, scriptOffset, instructionStart,
      codeSize, inliningId) {
    super(type, time);
    this._deoptReason = deoptReason;
    this._deoptLocation = deoptLocation;
    this._scriptOffset = scriptOffset;
    this._instructionStart = instructionStart;
    this._codeSize = codeSize;
    this._inliningId = inliningId;
  }

  toString() {
    return `Deopt(${this.type})${this._deoptReason}: ${this._deoptLocation}`;
  }
}

export class CodeLogEntry extends LogEntry {
  constructor(type, time, kind, entry) {
    super(type, time);
    this._kind = kind;
    this._entry = entry;
  }

  toString() {
    return `Code(${this.type}): ${this._entry.toString()}`;
  }

  get disassemble() {
    return this._entry?.source?.disassemble;
  }
}
