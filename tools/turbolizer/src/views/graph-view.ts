// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import * as d3 from "d3";
import { partial, storageGetItem, storageSetItem } from "../common/util";
import { PhaseView } from "./view";
import { SelectionMap } from "../selection/selection";
import { ClearableHandler, NodeSelectionHandler } from "../selection/selection-handler";
import { Graph } from "../graph";
import { SelectionBroker } from "../selection/selection-broker";
import { GraphNode } from "../phases/graph-phase/graph-node";
import { GraphEdge } from "../phases/graph-phase/graph-edge";
import { GraphLayout } from "../graph-layout";
import { GraphPhase, GraphStateType } from "../phases/graph-phase/graph-phase";
import { BytecodePosition } from "../position";
import { BytecodeOrigin } from "../origin";

interface GraphState {
  showTypes: boolean;
  selection: SelectionMap;
  mouseDownNode: any;
  justDragged: boolean;
  justScaleTransGraph: boolean;
  hideDead: boolean;
}

export class GraphView extends PhaseView {
  divElement: d3.Selection<any, any, any, any>;
  svg: d3.Selection<any, any, any, any>;
  showPhaseByName: (p: string, s: Set<any>) => void;
  state: GraphState;
  selectionHandler: NodeSelectionHandler & ClearableHandler;
  graphElement: d3.Selection<any, any, any, any>;
  visibleNodes: d3.Selection<any, GraphNode, any, any>;
  visibleEdges: d3.Selection<any, GraphEdge, any, any>;
  drag: d3.DragBehavior<any, GraphNode, GraphNode>;
  panZoom: d3.ZoomBehavior<SVGElement, any>;
  visibleBubbles: d3.Selection<any, any, any, any>;
  transitionTimout: number;
  graph: Graph;
  graphLayout: GraphLayout;
  broker: SelectionBroker;
  phaseName: string;
  toolbox: HTMLElement;

  createViewElement() {
    const pane = document.createElement('div');
    pane.setAttribute('id', "graph");
    return pane;
  }

  constructor(idOrContainer: string | HTMLElement, broker: SelectionBroker,
    showPhaseByName: (s: string) => void, toolbox: HTMLElement) {
    super(idOrContainer);
    const view = this;
    this.broker = broker;
    this.showPhaseByName = showPhaseByName;
    this.divElement = d3.select(this.divNode);
    this.phaseName = "";
    this.toolbox = toolbox;
    const svg = this.divElement.append("svg")
      .attr('version', '2.0')
      .attr("width", "100%")
      .attr("height", "100%");
    svg.on("click", function (d) {
      view.selectionHandler.clear();
    });
    // Listen for key events. Note that the focus handler seems
    // to be important even if it does nothing.
    svg
      .on("focus", e => { })
      .on("keydown", e => { view.svgKeyDown(); });

    view.svg = svg;

    this.state = {
      selection: null,
      mouseDownNode: null,
      justDragged: false,
      justScaleTransGraph: false,
      showTypes: false,
      hideDead: false
    };

    this.selectionHandler = {
      clear: function () {
        view.state.selection.clear();
        broker.broadcastClear(this);
        view.updateGraphVisibility();
      },
      select: function (nodes: Array<GraphNode>, selected: boolean) {
        const locations = [];
        for (const node of nodes) {
          if (node.nodeLabel.sourcePosition) {
            locations.push(node.nodeLabel.sourcePosition);
          }
          if (node.nodeLabel.origin && node.nodeLabel.origin instanceof BytecodeOrigin) {
            locations.push(new BytecodePosition(node.nodeLabel.origin.bytecodePosition));
          }
        }
        view.state.selection.select(nodes, selected);
        broker.broadcastSourcePositionSelect(this, locations, selected);
        view.updateGraphVisibility();
      },
      brokeredNodeSelect: function (locations, selected: boolean) {
        if (!view.graph) return;
        const selection = view.graph.nodes(n => {
          return locations.has(n.identifier())
            && (!view.state.hideDead || n.isLive());
        });
        view.state.selection.select(selection, selected);
        // Update edge visibility based on selection.
        for (const n of view.graph.nodes()) {
          if (view.state.selection.isSelected(n)) {
            n.visible = true;
            n.inputs.forEach(e => {
              e.visible = e.visible || view.state.selection.isSelected(e.source);
            });
            n.outputs.forEach(e => {
              e.visible = e.visible || view.state.selection.isSelected(e.target);
            });
          }
        }
        view.updateGraphVisibility();
      },
      brokeredClear: function () {
        view.state.selection.clear();
        view.updateGraphVisibility();
      }
    };

    view.state.selection = new SelectionMap(n => n.identifier(),
        n => n.nodeLabel?.origin?.identifier());

    const defs = svg.append('svg:defs');
    defs.append('svg:marker')
      .attr('id', 'end-arrow')
      .attr('viewBox', '0 -4 8 8')
      .attr('refX', 2)
      .attr('markerWidth', 2.5)
      .attr('markerHeight', 2.5)
      .attr('orient', 'auto')
      .append('svg:path')
      .attr('d', 'M0,-4L8,0L0,4');

    this.graphElement = svg.append("g");
    view.visibleEdges = this.graphElement.append("g");
    view.visibleNodes = this.graphElement.append("g");

    view.drag = d3.drag<any, GraphNode, GraphNode>()
      .on("drag", function (d) {
        d.x += d3.event.dx;
        d.y += d3.event.dy;
        view.updateGraphVisibility();
      });

    function zoomed() {
      if (d3.event.shiftKey) return false;
      view.graphElement.attr("transform", d3.event.transform);
      return true;
    }

    const zoomSvg = d3.zoom<SVGElement, any>()
      .scaleExtent([0.2, 40])
      .on("zoom", zoomed)
      .on("start", function () {
        if (d3.event.shiftKey) return;
        d3.select('body').style("cursor", "move");
      })
      .on("end", function () {
        d3.select('body').style("cursor", "auto");
      });

    svg.call(zoomSvg).on("dblclick.zoom", null);

    view.panZoom = zoomSvg;
  }

