import { GNode, MINIMUM_NODE_OUTPUT_APPROACH, NODE_INPUT_WIDTH } from "./node";
import { MAX_RANK_SENTINEL } from "./constants";
import { alignUp, measureText } from "./util";
import { Edge, MINIMUM_EDGE_SEPARATION } from "./edge";

export class Graph {
  nodeMap: Array<GNode>;
  minGraphX: number;
  maxGraphX: number;
  minGraphY: number;
  maxGraphY: number;
  maxGraphNodeX: number;
  maxBackEdgeNumber: number;

  constructor(data: any) {
    this.nodeMap = [];

    this.minGraphX = 0;
    this.maxGraphX = 1;
    this.minGraphY = 0;
    this.maxGraphY = 1;

    data.nodes.forEach((n: any) => {
      n.__proto__ = GNode.prototype;
      n.visible = false;
      n.x = 0;
      n.y = 0;
      if (typeof n.pos === "number") {
        // Backwards compatibility.
        n.sourcePosition = { scriptOffset: n.pos, inliningId: -1 };
      }
      n.rank = MAX_RANK_SENTINEL;
      n.inputs = [];
      n.outputs = [];
      n.outputApproach = MINIMUM_NODE_OUTPUT_APPROACH;
      // Every control node is a CFG node.
      n.cfg = n.control;
      this.nodeMap[n.id] = n;
      n.displayLabel = n.getDisplayLabel();
      n.labelbbox = measureText(n.displayLabel);
      const typebbox = measureText(n.getDisplayType());
      const innerwidth = Math.max(n.labelbbox.width, typebbox.width);
      n.width = alignUp(innerwidth + NODE_INPUT_WIDTH * 2,
        NODE_INPUT_WIDTH);
      const innerheight = Math.max(n.labelbbox.height, typebbox.height);
      n.normalheight = innerheight + 20;
    });

    data.edges.forEach((e: any) => {
      var t = this.nodeMap[e.target];
      var s = this.nodeMap[e.source];
      var newEdge = new Edge(t, e.index, s, e.type);
      t.inputs.push(newEdge);
      s.outputs.push(newEdge);
      if (e.type == 'control') {
        // Every source of a control edge is a CFG node.
        s.cfg = true;
      }
    });

  }

  *nodes(p = (n: GNode) => true) {
    for (const node of this.nodeMap) {
      if (!node || !p(node)) continue;
      yield node;
    }
  }

  *filteredEdges(p: (e: Edge) => boolean) {
    for (const node of this.nodes()) {
      for (const edge of node.inputs) {
        if (p(edge)) yield edge;
      }
    }
  }

  forEachEdge(p: (e: Edge) => void) {
    for (const node of this.nodeMap) {
      if (!node) continue;
      for (const edge of node.inputs) {
        p(edge);
      }
    }
  }

  redetermineGraphBoundingBox(showTypes: boolean): [[number, number], [[number, number], [number, number]]] {
    this.minGraphX = 0;
    this.maxGraphNodeX = 1;
    this.maxGraphX = undefined;  // see below
    this.minGraphY = 0;
    this.maxGraphY = 1;

    for (const node of this.nodes()) {
      if (!node.visible) {
        continue;
      }

      if (node.x < this.minGraphX) {
        this.minGraphX = node.x;
      }
      if ((node.x + node.getTotalNodeWidth()) > this.maxGraphNodeX) {
        this.maxGraphNodeX = node.x + node.getTotalNodeWidth();
      }
      if ((node.y - 50) < this.minGraphY) {
        this.minGraphY = node.y - 50;
      }
      if ((node.y + node.getNodeHeight(showTypes) + 50) > this.maxGraphY) {
        this.maxGraphY = node.y + node.getNodeHeight(showTypes) + 50;
      }
    }

    this.maxGraphX = this.maxGraphNodeX +
      this.maxBackEdgeNumber * MINIMUM_EDGE_SEPARATION;

    const width = (this.maxGraphX - this.minGraphX);
    const height = this.maxGraphY - this.minGraphY;

    const extent: [[number, number], [number, number]] = [
      [this.minGraphX - width / 2, this.minGraphY - height / 2],
      [this.maxGraphX + width / 2, this.maxGraphY + height / 2]
    ];

    return [[width, height], extent];
  }

}
