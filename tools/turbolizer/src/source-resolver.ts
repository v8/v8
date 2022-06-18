// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { anyToString, camelize, sortUnique } from "./common/util";
import { PhaseType } from "./phases/phase";
import { GraphPhase } from "./phases/graph-phase";
import { DisassemblyPhase } from "./phases/disassembly-phase";
import { BytecodePosition, InliningPosition, SourcePosition } from "./position";
import { InstructionsPhase } from "./phases/instructions-phase";
import { SchedulePhase } from "./phases/schedule-phase";
import { SequencePhase } from "./phases/sequence-phase";
import { BytecodeOrigin } from "./origin";
import { Source } from "./source";
import { NodeLabel } from "./node-label";
import { TurboshaftGraphPhase } from "./phases/turboshaft-graph-phase/turboshaft-graph-phase";

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
type GenericPhase = GraphPhase | TurboshaftGraphPhase | DisassemblyPhase
  | InstructionsPhase | SchedulePhase | SequencePhase;

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
  }

  public getMainFunction(jsonObj): Source {
    const fncJson = jsonObj.function;
    // Backwards compatibility.
    if (typeof fncJson === 'string') {
      return new Source(null, null, jsonObj.source, -1, true,
        new Array<SourcePosition>(), jsonObj.sourcePosition,
        jsonObj.sourcePosition + jsonObj.source.length);
    }
    return new Source(fncJson.sourceName, fncJson.functionName, fncJson.sourceText,
      fncJson.sourceId, false, new Array<SourcePosition>(), fncJson.startPosition,
      fncJson.endPosition);
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
        case PhaseType.TurboshaftGraph:
          const castedTurboshaftGraph = genericPhase as TurboshaftGraphPhase;
          const turboshaftGraphPhase = new TurboshaftGraphPhase(castedTurboshaftGraph.name, 0);
          turboshaftGraphPhase.parseDataFromJSON(castedTurboshaftGraph.data);
          this.phaseNames.set(turboshaftGraphPhase.name, this.phases.length);
          this.phases.push(turboshaftGraphPhase);
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