  getEdgeFrontier(nodes: Iterable<GraphNode>, inEdges: boolean,
    edgeFilter: (e: GraphEdge, i: number) => boolean) {
    const frontier: Set<GraphEdge> = new Set();
    for (const n of nodes) {
      const edges = inEdges ? n.inputs : n.outputs;
      let edgeNumber = 0;
      edges.forEach((edge: GraphEdge) => {
        if (edgeFilter == undefined || edgeFilter(edge, edgeNumber)) {
          frontier.add(edge);
        }
        ++edgeNumber;
      });
    }
    return frontier;
  }

  getNodeFrontier(nodes: Iterable<GraphNode>, inEdges: boolean,
    edgeFilter: (e: GraphEdge, i: number) => boolean) {
    const view = this;
    const frontier: Set<GraphNode> = new Set();
    let newState = true;
    const edgeFrontier = view.getEdgeFrontier(nodes, inEdges, edgeFilter);
    // Control key toggles edges rather than just turning them on
    if (d3.event.ctrlKey) {
      edgeFrontier.forEach(function (edge: GraphEdge) {
        if (edge.visible) {
          newState = false;
        }
      });
    }
    edgeFrontier.forEach(function (edge: GraphEdge) {
      edge.visible = newState;
      if (newState) {
        const node = inEdges ? edge.source : edge.target;
        node.visible = true;
        frontier.add(node);
      }
    });
    view.updateGraphVisibility();
    if (newState) {
      return frontier;
    } else {
      return undefined;
    }
  }

