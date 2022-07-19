// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../common/constants";
import * as d3 from "d3";
import { partial, storageSetItem } from "../common/util";
import { MovableView } from "./movable-view";
import { SelectionBroker } from "../selection/selection-broker";
import { SelectionMap } from "../selection/selection-map";
import { TurboshaftGraphNode } from "../phases/turboshaft-graph-phase/turboshaft-graph-node";
import { TurboshaftGraphEdge } from "../phases/turboshaft-graph-phase/turboshaft-graph-edge";
import { TurboshaftGraph } from "../turboshaft-graph";
import { TurboshaftGraphLayout } from "../turboshaft-graph-layout";
import { GraphStateType } from "../phases/graph-phase/graph-phase";
import { SelectionStorage } from "../selection/selection-storage";
import { TurboshaftGraphPhase } from "../phases/turboshaft-graph-phase/turboshaft-graph-phase";
import {
  BlockSelectionHandler,
  ClearableHandler,
  NodeSelectionHandler
} from "../selection/selection-handler";
import {
  TurboshaftGraphBlock,
  TurboshaftGraphBlockType
} from "../phases/turboshaft-graph-phase/turboshaft-graph-block";

export class TurboshaftGraphView extends MovableView<TurboshaftGraph> {
  graphLayout: TurboshaftGraphLayout;
  blockSelectionHandler: BlockSelectionHandler & ClearableHandler;
  visibleBlocks: d3.Selection<any, TurboshaftGraphBlock, any, any>;
  visibleNodes: d3.Selection<any, TurboshaftGraphNode, any, any>;
  visibleEdges: d3.Selection<any, TurboshaftGraphEdge<TurboshaftGraphBlock>, any, any>;
  visibleBubbles: d3.Selection<any, any, any, any>;
  blockDrag: d3.DragBehavior<any, TurboshaftGraphBlock, TurboshaftGraphBlock>;

  constructor(idOrContainer: string | HTMLElement, broker: SelectionBroker,
              showPhaseByName: (name: string) => void, toolbox: HTMLElement) {
    super(idOrContainer, broker, showPhaseByName, toolbox);

    this.state.selection = new SelectionMap(node => node.identifier());
    this.state.blocksSelection = new SelectionMap(block => {
      if (block instanceof TurboshaftGraphBlock) return block.identifier();
      return String(block);
    });

    this.nodeSelectionHandler = this.initializeNodeSelectionHandler();
    this.blockSelectionHandler = this.initializeBlockSelectionHandler();

    this.svg.on("click", () => {
      this.nodeSelectionHandler.clear();
      this.blockSelectionHandler.clear();
    });

    this.visibleEdges = this.graphElement.append("g");
    this.visibleBlocks = this.graphElement.append("g");

    this.blockDrag = d3.drag<any, TurboshaftGraphBlock, TurboshaftGraphBlock>()
      .on("drag",  (block: TurboshaftGraphBlock) => {
        block.x += d3.event.dx;
        block.y += d3.event.dy;
        this.updateBlockLocation(block);
      });
  }

  public initializeContent(data: TurboshaftGraphPhase, rememberedSelection: SelectionStorage):
    void {
    this.show();
    this.addImgInput("layout", "layout graph",
      partial(this.layoutAction, this));
    this.addImgInput("show-all", "show all blocks",
      partial(this.showAllBlocksAction, this));
    this.addImgInput("zoom-selection", "zoom selection",
      partial(this.zoomSelectionAction, this));
    this.addToggleImgInput("toggle-properties", "toggle properties",
      this.state.showProperties, partial(this.togglePropertiesAction, this));
    this.addToggleImgInput("toggle-cache-layout", "toggle saving graph layout",
      this.state.cacheLayout, partial(this.toggleLayoutCachingAction, this));

    this.phaseName = data.name;
    const adaptedSelection = this.createGraph(data, rememberedSelection);
    this.broker.addNodeHandler(this.nodeSelectionHandler);
    this.broker.addBlockHandler(this.blockSelectionHandler);

    if (adaptedSelection.isAdapted()) {
      this.attachSelection(adaptedSelection);
      this.viewSelection();
    } else {
      if (this.state.cacheLayout && data.transform) {
        this.viewTransformMatrix(data.transform);
      } else {
        this.viewWholeGraph();
      }
    }
  }

