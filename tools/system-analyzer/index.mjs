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
  #mapPanel;
  #timelinePanel;
  #icPanel;
  #mapTrack;
  #icTrack;
  #logFileReader;
  constructor(fileReaderId, mapPanelId, timelinePanelId,
      icPanelId, mapTrackId, icTrackId) {
    this.#logFileReader = this.$(fileReaderId);
    this.#mapPanel = this.$(mapPanelId);
    this.#timelinePanel = this.$(timelinePanelId);
    this.#icPanel = this.$(icPanelId);
    this.#mapTrack = this.$(mapTrackId);
    this.#icTrack = this.$(icTrackId);

    this.#logFileReader.addEventListener('fileuploadstart',
      e => this.handleFileUpload(e));
    this.#logFileReader.addEventListener('fileuploadend',
      e => this.handleDataUpload(e));
    document.addEventListener('keydown', e => this.handleKeyDown(e));
    this.#icPanel.addEventListener(
      'ictimefilter', e => this.handleICTimeFilter(e));
    this.#icPanel.addEventListener(
      'mapclick', e => this.handleMapClick(e));
    this.#icPanel.addEventListener(
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
    this.#icTrack.data.selectTimeRange(this.#timeSelection.start,
      this.#timeSelection.end);
    this.#icPanel.filteredEntries = this.#icTrack.data.selection;
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
      //TODO(zcankara) Exe: this.#icPanel.timeline = document.state.icTimeline
      this.#icTrack.data = icProcessor.processString(fileData.chunk);
      this.#icPanel.filteredEntries = this.#icTrack.data.all;
      this.#icPanel.count.innerHTML = this.#icTrack.data.all.length;
    }
    reader.readAsText(fileData.file);
    this.#icPanel.initGroupKeySelect();
  }

  // call when a new file uploaded
  handleDataUpload(e) {
    if(!e.detail) return;
    this.$('#container').style.display = 'block';
    // instantiate the app logic
    let fileData = e.detail;
    document.state = new State(this.#mapPanel, this.#timelinePanel,
      this.#mapTrack, this.#icTrack);
    try {
      const timeline = this.handleLoadTextMapProcessor(fileData.chunk);
      // Transitions must be set before timeline for stats panel.
      document.state.transitions= timeline.transitions;
      this.#mapTrack.data = timeline;
      document.state.mapTimeline = timeline;
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
