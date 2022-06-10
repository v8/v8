// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { Phase, PhaseType } from "./phase";

export class InstructionsPhase extends Phase {
  // Maps node ids to instruction ranges.
  nodeIdToInstructionRange?: Array<[number, number]>;
  // Maps block ids to instruction ranges.
  blockIdToInstructionRange?: Array<[number, number]>;
  instructionOffsetToPCOffset?: Array<[number, number]>;
  codeOffsetsInfo?: CodeOffsetsInfo;

  // Maps instruction numbers to PC offsets.
  instructionToPCOffset: Array<TurbolizerInstructionStartInfo>;
  // Maps PC offsets to instructions.
  pcOffsetToInstructions: Map<number, Array<number>>;
  pcOffsets: Array<number>;

  constructor(name: string, nodeIdToInstructionRange?: Array<[number, number]>,
              blockIdToInstructionRange?: Array<[number, number]>,
              codeOffsetsInfo?: CodeOffsetsInfo) {
    super(name, PhaseType.Instructions);
    this.nodeIdToInstructionRange = nodeIdToInstructionRange ?? new Array<[number, number]>();
    this.blockIdToInstructionRange = blockIdToInstructionRange ?? new Array<[number, number]>();
    this.codeOffsetsInfo = codeOffsetsInfo;

    this.instructionToPCOffset = new Array<TurbolizerInstructionStartInfo>();
    this.pcOffsetToInstructions = new Map<number, Array<number>>();
    this.pcOffsets = new Array<number>();
  }

  public parseNodeIdToInstructionRangeFromJSON(nodeIdToInstructionJson): void {
    if (!nodeIdToInstructionJson) return;
    for (const [nodeId, range] of Object.entries<[number, number]>(nodeIdToInstructionJson)) {
      this.nodeIdToInstructionRange[nodeId] = range;
    }
  }

  public parseBlockIdToInstructionRangeFromJSON(blockIdToInstructionRangeJson): void {
    if (!blockIdToInstructionRangeJson) return;
    for (const [blockId, range] of
      Object.entries<[number, number]>(blockIdToInstructionRangeJson)) {
      this.blockIdToInstructionRange[blockId] = range;
    }
  }

  public parseInstructionOffsetToPCOffsetFromJSON(instructionOffsetToPCOffsetJson): void {
    if (!instructionOffsetToPCOffsetJson) return;
    for (const [instruction, numberOrInfo] of Object.entries<number |
      TurbolizerInstructionStartInfo>(instructionOffsetToPCOffsetJson)) {
      let info: TurbolizerInstructionStartInfo = null;
      if (typeof numberOrInfo === "number") {
        info = new TurbolizerInstructionStartInfo(numberOrInfo, numberOrInfo, numberOrInfo);
      } else {
        info = new TurbolizerInstructionStartInfo(numberOrInfo.gap, numberOrInfo.arch,
          numberOrInfo.condition);
      }
      this.instructionToPCOffset[instruction] = info;
      if (!this.pcOffsetToInstructions.has(info.gap)) {
        this.pcOffsetToInstructions.set(info.gap, new Array<number>());
      }
      this.pcOffsetToInstructions.get(info.gap).push(Number(instruction));
    }
    this.pcOffsets = Array.from(this.pcOffsetToInstructions.keys()).sort((a, b) => b - a);
  }

  public parseCodeOffsetsInfoFromJSON(codeOffsetsInfoJson: CodeOffsetsInfo): void {
    if (!codeOffsetsInfoJson) return;
    this.codeOffsetsInfo = new CodeOffsetsInfo(codeOffsetsInfoJson.codeStartRegisterCheck,
      codeOffsetsInfoJson.deoptCheck, codeOffsetsInfoJson.initPoison,
      codeOffsetsInfoJson.blocksStart, codeOffsetsInfoJson.outOfLineCode,
      codeOffsetsInfoJson.deoptimizationExits, codeOffsetsInfoJson.pools,
      codeOffsetsInfoJson.jumpTables);
  }
}

export class CodeOffsetsInfo {
  codeStartRegisterCheck: number;
  deoptCheck: number;
  initPoison: number;
  blocksStart: number;
  outOfLineCode: number;
  deoptimizationExits: number;
  pools: number;
  jumpTables: number;

  constructor(codeStartRegisterCheck: number, deoptCheck: number, initPoison: number,
              blocksStart: number, outOfLineCode: number, deoptimizationExits: number,
              pools: number, jumpTables: number) {
    this.codeStartRegisterCheck = codeStartRegisterCheck;
    this.deoptCheck = deoptCheck;
    this.initPoison = initPoison;
    this.blocksStart = blocksStart;
    this.outOfLineCode = outOfLineCode;
    this.deoptimizationExits = deoptimizationExits;
    this.pools = pools;
    this.jumpTables = jumpTables;
  }
}

export class TurbolizerInstructionStartInfo {
  gap: number;
  arch: number;
  condition: number;

  constructor(gap: number, arch: number, condition: number) {
    this.gap = gap;
    this.arch = arch;
    this.condition = condition;
  }
}