  public updateGraphVisibility(): void {
    if (!this.graph) return;
    this.updateVisibleBlocksAndEdges();
    this.visibleNodes = this.visibleBlocks.selectAll(".turboshaft-node");
    this.visibleBubbles = d3.selectAll("circle");
    this.updateInputAndOutputBubbles();
    this.updateInlineNodes();
  }

  public svgKeyDown(): void {
    d3.event.preventDefault();
  }

  public searchInputAction(searchInput: HTMLInputElement, e: KeyboardEvent, onlyVisible: boolean):
    void {
    if (e.keyCode == 13) {
      this.nodeSelectionHandler.clear();
      const query = searchInput.value;
      storageSetItem("lastSearch", query);
      if (query.length == 0) return;

      const reg = new RegExp(query);
      const filterFunction = (node: TurboshaftGraphNode) => {
        return reg.exec(node.displayLabel) !== null ||
          (this.state.showProperties && reg.exec(node.properties)) ||
          reg.exec(node.getTitle());
      };

      const selection = this.searchNodes(filterFunction, e, onlyVisible);

      this.nodeSelectionHandler.select(selection, true);
      this.updateGraphVisibility();
      searchInput.blur();
      this.viewSelection();
      this.focusOnSvg();
    }
    e.stopPropagation();
  }

  public hide(): void {
    this.broker.deleteBlockHandler(this.blockSelectionHandler);
    super.hide();
  }

  public detachSelection(): SelectionStorage {
    return new SelectionStorage(this.state.selection.detachSelection(),
      this.state.blocksSelection.detachSelection());
  }

  public adaptSelection(rememberedSelection: SelectionStorage): SelectionStorage {
    if (!this.graph.nodeMap && !this.graph.blockMap ||
      !(rememberedSelection instanceof SelectionStorage)) {
      return new SelectionStorage();
    }

    for (const key of rememberedSelection.nodes.keys()) {
      if (this.graph.nodeMap[key]) {
        rememberedSelection.adaptNode(key);
      }
    }

    for (const key of rememberedSelection.blocks.keys()) {
      if (this.graph.blockMap[key]) {
        rememberedSelection.adaptBlock(key);
      }
    }

    return rememberedSelection;
  }

  private adaptiveUpdateGraphVisibility(): void {
    const graphElement = this.graphElement.node();
    const originalHeight = graphElement.getBBox().height;
    this.updateGraphVisibility();
    this.focusOnSvg();
    const newHeight = graphElement.getBBox().height;
    const transformMatrix = this.getTransformMatrix();
    transformMatrix.y *= (newHeight / originalHeight);
    this.viewTransformMatrix(transformMatrix);
  }

  private initializeNodeSelectionHandler(): NodeSelectionHandler & ClearableHandler {
    const view = this;
    return {
      select: function (selectedNodes: Array<TurboshaftGraphNode>, selected: boolean) {
        view.state.selection.select(selectedNodes, selected);
        view.updateGraphVisibility();
      },
      clear: function () {
        view.state.selection.clear();
        view.broker.broadcastClear(this);
        view.updateGraphVisibility();
      },
      brokeredNodeSelect: function (nodeIds: Set<string>, selected: boolean) {
        const selection = view.graph.nodes(node => nodeIds.has(node.identifier()));
        view.state.selection.select(selection, selected);
        view.updateGraphVisibility();
      },
      brokeredClear: function () {
        view.state.selection.clear();
        view.updateGraphVisibility();
      }
    };
  }

