// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {kChunkWidth, kChunkHeight} from './map-processor.mjs';
import {div, removeAllChildren, $} from './helper.mjs';



class State {
  constructor(mapPanelId, timelinePanelId) {
    this._nofChunks = 400;
    this._map = undefined;
    this._timeline = undefined;
    this._chunks = undefined;
    this._view = new View(this, mapPanelId, timelinePanelId);
    //TODO(zcankara) Depreciate the view
    this.mapPanel_ = $(mapPanelId);
    this._navigation = new Navigation(this, this.view);
  }
  set filteredEntries(value) {
    this._filteredEntries = value;
    if (this._filteredEntries) {
      //TODO(zcankara) update timeline view
    }
  }
  get filteredEntries() {
    return this._filteredEntries;
  }
  set entries(value) {
    this._entries = value;
  }
  get entries() {
    return this._entries;
  }

  get timeline() {
    return this._timeline
  }
  set timeline(value) {
    this._timeline = value;
    this.updateChunks();
    this.view.updateTimeline();
    this.mapPanel_.updateStats(this.timeline);
  }
  get chunks() {
    return this._chunks
  }
  get nofChunks() {
    return this._nofChunks
  }
  set nofChunks(count) {
    this._nofChunks = count;
    this.updateChunks();
    this.view.updateTimeline();
  }
  get mapPanel() {
    return this.mapPanel_;
  }
  get view() {
    return this._view
  }
  get navigation() {
    return this._navigation
  }
  get map() {
    return this._map
  }
  set map(value) {
    this._map = value;
    this._navigation.updateUrl();
    this.mapPanel_.map = this._map;
    this.view.redraw();
  }
  updateChunks() {
    this._chunks = this._timeline.chunks(this._nofChunks);
  }
  get entries() {
    if (!this.map) return {};
    return {
      map: this.map.id, time: this.map.time
    }
  }
}