  initializeContent(data: GraphPhase, rememberedSelection) {
    this.show();
    function createImgInput(id: string, title: string, onClick): HTMLElement {
      const input = document.createElement("input");
      input.setAttribute("id", id);
      input.setAttribute("type", "image");
      input.setAttribute("title", title);
      input.setAttribute("src", `img/toolbox/${id}-icon.png`);
      input.className = "button-input graph-toolbox-item";
      input.addEventListener("click", onClick);
      return input;
    }
    function createImgToggleInput(id: string, title: string, onClick): HTMLElement {
      const input = createImgInput(id, title, onClick);
      const toggled = storageGetItem(id, true);
      input.classList.toggle("button-input-toggled", toggled);
      return input;
    }
    this.toolbox.appendChild(createImgInput("layout", "layout graph",
      partial(this.layoutAction, this)));
    this.toolbox.appendChild(createImgInput("show-all", "show all nodes",
      partial(this.showAllAction, this)));
    this.toolbox.appendChild(createImgInput("show-control", "show only control nodes",
      partial(this.showControlAction, this)));
    this.toolbox.appendChild(createImgInput("toggle-hide-dead", "toggle hide dead nodes",
      partial(this.toggleHideDead, this)));
    this.toolbox.appendChild(createImgInput("hide-unselected", "hide unselected",
      partial(this.hideUnselectedAction, this)));
    this.toolbox.appendChild(createImgInput("hide-selected", "hide selected",
      partial(this.hideSelectedAction, this)));
    this.toolbox.appendChild(createImgInput("zoom-selection", "zoom selection",
      partial(this.zoomSelectionAction, this)));
    this.toolbox.appendChild(createImgInput("toggle-types", "toggle types",
      partial(this.toggleTypesAction, this)));
    this.toolbox.appendChild(createImgToggleInput("cache-graphs", "remember graph layout",
      partial(this.toggleGraphCachingAction)));

    const adaptedSelection = this.adaptSelectionToCurrentPhase(data.data, rememberedSelection);

    this.phaseName = data.name;
    this.createGraph(data, adaptedSelection);
    this.broker.addNodeHandler(this.selectionHandler);

    const selectedNodes = adaptedSelection?.size > 0
      ? this.attachSelection(adaptedSelection)
      : null;

    if (selectedNodes?.length > 0) {
      this.connectVisibleSelectedNodes();
      this.viewSelection();
    } else {
      this.viewWholeGraph();
      if (this.isCachingEnabled() && data.transform) {
        this.svg.call(this.panZoom.transform, d3.zoomIdentity
          .translate(data.transform.x, data.transform.y)
          .scale(data.transform.scale));
      }
    }
  }

  deleteContent() {
    for (const item of this.toolbox.querySelectorAll(".graph-toolbox-item")) {
      item.parentElement.removeChild(item);
    }

    if (!this.isCachingEnabled()) {
      this.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    }

    this.graph.graphPhase.rendered = false;
    this.updateGraphVisibility();
  }

  public hide(): void {
    if (this.isCachingEnabled()) {
      const matrix = this.graphElement.node().transform.baseVal.consolidate().matrix;
      this.graph.graphPhase.transform = { scale: matrix.a, x: matrix.e, y: matrix.f };
    } else {
      this.graph.graphPhase.transform = null;
    }
    super.hide();
    this.deleteContent();
  }

  createGraph(data, selection) {
    this.graph = new Graph(data);
    this.graphLayout = new GraphLayout(this.graph);

    if (!this.isCachingEnabled() ||
      this.graph.graphPhase.stateType == GraphStateType.NeedToFullRebuild) {
      this.updateGraphStateType(GraphStateType.NeedToFullRebuild);
      this.showControlAction(this);
    } else {
      this.showVisible();
    }

    if (selection !== undefined) {
      for (const node of this.graph.nodes()) {
        node.visible = node.visible || selection.has(node.identifier());
      }
    }

    this.graph.makeEdgesVisible();

    this.layoutGraph();
    this.updateGraphVisibility();
  }

  public showVisible() {
    this.updateGraphVisibility();
    this.viewWholeGraph();
    this.focusOnSvg();
  }

  connectVisibleSelectedNodes() {
    const view = this;
    for (const n of view.state.selection) {
      n.inputs.forEach(function (edge: GraphEdge) {
        if (edge.source.visible && edge.target.visible) {
          edge.visible = true;
        }
      });
      n.outputs.forEach(function (edge: GraphEdge) {
        if (edge.source.visible && edge.target.visible) {
          edge.visible = true;
        }
      });
    }
  }

  updateInputAndOutputBubbles() {
    const view = this;
    const g = this.graph;
    const s = this.visibleBubbles;
    s.classed("filledBubbleStyle", function (c) {
      const components = this.id.split(',');
      if (components[0] == "ib") {
        const edge = g.nodeMap[components[3]].inputs[components[2]];
        return edge.isVisible();
      } else {
        return g.nodeMap[components[1]].areAnyOutputsVisible() == 2;
      }
    }).classed("halfFilledBubbleStyle", function (c) {
      const components = this.id.split(',');
      if (components[0] == "ib") {
        return false;
      } else {
        return g.nodeMap[components[1]].areAnyOutputsVisible() == 1;
      }
    }).classed("bubbleStyle", function (c) {
      const components = this.id.split(',');
      if (components[0] == "ib") {
        const edge = g.nodeMap[components[3]].inputs[components[2]];
        return !edge.isVisible();
      } else {
        return g.nodeMap[components[1]].areAnyOutputsVisible() == 0;
      }
    });
    s.each(function (c) {
      const components = this.id.split(',');
      if (components[0] == "ob") {
        const from = g.nodeMap[components[1]];
        const x = from.getOutputX();
        const y = from.getNodeHeight(view.state.showTypes) + C.DEFAULT_NODE_BUBBLE_RADIUS;
        const transform = "translate(" + x + "," + y + ")";
        this.setAttribute('transform', transform);
      }
    });
  }