  private initializeBlockSelectionHandler(): BlockSelectionHandler & ClearableHandler {
    const view = this;
    return {
      select: function (selectedBlocks: Array<TurboshaftGraphBlock>, selected: boolean) {
        view.state.blocksSelection.select(selectedBlocks, selected);
        const selectedBlocksKeys = new Array<string>();
        for (const selectedBlock of selectedBlocks) {
          selectedBlocksKeys.push(view.state.blocksSelection.stringKey(selectedBlock));
        }
        view.broker.broadcastBlockSelect(this, selectedBlocksKeys, selected);
        view.updateGraphVisibility();
      },
      clear: function () {
        view.state.blocksSelection.clear();
        view.broker.broadcastClear(this);
        view.updateGraphVisibility();
      },
      brokeredBlockSelect: function (blockIds: Array<string>, selected: boolean) {
        view.state.blocksSelection.select(blockIds, selected);
        view.updateGraphVisibility();
      },
      brokeredClear: function () {
        view.state.blocksSelection.clear();
        view.updateGraphVisibility();
      },
    };
  }

  private createGraph(data: TurboshaftGraphPhase, rememberedSelection: SelectionStorage):
    SelectionStorage {
    this.graph = new TurboshaftGraph(data);
    this.graphLayout = new TurboshaftGraphLayout(this.graph);

    if (!this.state.cacheLayout ||
      this.graph.graphPhase.stateType == GraphStateType.NeedToFullRebuild) {
      this.updateGraphStateType(GraphStateType.NeedToFullRebuild);
      this.showAllBlocksAction(this);
    } else {
      this.showVisible();
    }

    const adaptedSelection = this.adaptSelection(rememberedSelection);

    this.layoutGraph();
    this.updateGraphVisibility();
    return adaptedSelection;
  }

  private layoutGraph(): void {
    const layoutMessage = this.graph.graphPhase.stateType == GraphStateType.Cached
      ? "Layout turboshaft graph from cache"
      : "Layout turboshaft graph";

    console.time(layoutMessage);
    this.graphLayout.rebuild(this.state.showProperties);
    const extent = this.graph.redetermineGraphBoundingBox(this.state.showProperties);
    this.panZoom.translateExtent(extent);
    this.minScale();
    console.timeEnd(layoutMessage);
  }

  private updateBlockLocation(block: TurboshaftGraphBlock): void {
    this.visibleBlocks
      .selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block")
      .filter(b => b == block)
      .attr("transform", block => `translate(${block.x},${block.y})`);

    this.visibleEdges
      .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path")
      .filter(edge => edge.target === block || edge.source === block)
      .attr("d", edge => edge.generatePath(this.graph, this.state.showProperties));
  }

