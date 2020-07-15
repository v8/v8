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

document.onkeydown = handleKeyDown;
function handleKeyDown(event) {
  stateGlobal.navigation = document.state.navigation;
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

// Update application state
function updateDocumentState(){
    document.state = stateGlobal.state;
    try {
      document.state.timeline = stateGlobal.timeline;
    } catch (error) {
      console.log(error);
      console.log("cannot assign timeline to state!");
    }
}

// Map event log processing
function handleLoadTextMapProcessor(text) {
    let mapProcessor = new MapProcessor();
    return mapProcessor.processString(text);
}

// IC event file reading and log processing

function loadFileIC(file) {
  let reader = new FileReader();
  reader.onload = function(evt) {
    let icProcessor = new CustomIcProcessor();
    icProcessor.processString(this.result);
    let entries = icProcessor.entries;
    $("ic-panel").entries = entries;
    $("ic-panel").count.innerHTML = entries.length;
  }
  reader.readAsText(file);
  $("ic-panel").initGroupKeySelect();
}

function $(id) { return document.querySelector(id); }

// holds the state of the application
let stateGlobal = Object.create(null);

// call when a new file uploaded
function handleDataUpload(e) {
  stateGlobal.timeline = e.detail;
  if(!e.detail) return;
  $('#container').style.display = 'block';
  // instantiate the app logic
  stateGlobal.fileData = e.detail;
  stateGlobal.state = new State('#map-panel','#timeline-panel');
  stateGlobal.timeline = handleLoadTextMapProcessor(stateGlobal.fileData.chunk);
  updateDocumentState();
  // process the IC explorer
  loadFileIC(stateGlobal.fileData.file);
}

function handleMapAddressSearch(e) {
  if(!e.detail.isValidMap) return;
  document.state.map = e.detail.map;
}

function showMaps(e) {
  // show maps on the view
  document.state.view.transitionView.showMaps(e.detail);
}

function handleSelectIc(e){
  if(!e.detail) return;
  // Set selected IC events on the View
  document.state.filteredEntries = e.detail;
}

class App {
  handleDataUpload = handleDataUpload;
  handleMapAddressSearch = handleMapAddressSearch;
  showMaps = showMaps;
  handleSelectIc = handleSelectIc;
}

export {App};
