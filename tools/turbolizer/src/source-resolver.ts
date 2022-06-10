// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { anyToString, camelize, sortUnique } from "./common/util";
import { PhaseType } from "./phases/phase";
import { GraphPhase } from "./phases/graph-phase";
import { DisassemblyPhase } from "./phases/disassembly-phase";
import { BytecodePosition, InliningPosition, SourcePosition } from "./position";
import { CodeOffsetsInfo, InstructionsPhase,
  TurbolizerInstructionStartInfo } from "./phases/instructions-phase";
import { SchedulePhase } from "./phases/schedule-phase";
import { SequencePhase } from "./phases/sequence-phase";
import { BytecodeOrigin } from "./origin";
import { Source } from "./source";
import { NodeLabel } from "./node-label";

function sourcePositionLe(a, b) {
  if (a.inliningId == b.inliningId) {
    return a.scriptOffset - b.scriptOffset;
  }
  return a.inliningId - b.inliningId;
}

function sourcePositionEq(a, b) {
  return a.inliningId == b.inliningId &&
    a.scriptOffset == b.scriptOffset;
}

export function sourcePositionToStringKey(sourcePosition: GenericPosition): string {
  if (!sourcePosition) return "undefined";
  if ('inliningId' in sourcePosition && 'scriptOffset' in sourcePosition) {
    return "SP:" + sourcePosition.inliningId + ":" + sourcePosition.scriptOffset;
  }
  if (sourcePosition.bytecodePosition) {
    return "BCP:" + sourcePosition.bytecodePosition;
  }
  return "undefined";
}

export function sourcePositionValid(l) {
  return (typeof l.scriptOffset !== undefined
    && typeof l.inliningId !== undefined) || typeof l.bytecodePosition != undefined;
}

type GenericPosition = SourcePosition | BytecodePosition;
type GenericPhase = GraphPhase | DisassemblyPhase | InstructionsPhase
  | SchedulePhase | SequencePhase;

export class Interval {
  start: number;
  end: number;

  constructor(numbers: [number, number]) {
    this.start = numbers[0];
    this.end = numbers[1];
  }
}

export class SourceResolver {
  nodePositionMap: Array<GenericPosition>;
  sources: Array<Source>;
  inlinings: Array<InliningPosition>;
  inliningsMap: Map<string, InliningPosition>;
  positionToNodes: Map<string, Array<string>>;
  phases: Array<GenericPhase>;
  phaseNames: Map<string, number>;
  disassemblyPhase: DisassemblyPhase;
  instructionsPhase: InstructionsPhase;
  linePositionMap: Map<string, Array<GenericPosition>>;
  nodeIdToInstructionRange: Array<[number, number]>;
  blockIdToInstructionRange: Array<[number, number]>;
  instructionToPCOffset: Array<TurbolizerInstructionStartInfo>;
  pcOffsetToInstructions: Map<number, Array<number>>;
  pcOffsets: Array<number>;
  blockIdToPCOffset: Array<number>;
  blockStartPCtoBlockIds: Map<number, Array<number>>;
  codeOffsetsInfo: CodeOffsetsInfo;

  constructor() {
    // Maps node ids to source positions.
    this.nodePositionMap = [];
    // Maps source ids to source objects.
    this.sources = [];
    // Maps inlining ids to inlining objects.
    this.inlinings = [];
    // Maps source position keys to inlinings.
    this.inliningsMap = new Map();
    // Maps source position keys to node ids.
    this.positionToNodes = new Map();
    // Maps phase ids to phases.
    this.phases = [];
    // Maps phase names to phaseIds.
    this.phaseNames = new Map();
    // The disassembly phase is stored separately.
    this.disassemblyPhase = undefined;
    // Maps line numbers to source positions
    this.linePositionMap = new Map();
    // Maps node ids to instruction ranges.
    this.nodeIdToInstructionRange = [];
    // Maps block ids to instruction ranges.
    this.blockIdToInstructionRange = [];
    // Maps instruction numbers to PC offsets.
    this.instructionToPCOffset = [];
    // Maps PC offsets to instructions.
    this.pcOffsetToInstructions = new Map();
    this.pcOffsets = [];
    this.blockIdToPCOffset = [];
    this.blockStartPCtoBlockIds = new Map();
    this.codeOffsetsInfo = null;
  }