  adaptSelectionToCurrentPhase(data, selection) {
    const updatedGraphSelection = new Set<string>();
    if (!data || !(selection instanceof Map)) return updatedGraphSelection;
    // Adding survived nodes (with the same id)
    for (const node of data.nodes) {
      const stringKey = this.state.selection.stringKey(node);
      if (selection.has(stringKey)) {
        updatedGraphSelection.add(stringKey);
      }
    }
    // Adding children of nodes
    for (const node of data.nodes) {
      const originStringKey = this.state.selection.originStringKey(node);
      if (originStringKey && selection.has(originStringKey)) {
        updatedGraphSelection.add(this.state.selection.stringKey(node));
      }
    }
    // Adding ancestors of nodes
    selection.forEach(selectedNode => {
      const originStringKey = this.state.selection.originStringKey(selectedNode);
      if (originStringKey) {
        updatedGraphSelection.add(originStringKey);
      }
    });
    return updatedGraphSelection;
  }

  public attachSelection(selection: Set<string>): Array<GraphNode> {
    if (!(selection instanceof Set)) return new Array<GraphNode>();
    this.selectionHandler.clear();
    const selected = [
      ...this.graph.nodes(node =>
        selection.has(this.state.selection.stringKey(node))
        && (!this.state.hideDead || node.isLive()))
    ];
    this.selectionHandler.select(selected, true);
    return selected;
  }

  detachSelection() {
    return this.state.selection.detachSelection();
  }

  selectAllNodes() {
    if (!d3.event.shiftKey) {
      this.state.selection.clear();
    }
    const allVisibleNodes = [...this.graph.nodes(n => n.visible)];
    this.state.selection.select(allVisibleNodes, true);
    this.updateGraphVisibility();
  }

  layoutAction(graph: GraphView) {
    graph.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    graph.layoutGraph();
    graph.updateGraphVisibility();
    graph.viewWholeGraph();
    graph.focusOnSvg();
  }

  showAllAction(view: GraphView) {
    for (const n of view.graph.nodes()) {
      n.visible = !view.state.hideDead || n.isLive();
    }
    view.graph.forEachEdge((e: GraphEdge) => {
      e.visible = e.source.visible || e.target.visible;
    });
    view.updateGraphVisibility();
    view.viewWholeGraph();
    view.focusOnSvg();
  }

  showControlAction(view: GraphView) {
    for (const n of view.graph.nodes()) {
      n.visible = n.cfg && (!view.state.hideDead || n.isLive());
    }
    view.graph.forEachEdge((e: GraphEdge) => {
      e.visible = e.type === "control" && e.source.visible && e.target.visible;
    });
    view.showVisible();
  }

  toggleHideDead(view: GraphView) {
    view.state.hideDead = !view.state.hideDead;
    if (view.state.hideDead) {
      view.hideDead();
    } else {
      view.showDead();
    }
    const element = document.getElementById('toggle-hide-dead');
    element.classList.toggle('button-input-toggled', view.state.hideDead);
    view.focusOnSvg();
  }

  hideDead() {
    for (const n of this.graph.nodes()) {
      if (!n.isLive()) {
        n.visible = false;
        this.state.selection.select([n], false);
      }
    }
    this.updateGraphVisibility();
  }

  showDead() {
    for (const n of this.graph.nodes()) {
      if (!n.isLive()) {
        n.visible = true;
      }
    }
    this.updateGraphVisibility();
  }

  hideUnselectedAction(view: GraphView) {
    for (const n of view.graph.nodes()) {
      if (!view.state.selection.isSelected(n)) {
        n.visible = false;
      }
    }
    view.updateGraphVisibility();
    view.focusOnSvg();
  }

