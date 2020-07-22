// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import CustomIcProcessor from "./ic-processor.mjs";
import {State} from './map-model.mjs';
import {MapProcessor} from './map-processor.mjs';
import './ic-panel.mjs';
import './timeline-panel.mjs';
import './map-panel.mjs';
import './log-file-reader.mjs';

class App {
  constructor(mapPanelId, timelinePanelId, icPanelId) {
    this.mapPanelId_ =  mapPanelId;
    this.timelinePanelId_ =  timelinePanelId;
    this.icPanelId_ =  icPanelId;
    this.icPanel_ = this.$(icPanelId);
    document.addEventListener('keydown', e => this.handleKeyDown(e));
  }

  $(id) { return document.querySelector(id); }

    handleKeyDown(event) {
    let nav = document.state.navigation;
    switch(event.key) {
      case "ArrowUp":
        event.preventDefault();
        if (event.shiftKey) {
          nav.selectPrevEdge();
        } else {
          nav.moveInChunk(-1);
        }
        return false;
      case "ArrowDown":
        event.preventDefault();
        if (event.shiftKey) {
          nav.selectNextEdge();
        } else {
          nav.moveInChunk(1);
        }
        return false;
      case "ArrowLeft":
        nav.moveInChunks(false);
        break;
      case "ArrowRight":
        nav.moveInChunks(true);
        break;
      case "+":
        nav.increaseTimelineResolution();
        break;
      case "-":
        nav.decreaseTimelineResolution();
        break;
    }
  }

  // Map event log processing
  handleLoadTextMapProcessor(text) {
    let mapProcessor = new MapProcessor();
    return mapProcessor.processString(text);
  }

  // IC event file reading and log processing
  loadICLogFile(fileData) {
    let reader = new FileReader();
    reader.onload = (evt) => {
      let icProcessor = new CustomIcProcessor();
      icProcessor.processString(fileData.chunk);
      let entries = icProcessor.entries;
      this.icPanel_.entries = entries;
      this.icPanel_.count.innerHTML = entries.length;
    }
    reader.readAsText(fileData.file);
    this.icPanel_.initGroupKeySelect();
  }

  // call when a new file uploaded
  handleDataUpload(e) {
    if(!e.detail) return;
    this.$('#container').style.display = 'block';
    // instantiate the app logic
    let fileData = e.detail;
    document.state = new State(this.mapPanelId_, this.timelinePanelId_);
    try {
      document.state.timeline = this.handleLoadTextMapProcessor(fileData.chunk);
    } catch (error) {
      console.log(error);
    }
    this.loadICLogFile(fileData);
  }

  handleMapAddressSearch(e) {
    if(!e.detail.isValidMap) return;
    document.state.map = e.detail.map;
  }

  handleShowMaps(e) {
    document.state.mapPanel.mapEntries = e.detail;
  }

  handleSelectIc(e){
    if(!e.detail) return;
    // Set selected IC events on the View
    document.state.filteredEntries = e.detail;
  }
}

export {App};
