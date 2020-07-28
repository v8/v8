// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import CustomIcProcessor from "./ic-processor.mjs";
import {State} from './app-model.mjs';
import {MapProcessor} from './map-processor.mjs';
import './ic-panel.mjs';
import './timeline-panel.mjs';
import './map-panel.mjs';
import './log-file-reader.mjs';
class App {
  #timeSelection = {start: 0, end: Infinity};
  constructor(fileReaderId, mapPanelId, timelinePanelId, icPanelId) {
    this.mapPanelId_ =  mapPanelId;
    this.timelinePanelId_ =  timelinePanelId;
    this.icPanelId_ =  icPanelId;
    this.icPanel_ = this.$(icPanelId);
    this.fileLoaded = false;
    this.logFileReader_ = this.$(fileReaderId);
    this.logFileReader_.addEventListener('fileuploadstart',
      e => this.handleFileUpload(e));
    this.logFileReader_.addEventListener('fileuploadend',
      e => this.handleDataUpload(e));
    document.addEventListener('keydown', e => this.handleKeyDown(e));
    this.icPanel_.addEventListener(
      'ictimefilter', e => this.handleICTimeFilter(e));
    this.icPanel_.addEventListener(
      'mapclick', e => this.handleMapClick(e));
    this.icPanel_.addEventListener(
      'filepositionclick', e => this.handleFilePositionClick(e));
    this.toggleSwitch = this.$('.theme-switch input[type="checkbox"]');
    this.toggleSwitch.addEventListener('change', e => this.switchTheme(e));
  }

  handleFileUpload(e){
    this.$('#container').style.display = 'none';
  }
  handleMapClick(e) {
     //TODO(zcankara) Direct the event based on the key and value
     console.log("map: ", e.detail.key);
  }
  handleFilePositionClick(e) {
    //TODO(zcankara) Direct the event based on the key and value
    console.log("filePosition: ", e.detail.key);
  }

  handleICTimeFilter(event) {
    this.#timeSelection.start = event.detail.startTime;
    this.#timeSelection.end = event.detail.endTime;
    document.state.icTimeline.selectTimeRange(this.#timeSelection.start,
      this.#timeSelection.end);
    this.icPanel_.filteredEntries = document.state.icTimeline.selection;
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
      //TODO(zcankara) Assign timeline directly to the ic panel
      //TODO(zcankara) Exe: this.icPanel_.timeline = document.state.icTimeline
      document.state.icTimeline = icProcessor.processString(fileData.chunk);
      this.icPanel_.filteredEntries = document.state.icTimeline.all;
      this.icPanel_.count.innerHTML = document.state.icTimeline.all.length;
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
      const timeline = this.handleLoadTextMapProcessor(fileData.chunk);
      // Transitions must be set before timeline for stats panel.
      document.state.transitions= timeline.transitions;
      document.state.timeline = timeline;
    } catch (error) {
      console.log(error);
    }
    this.loadICLogFile(fileData);
    this.fileLoaded = true;
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
    //TODO(zcankara) Send filtered entries to State
    console.log("filtered IC entried: ", e.detail)
  }

  switchTheme(event) {
    if(this.fileLoaded) return;
    document.documentElement.dataset.theme =
      event.target.checked ? 'dark' : 'light';
  }
}


export {App};
