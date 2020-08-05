// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import CustomIcProcessor from "./ic-processor.mjs";
import {State} from './app-model.mjs';
import {MapProcessor, V8Map} from './map-processor.mjs';
import {Chunk} from './timeline.mjs';
import {$} from './helper.mjs';
import './ic-panel.mjs';
import './timeline-panel.mjs';
import './map-panel.mjs';
import './log-file-reader.mjs';
class App {
  #state
  #view;
  constructor(fileReaderId, mapPanelId, timelinePanelId,
      icPanelId, mapTrackId, icTrackId) {
    this.#view = {
      logFileReader: $(fileReaderId),
      icPanel: $(icPanelId),
      mapPanel: $(mapPanelId),
      timelinePanel: $(timelinePanelId),
      mapTrack: $(mapTrackId),
      icTrack: $(icTrackId),
    }
    this.#state = new State();
    this.#view.logFileReader.addEventListener('fileuploadstart',
      e => this.handleFileUpload(e));
    this.#view.logFileReader.addEventListener('fileuploadend',
      e => this.handleDataUpload(e));
    this.toggleSwitch = $('.theme-switch input[type="checkbox"]');
    this.toggleSwitch.addEventListener('change', e => this.switchTheme(e));
    this.#view.timelinePanel.addEventListener(
      'mapchange', e => this.handleMapChange(e));
    this.#view.timelinePanel.addEventListener(
      'showmaps', e => this.handleShowMaps(e));
    this.#view.mapPanel.addEventListener(
      'mapchange', e => this.handleMapChange(e));
    this.#view.mapPanel.addEventListener(
      'selectmapdblclick', e => this.handleDblClickSelectMap(e));
    this.#view.mapPanel.addEventListener(
      'sourcepositionsclick', e => this.handleClickSourcePositions(e));
    this.#view.icPanel.addEventListener(
      'ictimefilter', e => this.handleICTimeFilter(e));
    this.#view.icPanel.addEventListener(
      'mapclick', e => this.handleMapClick(e));
    this.#view.mapPanel.addEventListener(
        'click', e => this.handleMapAddressSearch(e));
    this.#view.mapPanel.addEventListener(
      'change', e => this.handleShowMapsChange(e));
    this.#view.icPanel.addEventListener(
      'filepositionclick', e => this.handleFilePositionClick(e));
  }
  handleMapClick(e) {
    //TODO(zcankara) Direct the event based on the key and value
    console.log("map: ", e.detail.key);
  }
  handleFilePositionClick(e) {
    //TODO(zcankara) Direct the event based on the key and value
    console.log("filePosition: ", e.detail.key);
  }
  handleClickSourcePositions(e){
    //TODO(zcankara) Handle source position
    console.log("source position map detail: ", e.detail);
  }
  handleDblClickSelectMap(e){
    //TODO(zcankara) Handle double clicked map
    console.log("double clicked map: ", e.detail);
  }
  handleMapChange(e){
    if (!(e.detail instanceof V8Map)){
      console.error("selected entry not a V8Map instance");
      return;
    }
    this.#state.map = e.detail;
    this.#view.mapTrack.selectedEntry = e.detail;
    this.#view.mapPanel.map = e.detail;
  }
  handleShowMaps(e){
    if (!(e.detail instanceof Chunk)){
      console.error("Chunk not selected");
      return;
    }
    this.#view.mapPanel.mapEntries = e.detail.filter();
  }
  handleICTimeFilter(event) {
    this.#state.timeSelection.start = event.detail.startTime;
    this.#state.timeSelection.end = event.detail.endTime;
    this.#view.icTrack.data.selectTimeRange(this.#state.timeSelection.start,
      this.#state.timeSelection.end);
    this.#view.icPanel.filteredEntries = this.#view.icTrack.data.selection;
  }
  handleMapAddressSearch(e) {
    //TODO(zcankara) Combine with handleMapChange into selectMap event
    //TODO(zcankara) Possibility of select no map
    if(!e.detail.map) return;
    this.#state.map = e.detail.map;
    this.#view.mapTrack.selectedEntry = e.detail.map;
    this.#view.mapPanel.map = e.detail.map;
  }
  handleShowMapsChange(e) {
    //TODO(zcankara) Map entries repeats "map". Probabluy not needed
    this.#view.mapPanel.mapEntries = e.detail;
  }
  handleFileUpload(e){
    //TODO(zcankara) Set a state on the document.body. Exe: .loading, .loaded
    $('#container').style.display = 'none';
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
      //TODO(zcankara) Set the data of the State object first
      this.#state.icTimeline = icProcessor.processString(fileData.chunk);
      this.#view.icTrack.data = this.#state.icTimeline;
      this.#view.icPanel.filteredEntries = this.#view.icTrack.data.all;
      this.#view.icPanel.count.innerHTML = this.#view.icTrack.data.all.length;
    }
    reader.readAsText(fileData.file);
    this.#view.icPanel.initGroupKeySelect();
  }

  // call when a new file uploaded
  handleDataUpload(e) {
    if(!e.detail) return;
    $('#container').style.display = 'block';
    // instantiate the app logic
    let fileData = e.detail;
    try {
      const timeline = this.handleLoadTextMapProcessor(fileData.chunk);
      // Transitions must be set before timeline for stats panel.
      this.#state.mapTimeline = timeline;
      this.#view.mapPanel.transitions = this.#state.mapTimeline.transitions;
      this.#view.mapTrack.data = this.#state.mapTimeline;
      this.#state.chunks = this.#view.mapTrack.chunks;
      this.#view.mapPanel.timeline = this.#state.mapTimeline;
    } catch (error) {
      console.log(error);
    }
    this.loadICLogFile(fileData);
    this.fileLoaded = true;
  }

  refreshTimelineTrackView(){
    this.#view.mapTrack.data = this.#state.mapTimeline;
    this.#view.icTrack.data = this.#state.icTimeline;
  }

  switchTheme(event) {
    document.documentElement.dataset.theme =
      event.target.checked ? 'light' : 'dark';
    if(this.fileLoaded) {
      this.refreshTimelineTrackView();
    }
  }
}


export {App};