  private updateVisibleBlocksAndEdges(): void {
    const view = this;
    const iconsPath = "img/turboshaft/";

    // select existing edges
    const filteredEdges = [
      ...this.graph.blocksEdges(edge => this.graph.isRendered()
        && edge.source.visible && edge.target.visible)
    ];

    const selEdges = view.visibleEdges
      .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path")
      .data(filteredEdges, edge => edge.toString());

    // remove old edges
    selEdges.exit().remove();

    // add new edges
    const newEdges = selEdges
      .enter()
      .append("path")
      .style("marker-end", "url(#end-arrow)")
      .attr("id", edge => `e,${edge.toString()}`)
      .on("click",  edge => {
        d3.event.stopPropagation();
        if (!d3.event.shiftKey) {
          view.blockSelectionHandler.clear();
        }
        view.blockSelectionHandler.select([edge.source, edge.target], true);
      })
      .attr("adjacentToHover", "false");

    const newAndOldEdges = newEdges.merge(selEdges);

    newAndOldEdges.classed("hidden", edge => !edge.isVisible());

    // select existing blocks
    const filteredBlocks = [
      ...this.graph.blocks(block => this.graph.isRendered() && block.visible)
    ];
    const allBlocks = view.visibleBlocks
      .selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block");
    const selBlocks = allBlocks.data(filteredBlocks, block => block.toString());

    // remove old blocks
    selBlocks.exit().remove();

    // add new blocks
    const newBlocks = selBlocks
      .enter()
      .append("g")
      .classed("turboshaft-block", true)
      .classed("block", b => b.type == TurboshaftGraphBlockType.Block)
      .classed("merge", b => b.type == TurboshaftGraphBlockType.Merge)
      .classed("loop", b => b.type == TurboshaftGraphBlockType.Loop)
      .on("mouseenter", (block: TurboshaftGraphBlock) => {
        const visibleEdges = view.visibleEdges
          .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path");
        const adjInputEdges = visibleEdges.filter(edge => edge.target === block);
        const adjOutputEdges = visibleEdges.filter(edge => edge.source === block);
        adjInputEdges.classed("input", true);
        adjOutputEdges.classed("output", true);
        view.updateGraphVisibility();
      })
      .on("mouseleave", (block: TurboshaftGraphBlock) => {
        const visibleEdges = view.visibleEdges
          .selectAll<SVGPathElement, TurboshaftGraphEdge<TurboshaftGraphBlock>>("path");
        const adjEdges = visibleEdges
          .filter(edge => edge.target === block || edge.source === block);
        adjEdges.classed("input output", false);
        view.updateGraphVisibility();
      })
      .on("click", (block: TurboshaftGraphBlock) => {
        if (!d3.event.shiftKey) view.blockSelectionHandler.clear();
        view.blockSelectionHandler.select([block], undefined);
        d3.event.stopPropagation();
      })
      .call(view.blockDrag);

    newBlocks
      .append("rect")
      .attr("rx", C.TURBOSHAFT_BLOCK_BORDER_RADIUS)
      .attr("ry", C.TURBOSHAFT_BLOCK_BORDER_RADIUS)
      .attr("width", block => block.getWidth())
      .attr("height", block => block.getHeight(view.state.showProperties));

    newBlocks.each(function (block: TurboshaftGraphBlock) {
      const svg = d3.select<SVGGElement, TurboshaftGraphBlock>(this);
      svg
        .append("text")
        .classed("block-label", true)
        .attr("text-anchor", "middle")
        .attr("x", block.getWidth() / 2)
        .append("tspan")
        .text(block.displayLabel);

      svg
        .append("text")
        .classed("block-collapsed-label", true)
        .attr("text-anchor", "middle")
        .attr("x", block.getWidth() / 2)
        .attr("dy", block.labelBox.height)
        .attr("visibility", block.collapsed ? "visible" : "hidden")
        .append("tspan")
        .text(block.collapsedLabel);

      svg
        .append("image")
        .attr("xlink:href", `${iconsPath}collapse_${block.collapsed ? "down" : "up"}.svg`)
        .attr("height", block.labelBox.height)
        .attr("x", block.getWidth() - block.labelBox.height)
        .on("click", () => {
          d3.event.stopPropagation();
          block.collapsed = !block.collapsed;
          view.nodeSelectionHandler.select(block.nodes, false);
        });

      view.appendInputAndOutputBubbles(svg, block);
      view.appendInlineNodes(svg, block);
    });

    const newAndOldBlocks = newBlocks.merge(selBlocks);

    newAndOldBlocks
      .classed("selected", block => view.state.blocksSelection.isSelected(block))
      .attr("transform", block => `translate(${block.x},${block.y})`)
      .select("rect")
      .attr("height", block =>  block.getHeight(view.state.showProperties));

    newAndOldBlocks.select("image")
      .attr("xlink:href", block => `${iconsPath}collapse_${block.collapsed ? "down" : "up"}.svg`);

    newAndOldBlocks.select(".block-collapsed-label")
      .attr("visibility", block => block.collapsed ? "visible" : "hidden");

    newAndOldEdges.attr("d", edge => edge.generatePath(this.graph, view.state.showProperties));
  }