  hideSelectedAction(view: GraphView) {
    for (const n of view.graph.nodes()) {
      if (view.state.selection.isSelected(n)) {
        n.visible = false;
      }
    }
    view.selectionHandler.clear();
    view.focusOnSvg();
  }

  zoomSelectionAction(view: GraphView) {
    view.viewSelection();
    view.focusOnSvg();
  }

  toggleTypesAction(view: GraphView) {
    view.toggleTypes();
    view.focusOnSvg();
  }

  private toggleGraphCachingAction(): void {
    const key = "cache-graphs";
    const toggled = storageGetItem(key, true);
    storageSetItem(key, !toggled);
    const element = document.getElementById(key);
    element.classList.toggle("button-input-toggled", !toggled);
  }

  searchInputAction(searchBar: HTMLInputElement, e: KeyboardEvent, onlyVisible: boolean) {
    if (e.keyCode == 13) {
      this.selectionHandler.clear();
      const query = searchBar.value;
      window.sessionStorage.setItem("lastSearch", query);
      if (query.length == 0) return;

      const reg = new RegExp(query);
      const filterFunction = (n: GraphNode) => {
        return (reg.exec(n.getDisplayLabel()) != null ||
          (this.state.showTypes && reg.exec(n.getDisplayType())) ||
          (reg.exec(n.getTitle())) ||
          reg.exec(n.nodeLabel.opcode) != null);
      };

      const selection = [...this.graph.nodes(n => {
        if ((e.ctrlKey || n.visible || !onlyVisible) && filterFunction(n)) {
          if (e.ctrlKey || !onlyVisible) n.visible = true;
          return true;
        }
        return false;
      })];

      this.selectionHandler.select(selection, true);
      this.connectVisibleSelectedNodes();
      this.updateGraphVisibility();
      searchBar.blur();
      this.viewSelection();
      this.focusOnSvg();
    }
    e.stopPropagation();
  }

  focusOnSvg() {
    (document.getElementById("graph").childNodes[0] as HTMLElement).focus();
  }

