// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../src/constants"
import { SourceResolver } from "../src/source-resolver"
import { SelectionBroker } from "../src/selection-broker"
import { DisassemblyView } from "../src/disassembly-view"
import { GraphMultiView } from "../src/graphmultiview"
import { CodeMode, CodeView } from "../src/code-view"
import { Tabs } from "../src/tabs"
import * as d3 from "d3"

class Snapper {
  resizer: Resizer;
  sourceExpand: HTMLElement;
  sourceCollapse: HTMLElement;
  disassemblyExpand: HTMLElement;
  disassemblyCollapse: HTMLElement;

  constructor(resizer: Resizer) {
    this.resizer = resizer;
    this.sourceExpand = document.getElementById(C.SOURCE_EXPAND_ID);
    this.sourceCollapse = document.getElementById(C.SOURCE_COLLAPSE_ID);
    this.disassemblyExpand = document.getElementById(C.DISASSEMBLY_EXPAND_ID);
    this.disassemblyCollapse = document.getElementById(C.DISASSEMBLY_COLLAPSE_ID);

    document.getElementById("source-collapse").addEventListener("click", () => {
      this.toggleSourceExpanded();
    });
    document.getElementById("disassembly-collapse").addEventListener("click", () => {
      this.toggleDisassemblyExpanded();
    });
  }

  restoreExpandedState(): void {
    this.setSourceExpanded(this.getLastExpandedState("source", true));
    this.setDisassemblyExpanded(this.getLastExpandedState("disassembly", false));
  }

  getLastExpandedState(type: string, default_state: boolean): boolean {
    var state = window.sessionStorage.getItem("expandedState-" + type);
    if (state === null) return default_state;
    return state === 'true';
  }

  toggleSourceExpanded(): void {
    this.setSourceExpanded(!this.sourceExpand.classList.contains("invisible"));
  }

  sourceExpandUpdate(newState: boolean): void {
    window.sessionStorage.setItem("expandedState-source", `${newState}`);
    this.sourceExpand.classList.toggle("invisible", newState);
    this.sourceCollapse.classList.toggle("invisible", !newState);
  }

  setSourceExpanded(newState: boolean): void {
    if (this.sourceExpand.classList.contains("invisible") === newState) return;
    const resizer = this.resizer;
    this.sourceExpandUpdate(newState);
    if (newState) {
      resizer.sep_left = resizer.sep_left_snap;
      resizer.sep_left_snap = 0;
    } else {
      resizer.sep_left_snap = resizer.sep_left;
      resizer.sep_left = 0;
    }
    resizer.updatePanes();
  }

  toggleDisassemblyExpanded(): void {
    this.setDisassemblyExpanded(!this.disassemblyExpand.classList.contains("invisible"));
  }

  disassemblyExpandUpdate(newState: boolean): void {
    window.sessionStorage.setItem("expandedState-disassembly", `${newState}`);
    this.disassemblyExpand.classList.toggle("invisible", newState);
    this.disassemblyCollapse.classList.toggle("invisible", !newState);
  }

  setDisassemblyExpanded(newState: boolean): void {
    if (this.disassemblyExpand.classList.contains("invisible") === newState) return;
    const resizer = this.resizer;
    this.disassemblyExpandUpdate(newState);
    if (newState) {
      resizer.sep_right = resizer.sep_right_snap;
      resizer.sep_right_snap = resizer.client_width;
    } else {
      resizer.sep_right_snap = resizer.sep_right;
      resizer.sep_right = resizer.client_width;
    }
    resizer.updatePanes();
  }

  panesUpdated(): void {
    this.sourceExpandUpdate(this.resizer.sep_left > this.resizer.dead_width);
    this.disassemblyExpandUpdate(this.resizer.sep_right <
      (this.resizer.client_width - this.resizer.dead_width));
  }
}

class Resizer {
  snapper: Snapper;
  dead_width: number;
  client_width: number;
  left: HTMLElement;
  right: HTMLElement;
  middle: HTMLElement;
  sep_left: number;
  sep_right: number;
  sep_left_snap: number;
  sep_right_snap: number;
  sep_width_offset: number;
  panes_updated_callback: () => void;
  resizer_right: d3.Selection<HTMLDivElement, any, any, any>;
  resizer_left: d3.Selection<HTMLDivElement, any, any, any>;