  getBlockIdsForOffset(offset): Array<number> {
    return this.blockStartPCtoBlockIds.get(offset);
  }

  hasBlockStartInfo() {
    return this.blockIdToPCOffset.length > 0;
  }

  setSources(sources, mainBackup) {
    if (sources) {
      for (const [sourceId, source] of Object.entries(sources)) {
        this.sources[sourceId] = source;
        this.sources[sourceId].sourcePositions = [];
      }
    }
    // This is a fallback if the JSON is incomplete (e.g. due to compiler crash).
    if (!this.sources[-1]) {
      this.sources[-1] = mainBackup;
      this.sources[-1].sourcePositions = [];
    }
  }

  setInlinings(inlinings) {
    if (inlinings) {
      for (const [inliningId, inlining] of Object.entries<InliningPosition>(inlinings)) {
        this.inlinings[inliningId] = inlining;
        this.inliningsMap.set(sourcePositionToStringKey(inlining.inliningPosition), inlining);
      }
    }
    // This is a default entry for the script itself that helps
    // keep other code more uniform.
    this.inlinings[-1] = { sourceId: -1, inliningPosition: null };
  }

  setNodePositionMap(map) {
    if (!map) return;
    if (typeof map[0] != 'object') {
      const alternativeMap = {};
      for (const [nodeId, scriptOffset] of Object.entries<number>(map)) {
        alternativeMap[nodeId] = { scriptOffset: scriptOffset, inliningId: -1 };
      }
      map = alternativeMap;
    }

    for (const [nodeId, sourcePosition] of Object.entries<SourcePosition>(map)) {
      if (sourcePosition == undefined) {
        console.log("Warning: undefined source position ", sourcePosition, " for nodeId ", nodeId);
      }
      const inliningId = sourcePosition.inliningId;
      const inlining = this.inlinings[inliningId];
      if (inlining) {
        const sourceId = inlining.sourceId;
        this.sources[sourceId].sourcePositions.push(sourcePosition);
      }
      this.nodePositionMap[nodeId] = sourcePosition;
      const key = sourcePositionToStringKey(sourcePosition);
      if (!this.positionToNodes.has(key)) {
        this.positionToNodes.set(key, []);
      }
      this.positionToNodes.get(key).push(nodeId);
    }
    for (const [, source] of Object.entries(this.sources)) {
      source.sourcePositions = sortUnique(source.sourcePositions,
        sourcePositionLe, sourcePositionEq);
    }
  }

  sourcePositionsToNodeIds(sourcePositions) {
    const nodeIds = new Set<string>();
    for (const sp of sourcePositions) {
      const key = sourcePositionToStringKey(sp);
      const nodeIdsForPosition = this.positionToNodes.get(key);
      if (!nodeIdsForPosition) continue;
      for (const nodeId of nodeIdsForPosition) {
        nodeIds.add(nodeId);
      }
    }
    return nodeIds;
  }

  nodeIdsToSourcePositions(nodeIds): Array<GenericPosition> {
    const sourcePositions = new Map();
    for (const nodeId of nodeIds) {
      const sp = this.nodePositionMap[nodeId];
      const key = sourcePositionToStringKey(sp);
      sourcePositions.set(key, sp);
    }
    const sourcePositionArray = [];
    for (const sp of sourcePositions.values()) {
      sourcePositionArray.push(sp);
    }
    return sourcePositionArray;
  }

  forEachSource(f: (value: Source, index: number, array: Array<Source>) => void) {
    this.sources.forEach(f);
  }

  translateToSourceId(sourceId: number, location?: SourcePosition) {
    for (const position of this.getInlineStack(location)) {
      const inlining = this.inlinings[position.inliningId];
      if (!inlining) continue;
      if (inlining.sourceId == sourceId) {
        return position;
      }
    }
    return location;
  }