  svgKeyDown() {
    const view = this;
    const state = this.state;

    const showSelectionFrontierNodes = (inEdges: boolean, filter: (e: GraphEdge, i: number) =>
      boolean, doSelect: boolean) => {
      const frontier = view.getNodeFrontier(state.selection, inEdges, filter);
      if (frontier != undefined && frontier.size) {
        if (doSelect) {
          if (!d3.event.shiftKey) {
            state.selection.clear();
          }
          state.selection.select([...frontier], true);
        }
        view.updateGraphVisibility();
      }
    };

    let eventHandled = true; // unless the below switch defaults
    switch (d3.event.keyCode) {
      case 49:
      case 50:
      case 51:
      case 52:
      case 53:
      case 54:
      case 55:
      case 56:
      case 57:
        // '1'-'9'
        showSelectionFrontierNodes(true,
          (edge: GraphEdge, index: number) => index == (d3.event.keyCode - 49),
          !d3.event.ctrlKey);
        break;
      case 97:
      case 98:
      case 99:
      case 100:
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
        // 'numpad 1'-'numpad 9'
        showSelectionFrontierNodes(true,
          (edge, index) => index == (d3.event.keyCode - 97),
          !d3.event.ctrlKey);
        break;
      case 67:
        // 'c'
        showSelectionFrontierNodes(d3.event.altKey,
          (edge, index) => edge.type == 'control',
          true);
        break;
      case 69:
        // 'e'
        showSelectionFrontierNodes(d3.event.altKey,
          (edge, index) => edge.type == 'effect',
          true);
        break;
      case 79:
        // 'o'
        showSelectionFrontierNodes(false, undefined, false);
        break;
      case 73:
        // 'i'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          showSelectionFrontierNodes(true, undefined, false);
        } else {
          eventHandled = false;
        }
        break;
      case 65:
        // 'a'
        view.selectAllNodes();
        break;
      case 38:
      // UP
      case 40: {
        // DOWN
        showSelectionFrontierNodes(d3.event.keyCode == 38, undefined, true);
        break;
      }
      case 82:
        // 'r'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.layoutAction(this);
        } else {
          eventHandled = false;
        }
        break;
      case 80:
        // 'p'
        view.selectOrigins();
        break;
      default:
        eventHandled = false;
        break;
      case 83:
        // 's'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.hideSelectedAction(this);
        } else {
          eventHandled = false;
        }
        break;
      case 85:
        // 'u'
        if (!d3.event.ctrlKey && !d3.event.shiftKey) {
          this.hideUnselectedAction(this);
        } else {
          eventHandled = false;
        }
        break;
    }
    if (eventHandled) {
      d3.event.preventDefault();
    }
  }

  layoutGraph() {
    const layoutMessage = this.graph.graphPhase.stateType == GraphStateType.Cached
      ? "Layout graph from cache"
      : "Layout graph";

    console.time(layoutMessage);
    this.graphLayout.rebuild(this.state.showTypes);
    const extent = this.graph.redetermineGraphBoundingBox(this.state.showTypes);
    this.panZoom.translateExtent(extent);
    this.minScale();
    console.timeEnd(layoutMessage);
  }

  selectOrigins() {
    const state = this.state;
    const origins = [];
    let phase = this.phaseName;
    const selection = new Set<any>();
    for (const n of state.selection) {
      const origin = n.nodeLabel.origin;
      if (origin) {
        phase = origin.phase;
        const node = this.graph.nodeMap[origin.nodeId];
        if (phase === this.phaseName && node) {
          origins.push(node);
        } else {
          selection.add(`${origin.nodeId}`);
        }
      }
    }
    // Only go through phase reselection if we actually need
    // to display another phase.
    if (selection.size > 0 && phase !== this.phaseName) {
      this.showPhaseByName(phase, selection);
    } else if (origins.length > 0) {
      this.selectionHandler.clear();
      this.selectionHandler.select(origins, true);
    }
  }

  // call to propagate changes to graph
  updateGraphVisibility() {
    const view = this;
    const graph = this.graph;
    const state = this.state;
    if (!graph) return;

    const filteredEdges = [
      ...graph.filteredEdges(edge => this.graph.isRendered()
        && edge.source.visible && edge.target.visible)
    ];
    const selEdges = view.visibleEdges.selectAll<SVGPathElement, GraphEdge>("path")
      .data(filteredEdges, e => e.toString());

    // remove old links
    selEdges.exit().remove();

    // add new paths
    const newEdges = selEdges.enter()
      .append('path');

    newEdges.style('marker-end', 'url(#end-arrow)')
      .attr("id", function (edge) { return "e," + edge.toString(); })
      .on("click", function (edge) {
        d3.event.stopPropagation();
        if (!d3.event.shiftKey) {
          view.selectionHandler.clear();
        }
        view.selectionHandler.select([edge.source, edge.target], true);
      })
      .attr("adjacentToHover", "false")
      .classed('value', function (e) {
        return e.type == 'value' || e.type == 'context';
      }).classed('control', function (e) {
        return e.type == 'control';
      }).classed('effect', function (e) {
        return e.type == 'effect';
      }).classed('frame-state', function (e) {
        return e.type == 'frame-state';
      }).attr('stroke-dasharray', function (e) {
        if (e.type == 'frame-state') return "10,10";
        return (e.type == 'effect') ? "5,5" : "";
      });

    const newAndOldEdges = newEdges.merge(selEdges);

    newAndOldEdges.classed('hidden', e => !e.isVisible());

    // select existing nodes
    const filteredNodes = [...graph.nodes(n => this.graph.isRendered() && n.visible)];
    const allNodes = view.visibleNodes.selectAll<SVGGElement, GraphNode>("g");
    const selNodes = allNodes.data(filteredNodes, n => n.toString());

    // remove old nodes
    selNodes.exit().remove();

    // add new nodes
    const newGs = selNodes.enter()
      .append("g");

    newGs.classed("turbonode", function (n) { return true; })
      .classed("control", function (n) { return n.isControl(); })
      .classed("live", function (n) { return n.isLive(); })
      .classed("dead", function (n) { return !n.isLive(); })
      .classed("javascript", function (n) { return n.isJavaScript(); })
      .classed("input", function (n) { return n.isInput(); })
      .classed("simplified", function (n) { return n.isSimplified(); })
      .classed("machine", function (n) { return n.isMachine(); })
      .on('mouseenter', function (node) {
        const visibleEdges = view.visibleEdges.selectAll<SVGPathElement, GraphEdge>('path');
        const adjInputEdges = visibleEdges.filter(e => e.target === node);
        const adjOutputEdges = visibleEdges.filter(e => e.source === node);
        adjInputEdges.attr('relToHover', "input");
        adjOutputEdges.attr('relToHover', "output");
        const adjInputNodes = adjInputEdges.data().map(e => e.source);
        const visibleNodes = view.visibleNodes.selectAll<SVGGElement, GraphNode>("g");
        visibleNodes.data<GraphNode>(adjInputNodes, n => n.toString())
          .attr('relToHover', "input");
        const adjOutputNodes = adjOutputEdges.data().map(e => e.target);
        visibleNodes.data<GraphNode>(adjOutputNodes, n => n.toString())
          .attr('relToHover', "output");
        view.updateGraphVisibility();
      })
      .on('mouseleave', function (node) {
        const visibleEdges = view.visibleEdges.selectAll<SVGPathElement, GraphEdge>('path');
        const adjEdges = visibleEdges.filter(e => e.target === node || e.source === node);
        adjEdges.attr('relToHover', "none");
        const adjNodes = adjEdges.data().map(e => e.target).concat(adjEdges.data().map(e => e.source));
        const visibleNodes = view.visibleNodes.selectAll<SVGPathElement, GraphNode>("g");
        visibleNodes.data(adjNodes, n => n.toString()).attr('relToHover', "none");
        view.updateGraphVisibility();
      })
      .on("click", d => {
        if (!d3.event.shiftKey) view.selectionHandler.clear();
        view.selectionHandler.select([d], undefined);
        d3.event.stopPropagation();
      })
      .call(view.drag);

    newGs.append("rect")
      .attr("rx", 10)
      .attr("ry", 10)
      .attr('width', function (d) {
        return d.getTotalNodeWidth();
      })
      .attr('height', function (d) {
        return d.getNodeHeight(view.state.showTypes);
      });

    function appendInputAndOutputBubbles(g, d) {
      for (let i = 0; i < d.inputs.length; ++i) {
        const x = d.getInputX(i);
        const y = -C.DEFAULT_NODE_BUBBLE_RADIUS;
        g.append('circle')
          .classed("filledBubbleStyle", function (c) {
            return d.inputs[i].isVisible();
          })
          .classed("bubbleStyle", function (c) {
            return !d.inputs[i].isVisible();
          })
          .attr("id", `ib,${d.inputs[i]}`)
          .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
          .attr("transform", function (d) {
            return "translate(" + x + "," + y + ")";
          })
          .on("click", function (this: SVGCircleElement, d) {
            const components = this.id.split(',');
            const node = graph.nodeMap[components[3]];
            const edge = node.inputs[components[2]];
            const visible = !edge.isVisible();
            node.setInputVisibility(components[2], visible);
            d3.event.stopPropagation();
            view.updateGraphVisibility();
          });
      }
      if (d.outputs.length != 0) {
        const x = d.getOutputX();
        const y = d.getNodeHeight(view.state.showTypes) + C.DEFAULT_NODE_BUBBLE_RADIUS;
        g.append('circle')
          .classed("filledBubbleStyle", function (c) {
            return d.areAnyOutputsVisible() == 2;
          })
          .classed("halFilledBubbleStyle", function (c) {
            return d.areAnyOutputsVisible() == 1;
          })
          .classed("bubbleStyle", function (c) {
            return d.areAnyOutputsVisible() == 0;
          })
          .attr("id", "ob," + d.id)
          .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
          .attr("transform", function (d) {
            return "translate(" + x + "," + y + ")";
          })
          .on("click", function (d) {
            d.setOutputVisibility(d.areAnyOutputsVisible() == 0);
            d3.event.stopPropagation();
            view.updateGraphVisibility();
          });
      }
    }

    newGs.each(function (d) {
      appendInputAndOutputBubbles(d3.select(this), d);
    });

    newGs.each(function (d) {
      d3.select(this).append("text")
        .classed("label", true)
        .attr("text-anchor", "right")
        .attr("dx", 5)
        .attr("dy", 5)
        .append('tspan')
        .text(function (l) {
          return d.getDisplayLabel();
        })
        .append("title")
        .text(function (l) {
          return d.getTitle();
        });
      if (d.nodeLabel.type != undefined) {
        d3.select(this).append("text")
          .classed("label", true)
          .classed("type", true)
          .attr("text-anchor", "right")
          .attr("dx", 5)
          .attr("dy", d.labelBox.height + 5)
          .append('tspan')
          .text(function (l) {
            return d.getDisplayType();
          })
          .append("title")
          .text(function (l) {
            return d.getType();
          });
      }
    });

    const newAndOldNodes = newGs.merge(selNodes);

    newAndOldNodes.select<SVGTextElement>('.type').each(function (d) {
      this.setAttribute('visibility', view.state.showTypes ? 'visible' : 'hidden');
    });

    newAndOldNodes
      .classed("selected", function (n) {
        if (state.selection.isSelected(n)) return true;
        return false;
      })
      .attr("transform", function (d) { return "translate(" + d.x + "," + d.y + ")"; })
      .select('rect')
      .attr('height', function (d) { return d.getNodeHeight(view.state.showTypes); });

    view.visibleBubbles = d3.selectAll('circle');

    view.updateInputAndOutputBubbles();

    graph.maxGraphX = graph.maxGraphNodeX;
    newAndOldEdges.attr("d", function (edge) {
      return edge.generatePath(graph, view.state.showTypes);
    });
  }

  private getSvgViewDimensions(): [number, number] {
    return [this.container.clientWidth, this.container.clientHeight];
  }

  getSvgExtent(): [[number, number], [number, number]] {
    return [[0, 0], [this.container.clientWidth, this.container.clientHeight]];
  }

  minScale() {
    const [clientWith, clientHeight] = this.getSvgViewDimensions();
    const minXScale = clientWith / (2 * this.graph.width);
    const minYScale = clientHeight / (2 * this.graph.height);
    const minScale = Math.min(minXScale, minYScale);
    this.panZoom.scaleExtent([minScale, 40]);
    return minScale;
  }

  onresize() {
    const trans = d3.zoomTransform(this.svg.node());
    const ctrans = this.panZoom.constrain()(trans, this.getSvgExtent(), this.panZoom.translateExtent());
    this.panZoom.transform(this.svg, ctrans);
  }

  toggleTypes() {
    const view = this;
    view.state.showTypes = !view.state.showTypes;
    const element = document.getElementById('toggle-types');
    element.classList.toggle('button-input-toggled', view.state.showTypes);
    view.updateGraphVisibility();
  }

  viewSelection() {
    const view = this;
    let minX;
    let maxX;
    let minY;
    let maxY;
    let hasSelection = false;
    view.visibleNodes.selectAll<SVGGElement, GraphNode>("g").each(function (n) {
      if (view.state.selection.isSelected(n)) {
        hasSelection = true;
        minX = minX ? Math.min(minX, n.x) : n.x;
        maxX = maxX ? Math.max(maxX, n.x + n.getTotalNodeWidth()) :
          n.x + n.getTotalNodeWidth();
        minY = minY ? Math.min(minY, n.y) : n.y;
        maxY = maxY ? Math.max(maxY, n.y + n.getNodeHeight(view.state.showTypes)) :
          n.y + n.getNodeHeight(view.state.showTypes);
      }
    });
    if (hasSelection) {
      view.viewGraphRegion(minX - C.NODE_INPUT_WIDTH, minY - 60,
        maxX + C.NODE_INPUT_WIDTH, maxY + 60);
    }
  }

  viewGraphRegion(minX, minY, maxX, maxY) {
    const [width, height] = this.getSvgViewDimensions();
    const dx = maxX - minX;
    const dy = maxY - minY;
    const x = (minX + maxX) / 2;
    const y = (minY + maxY) / 2;
    const scale = Math.min(width / dx, height / dy) * 0.9;
    this.svg
      .transition().duration(120).call(this.panZoom.scaleTo, scale)
      .transition().duration(120).call(this.panZoom.translateTo, x, y);
  }

  viewWholeGraph() {
    this.panZoom.scaleTo(this.svg, 0);
    this.panZoom.translateTo(this.svg,
      this.graph.minGraphX + this.graph.width / 2,
      this.graph.minGraphY + this.graph.height / 2);
  }

  private updateGraphStateType(stateType: GraphStateType): void {
    this.graph.graphPhase.stateType = stateType;
  }

  private isCachingEnabled(): boolean {
    return storageGetItem("cache-graphs", true);
  }
}