  constructor(panes_updated_callback: () => void, dead_width: number) {
    let resizer = this;
    resizer.panes_updated_callback = panes_updated_callback;
    resizer.dead_width = dead_width
    resizer.left = document.getElementById(C.SOURCE_PANE_ID);
    resizer.middle = document.getElementById(C.INTERMEDIATE_PANE_ID);
    resizer.right = document.getElementById(C.GENERATED_PANE_ID);
    resizer.resizer_left = d3.select('#resizer-left');
    resizer.resizer_right = d3.select('#resizer-right');
    resizer.sep_left_snap = 0;
    resizer.sep_right_snap = 0;
    // Offset to prevent resizers from sliding slightly over one another.
    resizer.sep_width_offset = 7;
    this.updateWidths();

    let dragResizeLeft = d3.drag()
      .on('drag', function () {
        let x = d3.mouse(this.parentElement)[0];
        resizer.sep_left = Math.min(Math.max(0, x), resizer.sep_right - resizer.sep_width_offset);
        resizer.updatePanes();
      })
      .on('start', function () {
        resizer.resizer_left.classed("dragged", true);
        let x = d3.mouse(this.parentElement)[0];
        if (x > dead_width) {
          resizer.sep_left_snap = resizer.sep_left;
        }
      })
      .on('end', function () {
        if (!resizer.isRightSnapped()) {
          window.sessionStorage.setItem("source-pane-width", `${resizer.sep_left / resizer.client_width}`);
        }
        resizer.resizer_left.classed("dragged", false);
      });
    resizer.resizer_left.call(dragResizeLeft);

    let dragResizeRight = d3.drag()
      .on('drag', function () {
        let x = d3.mouse(this.parentElement)[0];
        resizer.sep_right = Math.max(resizer.sep_left + resizer.sep_width_offset, Math.min(x, resizer.client_width));
        resizer.updatePanes();
      })
      .on('start', function () {
        resizer.resizer_right.classed("dragged", true);
        let x = d3.mouse(this.parentElement)[0];
        if (x < (resizer.client_width - dead_width)) {
          resizer.sep_right_snap = resizer.sep_right;
        }
      })
      .on('end', function () {
        if (!resizer.isRightSnapped()) {
          console.log(`disassembly-pane-width ${resizer.sep_right}`)
          window.sessionStorage.setItem("disassembly-pane-width", `${resizer.sep_right / resizer.client_width}`);
        }
        resizer.resizer_right.classed("dragged", false);
      });;
    resizer.resizer_right.call(dragResizeRight);
    window.onresize = function () {
      resizer.updateWidths();
      resizer.updatePanes();
    };
    resizer.snapper = new Snapper(resizer);
    resizer.snapper.restoreExpandedState();
  }

  isLeftSnapped() {
    return this.sep_left === 0;
  }

  isRightSnapped() {
    return this.sep_right >= this.client_width - 1;
  }

  updatePanes() {
    let left_snapped = this.isLeftSnapped();
    let right_snapped = this.isRightSnapped();
    this.resizer_left.classed("snapped", left_snapped);
    this.resizer_right.classed("snapped", right_snapped);
    this.left.style.width = this.sep_left + 'px';
    this.middle.style.width = (this.sep_right - this.sep_left) + 'px';
    this.right.style.width = (this.client_width - this.sep_right) + 'px';
    this.resizer_left.style('left', this.sep_left + 'px');
    this.resizer_right.style('right', (this.client_width - this.sep_right - 1) + 'px');

    this.snapper.panesUpdated();
    this.panes_updated_callback();
  }

  updateWidths() {
    this.client_width = document.body.getBoundingClientRect().width;
    const sep_left = window.sessionStorage.getItem("source-pane-width");
    this.sep_left = this.client_width * (sep_left ? Number.parseFloat(sep_left) : (1 / 3));
    const sep_right = window.sessionStorage.getItem("disassembly-pane-width");
    this.sep_right = this.client_width * (sep_right ? Number.parseFloat(sep_right) : (2 / 3));
  }
}