  addInliningPositions(sourcePosition: GenericPosition, locations: Array<SourcePosition>) {
    const inlining = this.inliningsMap.get(sourcePositionToStringKey(sourcePosition));
    if (!inlining) return;
    const sourceId = inlining.sourceId;
    const source = this.sources[sourceId];
    for (const sp of source.sourcePositions) {
      locations.push(sp);
      this.addInliningPositions(sp, locations);
    }
  }

  getInliningForPosition(sourcePosition: GenericPosition) {
    return this.inliningsMap.get(sourcePositionToStringKey(sourcePosition));
  }

  getSource(sourceId: number) {
    return this.sources[sourceId];
  }

  getSourceName(sourceId: number) {
    const source = this.sources[sourceId];
    return `${source.sourceName}:${source.functionName}`;
  }

  sourcePositionFor(sourceId: number, scriptOffset: number) {
    if (!this.sources[sourceId]) {
      return null;
    }
    const list = this.sources[sourceId].sourcePositions;
    for (let i = 0; i < list.length; i++) {
      const sourcePosition = list[i];
      const position = sourcePosition.scriptOffset;
      const nextPosition = list[Math.min(i + 1, list.length - 1)].scriptOffset;
      if ((position <= scriptOffset && scriptOffset < nextPosition)) {
        return sourcePosition;
      }
    }
    return null;
  }

  sourcePositionsInRange(sourceId: number, start: number, end: number) {
    if (!this.sources[sourceId]) return [];
    const res = [];
    const list = this.sources[sourceId].sourcePositions;
    for (const sourcePosition of list) {
      if (start <= sourcePosition.scriptOffset && sourcePosition.scriptOffset < end) {
        res.push(sourcePosition);
      }
    }
    return res;
  }

  getInlineStack(sourcePosition?: SourcePosition) {
    if (!sourcePosition) return [];

    const inliningStack = [];
    let cur = sourcePosition;
    while (cur && cur.inliningId != -1) {
      inliningStack.push(cur);
      const inlining = this.inlinings[cur.inliningId];
      if (!inlining) {
        break;
      }
      cur = inlining.inliningPosition;
    }
    if (cur && cur.inliningId == -1) {
      inliningStack.push(cur);
    }
    return inliningStack;
  }

  private recordOrigins(graphPhase: GraphPhase): void {
    if (graphPhase.type !== PhaseType.Graph) return;
    for (const node of graphPhase.data.nodes) {
      graphPhase.highestNodeId = Math.max(graphPhase.highestNodeId, node.id);
      const origin = node.nodeLabel.origin;
      const isBytecode = origin instanceof BytecodeOrigin;
      if (isBytecode) {
        const position = new BytecodePosition(origin.bytecodePosition);
        this.nodePositionMap[node.id] = position;
        const key = position.toString();
        if (!this.positionToNodes.has(key)) {
          this.positionToNodes.set(key, []);
        }
        const nodes = this.positionToNodes.get(key);
        const identifier = node.identifier();
        if (!nodes.includes(identifier)) nodes.push(identifier);
      }
    }
  }

  getInstruction(nodeId: number): [number, number] {
    const X = this.nodeIdToInstructionRange[nodeId];
    if (X === undefined) return [-1, -1];
    return X;
  }

  getInstructionRangeForBlock(blockId: number): [number, number] {
    const X = this.blockIdToInstructionRange[blockId];
    if (X === undefined) return [-1, -1];
    return X;
  }

  hasPCOffsets() {
    return this.pcOffsetToInstructions.size > 0;
  }

  getKeyPcOffset(offset: number): number {
    if (this.pcOffsets.length === 0) return -1;
    for (const key of this.pcOffsets) {
      if (key <= offset) {
        return key;
      }
    }
    return -1;
  }