class Navigation {
  constructor(state, view) {
    this.state = state;
    this.view = view;
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

class View {
  constructor(state, mapPanelId, timelinePanelId) {
    this.mapPanel_ = $(mapPanelId);
    this.mapPanel_.addEventListener(
      'statemapchange', e => this.handleStateMapChange(e));
    this.mapPanel_.addEventListener(
      'selectmapdblclick', e => this.handleDblClickSelectMap(e));
    this.mapPanel_.addEventListener(
      'sourcepositionsclick', e => this.handleClickSourcePositions(e));

    this.timelinePanel_ = $(timelinePanelId);
    this.state = state;
    setInterval(this.timelinePanel_.updateOverviewWindow(), 50);
    this.timelinePanel_.createBackgroundCanvas();
    this.isLocked = false;
    this._filteredEntries = [];
  }
  get chunks() {
    return this.state.chunks
  }
  get timeline() {
    return this.state.timeline
  }
  get map() {
    return this.state.map
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
    this.state.map = e.detail;
  }

  handleIsLocked(e){
    this.state.view.isLocked = e.detail;
  }

  updateTimeline() {
    let chunksNode = this.timelinePanel_.timelineChunks;
    removeAllChildren(chunksNode);
    let chunks = this.chunks;
    let max = chunks.max(each => each.size());
    let start = this.timeline.startTime;
    let end = this.timeline.endTime;
    let duration = end - start;
    const timeToPixel = chunks.length * kChunkWidth / duration;
    let addTimestamp = (time, name) => {
      let timeNode = div('timestamp');
      timeNode.innerText = name;
      timeNode.style.left = ((time - start) * timeToPixel) + 'px';
      chunksNode.appendChild(timeNode);
    };
    let backgroundTodo = [];
    for (let i = 0; i < chunks.length; i++) {
      let chunk = chunks[i];
      let height = (chunk.size() / max * kChunkHeight);
      chunk.height = height;
      if (chunk.isEmpty()) continue;
      let node = div();
      node.className = 'chunk';
      node.style.left = (i * kChunkWidth) + 'px';
      node.style.height = height + 'px';
      node.chunk = chunk;
      node.addEventListener('mousemove', e => this.handleChunkMouseMove(e));
      node.addEventListener('click', e => this.handleChunkClick(e));
      node.addEventListener('dblclick', e => this.handleChunkDoubleClick(e));
      backgroundTodo.push([chunk, node])
      chunksNode.appendChild(node);
      chunk.markers.forEach(marker => addTimestamp(marker.time, marker.name));
    }


    this.timelinePanel_.asyncSetTimelineChunkBackground(backgroundTodo)

    // Put a time marker roughly every 20 chunks.
    let expected = duration / chunks.length * 20;
    let interval = (10 ** Math.floor(Math.log10(expected)));
    let correction = Math.log10(expected / interval);
    correction = (correction < 0.33) ? 1 : (correction < 0.75) ? 2.5 : 5;
    interval *= correction;

    let time = start;
    while (time < end) {
      addTimestamp(time, ((time - start) / 1000) + ' ms');
      time += interval;
    }
    this.drawOverview();
    this.redraw();
  }


  handleChunkMouseMove(event) {
    if (this.isLocked) return false;
    let chunk = event.target.chunk;
    if (!chunk) return;
    // topmost map (at chunk.height) == map #0.
    let relativeIndex =
        Math.round(event.layerY / event.target.offsetHeight * chunk.size());
    let map = chunk.at(relativeIndex);
    this.state.map = map;
  }

  handleChunkClick(event) {
    this.isLocked = !this.isLocked;
  }

  handleChunkDoubleClick(event) {
    this.isLocked = true;
    let chunk = event.target.chunk;
    if (!chunk) return;
    this.state.view.isLocked = true;
    this.mapPanel_.mapEntries = chunk.getUniqueTransitions();
  }

  drawOverview() {
    const height = 50;
    const kFactor = 2;
    //let canvas = this.backgroundCanvas;
    let canvas = this.timelinePanel_.backgroundCanvas;
    canvas.height = height;
    canvas.width = window.innerWidth;
    let ctx = canvas.getContext('2d');
    let chunks = this.state.timeline.chunkSizes(canvas.width * kFactor);
    let max = chunks.max();

    ctx.clearRect(0, 0, canvas.width, height);
    ctx.fillStyle = 'white';
    ctx.beginPath();
    ctx.moveTo(0, height);
    for (let i = 0; i < chunks.length; i++) {
      ctx.lineTo(i / kFactor, height - chunks[i] / max * height);
    }
    ctx.lineTo(chunks.length, height);
    ctx.strokeStyle = 'white';
    ctx.stroke();
    ctx.closePath();
    ctx.fill();
    let imageData = canvas.toDataURL('image/webp', 0.2);
    this.timelinePanel_.timelineOverview.style.backgroundImage =
        'url(' + imageData + ')';
  }

  redraw() {
    let canvas = this.timelinePanel_.timelineCanvas;
    canvas.width = (this.chunks.length + 1) * kChunkWidth;
    canvas.height = kChunkHeight;
    let ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, kChunkHeight);
    if (!this.state.map) return;
    //TODO(zcankara) Redraw the IC events on canvas.
    this.drawEdges(ctx);
  }

  setMapStyle(map, ctx) {
    ctx.fillStyle = map.edge && map.edge.from ? 'white' : '#aedc6e';
  }

  setEdgeStyle(edge, ctx) {
    let color = edge.getColor();
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
  }

  markMap(ctx, map) {
    let [x, y] = map.position(this.state.chunks);
    ctx.beginPath();
    this.setMapStyle(map, ctx);
    ctx.arc(x, y, 3, 0, 2 * Math.PI);
    ctx.fill();
    ctx.beginPath();
    ctx.fillStyle = 'white';
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();
  }

  markSelectedMap(ctx, map) {
    let [x, y] = map.position(this.state.chunks);
    ctx.beginPath();
    this.setMapStyle(map, ctx);
    ctx.arc(x, y, 6, 0, 2 * Math.PI);
    ctx.strokeStyle = 'white';
    ctx.stroke();
  }

  drawEdges(ctx) {
    // Draw the trace of maps in reverse order to make sure the outgoing
    // transitions of previous maps aren't drawn over.
    const kMaxOutgoingEdges = 100;
    let nofEdges = 0;
    let stack = [];
    let current = this.state.map;
    while (current && nofEdges < kMaxOutgoingEdges) {
      nofEdges += current.children.length;
      stack.push(current);
      current = current.parent();
    }
    ctx.save();
    this.drawOutgoingEdges(ctx, this.state.map, 3);
    ctx.restore();

    let labelOffset = 15;
    let xPrev = 0;
    while (current = stack.pop()) {
      if (current.edge) {
        this.setEdgeStyle(current.edge, ctx);
        let [xTo, yTo] = this.drawEdge(ctx, current.edge, true, labelOffset);
        if (xTo == xPrev) {
          labelOffset += 8;
        } else {
          labelOffset = 15
        }
        xPrev = xTo;
      }
      this.markMap(ctx, current);
      current = current.parent();
      ctx.save();
      // this.drawOutgoingEdges(ctx, current, 1);
      ctx.restore();
    }
    // Mark selected map
    this.markSelectedMap(ctx, this.state.map);
  }

  drawEdge(ctx, edge, showLabel = true, labelOffset = 20) {
    if (!edge.from || !edge.to) return [-1, -1];
    let [xFrom, yFrom] = edge.from.position(this.chunks);
    let [xTo, yTo] = edge.to.position(this.chunks);
    let sameChunk = xTo == xFrom;
    if (sameChunk) labelOffset += 8;

    ctx.beginPath();
    ctx.moveTo(xFrom, yFrom);
    let offsetX = 20;
    let offsetY = 20;
    let midX = xFrom + (xTo - xFrom) / 2;
    let midY = (yFrom + yTo) / 2 - 100;
    if (!sameChunk) {
      ctx.quadraticCurveTo(midX, midY, xTo, yTo);
    } else {
      ctx.lineTo(xTo, yTo);
    }
    if (!showLabel) {
      ctx.strokeStyle = 'white';
      ctx.stroke();
    } else {
      let centerX, centerY;
      if (!sameChunk) {
        centerX = (xFrom / 2 + midX + xTo / 2) / 2;
        centerY = (yFrom / 2 + midY + yTo / 2) / 2;
      } else {
        centerX = xTo;
        centerY = yTo;
      }
      ctx.strokeStyle = 'white';
      ctx.moveTo(centerX, centerY);
      ctx.lineTo(centerX + offsetX, centerY - labelOffset);
      ctx.stroke();
      ctx.textAlign = 'left';
      ctx.fillStyle = 'white';
      ctx.fillText(
          edge.toString(), centerX + offsetX + 2, centerY - labelOffset)
    }
    return [xTo, yTo];
  }

  drawOutgoingEdges(ctx, map, max = 10, depth = 0) {
    if (!map) return;
    if (depth >= max) return;
    ctx.globalAlpha = 0.5 - depth * (0.3 / max);
    ctx.strokeStyle = '#666';

    const limit = Math.min(map.children.length, 100)
    for (let i = 0; i < limit; i++) {
      let edge = map.children[i];
      this.drawEdge(ctx, edge, true);
      this.drawOutgoingEdges(ctx, edge.to, max, depth + 1);
    }
  }
}

export { State };
