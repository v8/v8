// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {$} from './helper.mjs';

class State {
  #mapTimeline;
  #icTimeline;
  #timeline;
  #transitions;
  constructor(mapPanelId, timelinePanelId) {
    this.mapPanel_ = $(mapPanelId);
    this.timelinePanel_ = $(timelinePanelId);
    this._navigation = new Navigation(this);
    this.timelinePanel_.addEventListener(
      'mapchange', e => this.handleMapChange(e));
    this.timelinePanel_.addEventListener(
      'showmaps', e => this.handleShowMaps(e));
    this.mapPanel_.addEventListener(
      'statemapchange', e => this.handleStateMapChange(e));
    this.mapPanel_.addEventListener(
      'selectmapdblclick', e => this.handleDblClickSelectMap(e));
    this.mapPanel_.addEventListener(
      'sourcepositionsclick', e => this.handleClickSourcePositions(e));
  }
  handleMapChange(e){
    this.map = e.detail;
  }
  handleShowMaps(e){
    this.mapPanel_.mapEntries = e.detail.filter();
  }
  get icTimeline() {
    return this.#icTimeline;
  }
  set icTimeline(value) {
    this.#icTimeline = value;
  }
  set transitions(value) {
    this.mapPanel_.transitions = value;
  }
  get timeline() {
    return this.#timeline;
  }
  set timeline(value) {
    this.#timeline = value;
    this.timelinePanel.timelineEntries = value;
    this.timelinePanel.updateTimeline(this.map);
    this.mapPanel_.timeline = this.timeline;
  }
  get chunks() {
    return this.timelinePanel.chunks;
  }
  get nofChunks() {
    return this.timelinePanel.nofChunks;
  }
  set nofChunks(count) {
    this.timelinePanel.nofChunks = count;
    this.timelinePanel.updateTimeline(this.map);
  }
  get mapPanel() {
    return this.mapPanel_;
  }
  get timelinePanel() {
    return this.timelinePanel_;
  }
  get navigation() {
    return this._navigation
  }
  get map() {
    return this.timelinePanel.map;
  }
  set map(value) {
    if(!value) return;
    this.timelinePanel.map = value;
    this._navigation.updateUrl();
    this.mapPanel.map = value;
  }
  get entries() {
    if (!this.map) return {};
    return {
      map: this.map.id, time: this.map.time
    }
  }

  handleClickSourcePositions(e){
    //TODO(zcankara) Handle source position
    console.log("source position map detail: ", e.detail);
  }

  handleDblClickSelectMap(e){
    //TODO(zcankara) Handle double clicked map
    console.log("double clicked map: ", e.detail);
  }

  handleStateMapChange(e){
    this.map = e.detail;
  }

}

class Navigation {
  constructor(state) {
    this.state = state;
  }
  get map() {
    return this.state.map
  }
  set map(value) {
    this.state.map = value
  }
  get chunks() {
    return this.state.chunks
  }

  increaseTimelineResolution() {
    this.state.nofChunks *= 1.5;
  }

  decreaseTimelineResolution() {
    this.state.nofChunks /= 1.5;
  }

  selectNextEdge() {
    if (!this.map) return;
    if (this.map.children.length != 1) return;
    this.map = this.map.children[0].to;
  }

  selectPrevEdge() {
    if (!this.map) return;
    if (!this.map.parent()) return;
    this.map = this.map.parent();
  }

  selectDefaultMap() {
    this.map = this.chunks[0].at(0);
  }
  moveInChunks(next) {
    if (!this.map) return this.selectDefaultMap();
    let chunkIndex = this.map.chunkIndex(this.chunks);
    let chunk = this.chunks[chunkIndex];
    let index = chunk.indexOf(this.map);
    if (next) {
      chunk = chunk.next(this.chunks);
    } else {
      chunk = chunk.prev(this.chunks);
    }
    if (!chunk) return;
    index = Math.min(index, chunk.size() - 1);
    this.map = chunk.at(index);
  }

  moveInChunk(delta) {
    if (!this.map) return this.selectDefaultMap();
    let chunkIndex = this.map.chunkIndex(this.chunks)
    let chunk = this.chunks[chunkIndex];
    let index = chunk.indexOf(this.map) + delta;
    let map;
    if (index < 0) {
      map = chunk.prev(this.chunks).last();
    } else if (index >= chunk.size()) {
      map = chunk.next(this.chunks).first()
    } else {
      map = chunk.at(index);
    }
    this.map = map;
  }

  updateUrl() {
    let entries = this.state.entries;
    let params = new URLSearchParams(entries);
    window.history.pushState(entries, '', '?' + params.toString());
  }
}


export { State };