  getInstructionKindForPCOffset(offset: number) {
    if (this.codeOffsetsInfo) {
      if (offset >= this.codeOffsetsInfo.deoptimizationExits) {
        if (offset >= this.codeOffsetsInfo.pools) {
          return "pools";
        } else if (offset >= this.codeOffsetsInfo.jumpTables) {
          return "jump-tables";
        } else {
          return "deoptimization-exits";
        }
      }
      if (offset < this.codeOffsetsInfo.deoptCheck) {
        return "code-start-register";
      } else if (offset < this.codeOffsetsInfo.initPoison) {
        return "deopt-check";
      } else if (offset < this.codeOffsetsInfo.blocksStart) {
        return "init-poison";
      }
    }
    const keyOffset = this.getKeyPcOffset(offset);
    if (keyOffset != -1) {
      const infos = this.pcOffsetToInstructions.get(keyOffset).map(instrId => this.instructionToPCOffset[instrId]).filter(info => info.gap != info.condition);
      if (infos.length > 0) {
        const info = infos[0];
        if (!info || info.gap == info.condition) return "unknown";
        if (offset < info.arch) return "gap";
        if (offset < info.condition) return "arch";
        return "condition";
      }
    }
    return "unknown";
  }

  instructionKindToReadableName(instructionKind) {
    switch (instructionKind) {
      case "code-start-register": return "Check code register for right value";
      case "deopt-check": return "Check if function was marked for deoptimization";
      case "init-poison": return "Initialization of poison register";
      case "gap": return "Instruction implementing a gap move";
      case "arch": return "Instruction implementing the actual machine operation";
      case "condition": return "Code implementing conditional after instruction";
      case "pools": return "Data in a pool (e.g. constant pool)";
      case "jump-tables": return "Part of a jump table";
      case "deoptimization-exits": return "Jump to deoptimization exit";
    }
    return null;
  }

  instructionRangeToKeyPcOffsets([start, end]: [number, number]): Array<TurbolizerInstructionStartInfo> {
    if (start == end) return [this.instructionToPCOffset[start]];
    return this.instructionToPCOffset.slice(start, end);
  }

  instructionToPcOffsets(instr: number): TurbolizerInstructionStartInfo {
    return this.instructionToPCOffset[instr];
  }

  instructionsToKeyPcOffsets(instructionIds: Iterable<number>): Array<number> {
    const keyPcOffsets = [];
    for (const instructionId of instructionIds) {
      const pcOffset = this.instructionToPCOffset[instructionId];
      if (pcOffset !== undefined) keyPcOffsets.push(pcOffset.gap);
    }
    return keyPcOffsets;
  }

  nodesToKeyPcOffsets(nodes) {
    let offsets = [];
    for (const node of nodes) {
      const range = this.nodeIdToInstructionRange[node];
      if (!range) continue;
      offsets = offsets.concat(this.instructionRangeToKeyPcOffsets(range));
    }
    return offsets;
  }

  nodesForPCOffset(offset: number): [Array<string>, Array<string>] {
    if (this.pcOffsets.length === 0) return [[], []];
    for (const key of this.pcOffsets) {
      if (key <= offset) {
        const instrs = this.pcOffsetToInstructions.get(key);
        const nodes = [];
        const blocks = [];
        for (const instr of instrs) {
          for (const [nodeId, range] of this.nodeIdToInstructionRange.entries()) {
            if (!range) continue;
            const [start, end] = range;
            if (start == end && instr == start) {
              nodes.push("" + nodeId);
            }
            if (start <= instr && instr < end) {
              nodes.push("" + nodeId);
            }
          }
        }
        return [nodes, blocks];
      }
    }
    return [[], []];
  }