  private appendInlineNodes(svg: d3.Selection<SVGGElement, TurboshaftGraphBlock, any, any>,
                            block: TurboshaftGraphBlock): void {
    const state = this.state;
    const graph = this.graph;
    const filteredNodes = [...block.nodes.filter(_ => graph.isRendered())];
    const allNodes = svg.selectAll<SVGGElement, TurboshaftGraphNode>(".inline-node");
    const selNodes = allNodes.data(filteredNodes, node => node.toString());

    // remove old nodes
    selNodes.exit().remove();

    // add new nodes
    const newNodes = selNodes
      .enter()
      .append("g")
      .classed("turboshaft-node inline-node", true);

    let nodeY = block.labelBox.height;
    const blockWidth = block.getWidth();
    const view = this;
    newNodes.each(function (node: TurboshaftGraphNode) {
      const nodeSvg = d3.select(this);
      nodeSvg
        .attr("id", node.id)
        .append("text")
        .attr("dx", C.TURBOSHAFT_NODE_X_INDENT)
        .classed("inline-node-label", true)
        .attr("dy", nodeY)
        .append("tspan")
        .text(node.displayLabel)
        .append("title")
        .text(node.getTitle());

      nodeSvg
        .on("mouseenter", (node: TurboshaftGraphNode) => {
          view.visibleNodes.data<TurboshaftGraphNode>(
            node.inputs.map(edge => edge.source), source => source.toString())
            .classed("input", true);
          view.visibleNodes.data<TurboshaftGraphNode>(
            node.outputs.map(edge => edge.target), target => target.toString())
            .classed("output", true);
          view.updateGraphVisibility();
        })
        .on("mouseleave", (node: TurboshaftGraphNode) => {
          const inOutNodes = node.inputs.map(edge => edge.source)
            .concat(node.outputs.map(edge => edge.target));
          view.visibleNodes.data<TurboshaftGraphNode>(inOutNodes, inOut => inOut.toString())
            .classed("input output", false);
          view.updateGraphVisibility();
        })
        .on("click", (node: TurboshaftGraphNode) => {
          if (!d3.event.shiftKey) view.nodeSelectionHandler.clear();
          view.nodeSelectionHandler.select([node], undefined);
          d3.event.stopPropagation();
        });
      nodeY += node.labelBox.height;
      if (node.properties) {
        nodeSvg
          .append("text")
          .attr("dx", C.TURBOSHAFT_NODE_X_INDENT)
          .classed("inline-node-properties", true)
          .attr("dy", nodeY)
          .append("tspan")
          .text(node.getReadableProperties(blockWidth))
          .append("title")
          .text(node.properties);
        nodeY += node.propertiesBox.height;
      }
    });

    newNodes.merge(selNodes)
      .classed("selected", node => state.selection.isSelected(node))
      .select("rect")
      .attr("height", node => node.getHeight(state.showProperties));
  }

  private updateInlineNodes(): void {
    const state = this.state;
    let totalHeight = 0;
    let blockId = 0;
    this.visibleNodes.each(function (node: TurboshaftGraphNode) {
      if (blockId != node.block.id) {
        blockId = node.block.id;
        totalHeight = 0;
      }
      totalHeight += node.getHeight(state.showProperties);
      const nodeSvg = d3.select(this);
      const nodeY = state.showProperties && node.properties
        ? totalHeight - node.labelBox.height
        : totalHeight;
      nodeSvg
        .select(".inline-node-label")
        .classed("selected", node => state.selection.isSelected(node))
        .attr("dy", nodeY)
        .attr("visibility", !node.block.collapsed ? "visible" : "hidden");
      nodeSvg
        .select(".inline-node-properties")
        .attr("visibility", !node.block.collapsed && state.showProperties ? "visible" : "hidden");
    });
  }

  private appendInputAndOutputBubbles(
    svg: d3.Selection<SVGGElement, TurboshaftGraphBlock, any, any>,
    block: TurboshaftGraphBlock): void {
    for (let i = 0; i < block.inputs.length; i++) {
      const x = block.getInputX(i);
      const y = -C.DEFAULT_NODE_BUBBLE_RADIUS;
      svg.append("circle")
        .classed("filledBubbleStyle", true)
        .attr("id", `ib,${block.inputs[i].toString()}`)
        .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
        .attr("transform", `translate(${x},${y})`);
    }
    if (block.outputs.length > 0) {
      const x = block.getOutputX();
      const y = block.getHeight(this.state.showProperties)
        + C.DEFAULT_NODE_BUBBLE_RADIUS;
      svg.append("circle")
        .classed("filledBubbleStyle", true)
        .attr("id", `ob,${block.id}`)
        .attr("r", C.DEFAULT_NODE_BUBBLE_RADIUS)
        .attr("transform", `translate(${x},${y})`);
    }
  }

