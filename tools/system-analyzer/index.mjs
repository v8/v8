// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import CustomIcProcessor from "./ic-processor.mjs";
import {Entry} from "./ic-processor.mjs";
import {State} from './app-model.mjs';
import {MapProcessor, V8Map} from './map-processor.mjs';
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
    this.toggleSwitch = $('.theme-switch input[type="checkbox"]');
    this.toggleSwitch.addEventListener('change', e => this.switchTheme(e));
    this.#view.logFileReader.addEventListener('fileuploadstart',
      e => this.handleFileUpload(e));
    this.#view.logFileReader.addEventListener('fileuploadend',
      e => this.handleDataUpload(e));
    Object.entries(this.#view).forEach(([_, value]) => {
      value.addEventListener('showentries',
        e => this.handleShowEntries(e));
      value.addEventListener('showentrydetail',
        e => this.handleShowEntryDetail(e));
    });
    this.#view.icPanel.addEventListener(
      'ictimefilter', e => this.handleICTimeFilter(e));
  }
  handleShowEntries(e){
    if(e.entries[0] instanceof V8Map){
      this.#view.mapPanel.mapEntries = e.entries;
    }
  }
  handleShowEntryDetail(e){
    if(e.entry instanceof V8Map){
      this.selectMapLogEvent(e.entry);
    }
    else if(e.entry instanceof Entry){
      this.selectICLogEvent(e.entry);
    }
    else if(typeof e.entry === 'string'){
      this.selectSourcePositionEvent(e.entry);
    }
    else {
      console.log("undefined");
    }
  }
  handleClickSourcePositions(e){
    //TODO(zcankara) Handle source position
    console.log("Entry containing source position: ", e.entries);
  }
  selectMapLogEvent(entry){
    this.#state.map = entry;
    this.#view.mapTrack.selectedEntry = entry;
    this.#view.mapPanel.map = entry;
  }
  selectICLogEvent(entry){
    console.log("IC Entry selection");
  }
  selectSourcePositionEvent(sourcePositions){
    console.log("source positions: ", sourcePositions);
  }
  handleICTimeFilter(event) {
    this.#state.timeSelection.start = event.detail.startTime;
    this.#state.timeSelection.end = event.detail.endTime;
    this.#view.icTrack.data.selectTimeRange(this.#state.timeSelection.start,
      this.#state.timeSelection.end);
    this.#view.icPanel.filteredEntries = this.#view.icTrack.data.selection;
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