  public parsePhases(phasesJson): void {
    const nodeLabelMap = new Array<NodeLabel>();
    for (const [, genericPhase] of Object.entries<GenericPhase>(phasesJson)) {
      switch (genericPhase.type) {
        case PhaseType.Disassembly:
          const castedDisassembly = genericPhase as DisassemblyPhase;
          const disassemblyPhase = new DisassemblyPhase(castedDisassembly.name,
            castedDisassembly.data);
          disassemblyPhase.parseBlockIdToOffsetFromJSON(castedDisassembly?.blockIdToOffset);
          this.disassemblyPhase = disassemblyPhase;

          // Will be taken from the class
          this.blockIdToPCOffset = disassemblyPhase.blockIdToOffset;
          this.blockStartPCtoBlockIds = disassemblyPhase.blockStartPCtoBlockIds;
          break;
        case PhaseType.Schedule:
          const castedSchedule = genericPhase as SchedulePhase;
          const schedulePhase = new SchedulePhase(castedSchedule.name, castedSchedule.data);
          this.phaseNames.set(schedulePhase.name, this.phases.length);
          schedulePhase.parseScheduleFromJSON(castedSchedule.data);
          this.phases.push(schedulePhase);
          break;
        case PhaseType.Sequence:
          const castedSequence = camelize(genericPhase) as SequencePhase;
          const sequencePhase = new SequencePhase(castedSequence.name, castedSequence.blocks,
            castedSequence.registerAllocation);
          this.phaseNames.set(sequencePhase.name, this.phases.length);
          this.phases.push(sequencePhase);
          break;
        case PhaseType.Instructions:
          const castedInstructions = genericPhase as InstructionsPhase;
          let instructionsPhase: InstructionsPhase = null;
          if (this.instructionsPhase) {
            instructionsPhase = this.instructionsPhase;
            instructionsPhase.name += `, ${castedInstructions.name}`;
          } else {
            instructionsPhase = new InstructionsPhase(castedInstructions.name);
          }
          instructionsPhase.parseNodeIdToInstructionRangeFromJSON(castedInstructions
            ?.nodeIdToInstructionRange);
          instructionsPhase.parseBlockIdToInstructionRangeFromJSON(castedInstructions
            ?.blockIdToInstructionRange);
          instructionsPhase.parseInstructionOffsetToPCOffsetFromJSON(castedInstructions
            ?.instructionOffsetToPCOffset);
          instructionsPhase.parseCodeOffsetsInfoFromJSON(castedInstructions
            ?.codeOffsetsInfo);
          this.instructionsPhase = instructionsPhase;

          // Will be taken from the class
          this.nodeIdToInstructionRange = instructionsPhase.nodeIdToInstructionRange;
          this.blockIdToInstructionRange = instructionsPhase.blockIdToInstructionRange;
          this.codeOffsetsInfo = instructionsPhase.codeOffsetsInfo;
          this.instructionToPCOffset = instructionsPhase.instructionToPCOffset;
          this.pcOffsetToInstructions = instructionsPhase.pcOffsetToInstructions;
          this.pcOffsets = instructionsPhase.pcOffsets;
          break;
        case PhaseType.Graph:
          const castedGraph = genericPhase as GraphPhase;
          const graphPhase = new GraphPhase(castedGraph.name, 0);
          graphPhase.parseDataFromJSON(castedGraph.data, nodeLabelMap);
          graphPhase.nodeLabelMap = nodeLabelMap.slice();
          this.recordOrigins(graphPhase);
          this.phaseNames.set(graphPhase.name, this.phases.length);
          this.phases.push(graphPhase);
          break;
        default:
          throw "Unsupported phase type";
      }
    }
  }

  repairPhaseId(anyPhaseId) {
    return Math.max(0, Math.min(anyPhaseId | 0, this.phases.length - 1));
  }

  getPhase(phaseId: number) {
    return this.phases[phaseId];
  }

  getPhaseIdByName(phaseName: string) {
    return this.phaseNames.get(phaseName);
  }

  forEachPhase(f: (value: GenericPhase, index: number, array: Array<GenericPhase>) => void) {
    this.phases.forEach(f);
  }

  addAnyPositionToLine(lineNumber: number | string, sourcePosition: GenericPosition) {
    const lineNumberString = anyToString(lineNumber);
    if (!this.linePositionMap.has(lineNumberString)) {
      this.linePositionMap.set(lineNumberString, []);
    }
    const A = this.linePositionMap.get(lineNumberString);
    if (!A.includes(sourcePosition)) A.push(sourcePosition);
  }

  setSourceLineToBytecodePosition(sourceLineToBytecodePosition: Array<number> | undefined) {
    if (!sourceLineToBytecodePosition) return;
    sourceLineToBytecodePosition.forEach((pos, i) => {
      this.addAnyPositionToLine(i, new BytecodePosition(pos));
    });
  }

  lineToSourcePositions(lineNumber: number | string) {
    const positions = this.linePositionMap.get(anyToString(lineNumber));
    if (positions === undefined) return [];
    return positions;
  }
}