  private updateInputAndOutputBubbles(): void {
    const view = this;
    this.visibleBubbles.each(function () {
      const components = this.id.split(",");
      if (components[0] === "ob") {
        const from = view.graph.blockMap[components[1]];
        const x = from.getOutputX();
        const y = from.getHeight(view.state.showProperties)
          + C.DEFAULT_NODE_BUBBLE_RADIUS;
        this.setAttribute("transform", `translate(${x},${y})`);
      }
    });
  }

  private viewSelection(): void {
    let minX;
    let maxX;
    let minY;
    let maxY;
    let hasSelection = false;
    this.visibleBlocks.selectAll<SVGGElement, TurboshaftGraphBlock>(".turboshaft-block")
      .each((block: TurboshaftGraphBlock) => {
      let blockHasSelection = false;
      for (const node of block.nodes) {
        if (this.state.selection.isSelected(node) || this.state.blocksSelection.isSelected(block)) {
          blockHasSelection = true;
          minX = minX ? Math.min(minX, block.x) : block.x;
          maxX = maxX ? Math.max(maxX, block.x + block.getWidth()) : block.x + block.getWidth();
          minY = minY ? Math.min(minY, block.y) : block.y;
          maxY = maxY
            ? Math.max(maxY, block.y + block.getHeight(this.state.showProperties))
            : block.y + block.getHeight(this.state.showProperties);
        }
        if (blockHasSelection) {
          hasSelection = true;
          break;
        }
      }
    });
    if (hasSelection) {
      this.viewGraphRegion(minX - C.NODE_INPUT_WIDTH, minY - 60,
        maxX + C.NODE_INPUT_WIDTH, maxY + 60);
    }
  }

  private attachSelection(selection: SelectionStorage): void {
    if (!(selection instanceof SelectionStorage)) return;
    this.nodeSelectionHandler.clear();
    this.blockSelectionHandler.clear();
    const selectedNodes = [
      ...this.graph.nodes(node =>
        selection.adaptedNodes.has(this.state.selection.stringKey(node)))
    ];
    this.nodeSelectionHandler.select(selectedNodes, true);
    const selectedBlocks = [
      ...this.graph.blocks(block =>
        selection.adaptedBocks.has(this.state.blocksSelection.stringKey(block)))
    ];
    this.blockSelectionHandler.select(selectedBlocks, true);
  }

  // Actions (handlers of toolbox menu and hotkeys events)
  private layoutAction(view: TurboshaftGraphView): void {
    view.updateGraphStateType(GraphStateType.NeedToFullRebuild);
    view.layoutGraph();
    view.updateGraphVisibility();
    view.viewWholeGraph();
    view.focusOnSvg();
  }

  // TODO (danylo boiko) Uncollapse all blocks
  private showAllBlocksAction(view: TurboshaftGraphView): void {
    for (const node of view.graph.blocks()) {
      node.visible = true;
    }
    for (const edge of view.graph.blocksEdges()) {
      edge.visible = true;
    }
    view.showVisible();
  }

  private zoomSelectionAction(view: TurboshaftGraphView): void {
    view.viewSelection();
    view.focusOnSvg();
  }

  private togglePropertiesAction(view: TurboshaftGraphView): void {
    view.state.showProperties = !view.state.showProperties;
    const ranksMaxBlockHeight = view.graph.getRanksMaxBlockHeight(view.state.showProperties);

    for (const block of view.graph.blocks()) {
      block.y = ranksMaxBlockHeight.slice(1, block.rank).reduce<number>((accumulator, current) => {
        return accumulator + current;
      }, block.getRankIndent());
    }

    const element = document.getElementById("toggle-properties");
    element.classList.toggle("button-input-toggled", view.state.showProperties);
    view.adaptiveUpdateGraphVisibility();
  }

  private toggleLayoutCachingAction(view: TurboshaftGraphView): void {
    view.state.cacheLayout = !view.state.cacheLayout;
    const element = document.getElementById("toggle-cache-layout");
    element.classList.toggle("button-input-toggled", view.state.cacheLayout);
  }
}
