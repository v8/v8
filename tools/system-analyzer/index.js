// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';
// TODO(zc) make imports  work

// import  "./helper.js";

import '../splaytree.js';
import '../codemap.js';
import '../csvparser.js';
import '../consarray.js';
import '../profile.js';
import '../profile_view.js';
import '../logreader.js';
import '../arguments.js';
import '../SourceMap.js';

import './map-processor.js';
import './ic-processor.js';
import './map-model.js';

define(Array.prototype, 'histogram', function(mapFn) {
  let histogram = [];
  for (let i = 0; i < this.length; i++) {
    let value = this[i];
    let index = Math.round(mapFn(value))
    let bucket = histogram[index];
    if (bucket !== undefined) {
      bucket.push(value);
    } else {
      histogram[index] = [value];
    }
  }
  for (let i = 0; i < histogram.length; i++) {
    histogram[i] = histogram[i] || [];
  }
  return histogram;
});

Object.defineProperty(Edge.prototype, 'getColor', {
  value: function() {
    return transitionTypeToColor(this.type);
  }
});

// ===================================
// Controller logic of the application

// Event handlers
document.onkeydown = handleKeyDown;
function handleKeyDown(event) {
  stateGlobal.navigation = document.state.navigation;
  let nav = document.state.navigation;
  switch (event.key) {
    case 'ArrowUp':
      event.preventDefault();
      if (event.shiftKey) {
        nav.selectPrevEdge();
      } else {
        nav.moveInChunk(-1);
      }
      return false;
    case 'ArrowDown':
      event.preventDefault();
      if (event.shiftKey) {
        nav.selectNextEdge();
      } else {
        nav.moveInChunk(1);
      }
      return false;
    case 'ArrowLeft':
      nav.moveInChunks(false);
      break;
    case 'ArrowRight':
      nav.moveInChunks(true);
      break;
    case '+':
      nav.increaseTimelineResolution();
      break;
    case '-':
      nav.decreaseTimelineResolution();
      break;
  }
}

// Update application state
function updateDocumentState() {
  document.state = stateGlobal.state;
  try {
    document.state.timeline = stateGlobal.timeline;
  } catch (error) {
    console.log(error);
    console.log('cannot assign timeline to state!');
  }
}

// Map event log processing
function handleLoadTextMapProcessor(text) {
  let mapProcessor = new MapProcessor();
  return mapProcessor.processString(text);
}

// IC event file reading and log processing
/*
function loadFileIC(file) {
  let reader = new FileReader();
  reader.onload = function(evt) {
    let icProcessor = new CustomIcProcessor();
    icProcessor.processString(this.result);
    entries = icProcessor.entries;
    $('ic-panel').countSelect.innerHTML = entries.length;
    $('ic-panel').updateTable(entries);
  } reader.readAsText(file);
  $('ic-panel').initGroupKeySelect();
}
*/

function $(id) {
  return document.querySelector(id);
}

// holds the state of the application
let stateGlobal = Object.create(null);

// call when a new file uploaded
function globalDataUpload(e) {
  stateGlobal.timeline = e.detail;
  if (!e.detail) return;
  // instantiate the app logic
  stateGlobal.fileData = e.detail;
  stateGlobal.state = new State();
  stateGlobal.timeline = handleLoadTextMapProcessor(stateGlobal.fileData.chunk);
  updateDocumentState();
  // process the IC explorer
  loadFileIC(stateGlobal.fileData.file);
}

function globalSearchBarEvent(e) {
  if (!e.detail.isValidMap) return;
  document.state.map = e.detail.map;
}
