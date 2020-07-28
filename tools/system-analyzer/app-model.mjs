// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { V8Map } from './map-processor.mjs';

class State {
  #mapPanel;
  #timelinePanel;
  #mapTrack;
  #icTrack;
  #map;
  #ic;
  #navigation;
  constructor(mapPanel, timelinePanel, mapTrack, icTrack) {
    this.#mapPanel = mapPanel;
    this.#timelinePanel = timelinePanel;
    this.#mapTrack = mapTrack;
    this.#icTrack = icTrack;
    this.#navigation = new Navigation(this);
    this.#timelinePanel.addEventListener(
      'mapchange', e => this.handleMapChange(e));
    this.#timelinePanel.addEventListener(
      'showmaps', e => this.handleShowMaps(e));
    this.#mapPanel.addEventListener(
      'mapchange', e => this.handleMapChange(e));
    this.#mapPanel.addEventListener(
      'selectmapdblclick', e => this.handleDblClickSelectMap(e));
    this.#mapPanel.addEventListener(
      'sourcepositionsclick', e => this.handleClickSourcePositions(e));
  }
  get chunks(){
    //TODO(zcankara) for timeline dependency
    return this.#mapTrack.chunks;
  }
  handleMapChange(e){
    if (!(e.detail instanceof V8Map)) return;
    this.map = e.detail;
  }
  handleStateMapChange(e){
    this.map = e.detail;
  }
  handleShowMaps(e){
    if (!(e.detail instanceof V8Map)) return;
    this.#mapPanel.mapEntries = e.detail.filter();
  }
  set transitions(value) {
    this.#mapPanel.transitions = value;
  }
  set mapTimeline(value) {
    this.#mapPanel.timeline = value;
  }
  get nofChunks() {
    return this.timelinePanel.nofChunks;
  }
  set nofChunks(count) {
    this.timelinePanel.nofChunks = count;
  }
  get mapPanel() {
    return this.#mapPanel;
  }
  get timelinePanel() {
    return this.#timelinePanel;
  }
  get navigation() {
    return this.#navigation
  }

  get map() {
    return this.#map;
  }
  set map(value) {
    if(!value) return;
    this.#map = value;
    this.#mapTrack.selectedEntry = value;
    this.#navigation.updateUrl();
    this.mapPanel.map = value;
  }
  get ic() {
    return this.#ic;
  }
  set ic(value) {
    if(!value) return;
    this.#ic = value;
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