window.onload = function () {
  var multiview = null;
  var disassemblyView = null;
  var sourceViews = [];
  var selectionBroker = null;
  var sourceResolver = null;
  let resizer = new Resizer(panesUpdatedCallback, 100);

  function panesUpdatedCallback() {
    if (multiview) multiview.onresize();
  }

  function loadFile(txtRes: string) {
    // If the JSON isn't properly terminated, assume compiler crashed and
    // add best-guess empty termination
    if (txtRes[txtRes.length - 2] == ',') {
      txtRes += '{"name":"disassembly","type":"disassembly","data":""}]}';
    }
    try {
      sourceViews.forEach((sv) => sv.hide());
      if (multiview) multiview.hide();
      multiview = null;
      if (disassemblyView) disassemblyView.hide();
      sourceViews = [];
      sourceResolver = new SourceResolver();
      selectionBroker = new SelectionBroker(sourceResolver);

      const jsonObj = JSON.parse(txtRes);

      let fnc = null
      // Backwards compatibility.
      if (typeof jsonObj.function == 'string') {
        fnc = {
          functionName: fnc,
          sourceId: -1,
          startPosition: jsonObj.sourcePosition,
          endPosition: jsonObj.sourcePosition + jsonObj.source.length,
          sourceText: jsonObj.source,
          backwardsCompatibility: true
        };
      } else {
        fnc = Object.assign(jsonObj.function, { backwardsCompatibility: false });
      }

      sourceResolver.setInlinings(jsonObj.inlinings);
      sourceResolver.setSourceLineToBytecodePosition(jsonObj.sourceLineToBytecodePosition);
      sourceResolver.setSources(jsonObj.sources, fnc)
      sourceResolver.setNodePositionMap(jsonObj.nodePositions);
      sourceResolver.parsePhases(jsonObj.phases);

      const sourceTabsContainer = document.getElementById(C.SOURCE_PANE_ID);
      const sourceTabs = new Tabs(sourceTabsContainer);
      const [sourceTab, sourceContainer] = sourceTabs.addTabAndContent("Source")
      sourceContainer.classList.add("viewpane", "scrollable");
      sourceTabs.activateTab(sourceTab);
      sourceTabs.addTab("&#x2b;").classList.add("open-tab");
      let sourceView = new CodeView(sourceContainer, selectionBroker, sourceResolver, fnc, CodeMode.MAIN_SOURCE);
      sourceView.show(null, null);
      sourceViews.push(sourceView);

      sourceResolver.forEachSource((source) => {
        let sourceView = new CodeView(sourceContainer, selectionBroker, sourceResolver, source, CodeMode.INLINED_SOURCE);
        sourceView.show(null, null);
        sourceViews.push(sourceView);
      });

      const disassemblyTabsContainer = document.getElementById(C.GENERATED_PANE_ID);
      const disassemblyTabs = new Tabs(disassemblyTabsContainer);
      const [disassemblyTab, disassemblyContainer] = disassemblyTabs.addTabAndContent("Disassembly")
      disassemblyContainer.classList.add("viewpane", "scrollable");
      disassemblyTabs.activateTab(disassemblyTab);
      disassemblyTabs.addTab("&#x2b;").classList.add("open-tab");
      disassemblyView = new DisassemblyView(disassemblyContainer, selectionBroker);
      disassemblyView.initializeCode(fnc.sourceText);
      if (sourceResolver.disassemblyPhase) {
        disassemblyView.initializePerfProfile(jsonObj.eventCounts);
        disassemblyView.show(sourceResolver.disassemblyPhase.data, null);
      }

      multiview = new GraphMultiView(C.INTERMEDIATE_PANE_ID, selectionBroker, sourceResolver);
      multiview.show(jsonObj);
    } catch (err) {
      if (window.confirm("Error: Exception during load of TurboFan JSON file:\n" +
        "error: " + err.message + "\nDo you want to clear session storage?")) {
        window.sessionStorage.clear();
      }
      return;
    }
  }

  function initializeUploadHandlers() {
    // The <input> form #upload-helper with type file can't be a picture.
    // We hence keep it hidden, and forward the click from the picture
    // button #upload.
    d3.select("#upload").on("click",
      () => document.getElementById("upload-helper").click());
    d3.select("#upload-helper").on("change", function (this: HTMLInputElement) {
      const uploadFile = this.files && this.files[0];
      if (uploadFile) {
        const filereader = new FileReader();
        filereader.onload = () => {
          const txtRes = filereader.result;
          if (typeof txtRes == 'string')
            loadFile(txtRes);
        };
        filereader.readAsText(uploadFile);
      }
    });
  }

  initializeUploadHandlers();
  resizer.updatePanes();
};
