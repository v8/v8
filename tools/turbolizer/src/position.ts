// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export class InliningPosition {
  sourceId: number;
  inliningPosition: SourcePosition;

  constructor(sourceId: number, inliningPosition: SourcePosition) {
    this.sourceId = sourceId;
    this.inliningPosition = inliningPosition;
  }
}

export class SourcePosition {
  scriptOffset: number;
  inliningId: number;

  constructor(scriptOffset: number, inliningId: number) {
    this.scriptOffset = scriptOffset;
    this.inliningId = inliningId;
  }

  public lessOrEquals(other: SourcePosition): number {
    if (this.inliningId == other.inliningId) {
      return this.scriptOffset - other.scriptOffset;
    }
    return this.inliningId - other.inliningId;
  }

  public equals(other: SourcePosition): boolean {
    if (this.scriptOffset != other.scriptOffset) return false;
    return this.inliningId == other.inliningId;
  }

  public isValid(): boolean {
    return typeof this.scriptOffset !== undefined && typeof this.inliningId !== undefined;
  }

  public toString = (): string => `SP:${this.inliningId}:${this.scriptOffset}`;
}

export class BytecodePosition {
  bytecodePosition: number;

  constructor(bytecodePosition: number) {
    this.bytecodePosition = bytecodePosition;
  }

  public isValid(): boolean {
    return typeof this.bytecodePosition !== undefined;
  }

  public toString = (): string => `BCP:${this.bytecodePosition}`;
}
