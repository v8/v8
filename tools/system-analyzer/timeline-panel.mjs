// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {defineCustomElement, V8CustomElement} from './helper.mjs';
import {kChunkWidth, kChunkHeight} from './map-processor.mjs';

defineCustomElement('timeline-panel', (templateText) =>
 class TimelinePanel extends V8CustomElement {
  constructor() {
    super(templateText);
    this.timelineOverview.addEventListener(
        'mousemove', e => this.handleTimelineIndicatorMove(e));
    setInterval(this.updateOverviewWindow(), 50);
    this.backgroundCanvas = document.createElement('canvas');
    this.isLocked = false;
  }
  #timelineEntries;
  #nofChunks = 400;
  #chunks;
  #map;

  get timelineOverview() {
    return this.$('#timelineOverview');
  }

  get timelineOverviewIndicator() {
    return this.$('#timelineOverviewIndicator');
  }

  get timelineCanvas() {
    return this.$('#timelineCanvas');
  }

  get timelineChunks() {
    return this.$('#timelineChunks');
  }

  get timeline() {
    return this.$('#timeline');
  }

  set timelineEntries(value){
    this.#timelineEntries = value;
    this.updateChunks();
  }
  get timelineEntries(){
    return this.#timelineEntries;
  }
  set nofChunks(count){
    this.#nofChunks = count;
    this.updateChunks();
  }
  get nofChunks(){
    return this.#nofChunks;
  }
  updateChunks() {
    this.#chunks = this.timelineEntries.chunks(this.nofChunks);
  }
  get chunks(){
    return this.#chunks;
  }
  set map(value){
    this.#map = value;
    this.redraw(this.map);
  }
  get map(){
    return this.#map;
  }

  handleTimelineIndicatorMove(event) {
    if (event.buttons == 0) return;
    let timelineTotalWidth = this.timelineCanvas.offsetWidth;
    let factor = this.timelineOverview.offsetWidth / timelineTotalWidth;
    this.timeline.scrollLeft += event.movementX / factor;
  }

  updateOverviewWindow() {
    let indicator = this.timelineOverviewIndicator;
    let totalIndicatorWidth =
        this.timelineOverview.offsetWidth;
    let div = this.timeline;
    let timelineTotalWidth =
        this.timelineCanvas.offsetWidth;
    let factor = totalIndicatorWidth / timelineTotalWidth;
    let width = div.offsetWidth * factor;
    let left = div.scrollLeft * factor;
    indicator.style.width = width + 'px';
    indicator.style.left = left + 'px';
  }

  asyncSetTimelineChunkBackground(backgroundTodo) {
    const kIncrement = 100;
    let start = 0;
    let delay = 1;
    while (start < backgroundTodo.length) {
      let end = Math.min(start + kIncrement, backgroundTodo.length);
      setTimeout((from, to) => {
        for (let i = from; i < to; i++) {
          let [chunk, node] = backgroundTodo[i];
          this.setTimelineChunkBackground(chunk, node);
        }
      }, delay++, start, end);
      start = end;
    }
  }

  setTimelineChunkBackground(chunk, node) {
    // Render the types of transitions as bar charts
    const kHeight = chunk.height;
    const kWidth = 1;
    this.backgroundCanvas.width = kWidth;
    this.backgroundCanvas.height = kHeight;
    let ctx = this.backgroundCanvas.getContext('2d');
    ctx.clearRect(0, 0, kWidth, kHeight);
    let y = 0;
    let total = chunk.size();
    let type, count;
    if (true) {
      chunk.getTransitionBreakdown().forEach(([type, count]) => {
        ctx.fillStyle = this.transitionTypeToColor(type);
        let height = count / total * kHeight;
        ctx.fillRect(0, y, kWidth, y + height);
        y += height;
      });
    } else {
      chunk.items.forEach(map => {
        ctx.fillStyle = this.transitionTypeToColor(map.getType());
        let y = chunk.yOffset(map);
        ctx.fillRect(0, y, kWidth, y + 1);
      });
    }

    let imageData = this.backgroundCanvas.toDataURL('image/webp', 0.2);
    node.style.backgroundImage = 'url(' + imageData + ')';
  }

  transitionTypeToColor(type) {
    switch (type) {
      case 'new':
        // green
        return '#aedc6e';
      case 'Normalize':
        // violet
        return '#d26edc';
      case 'SlowToFast':
        // orange
        return '#dc9b6e';
      case 'InitialMap':
        // yellow
        return '#EEFF41';
      case 'Transition':
        // pink/violet (primary)
        return '#9B6EDC';
      case 'ReplaceDescriptors':
        // red
        return '#dc6eae';
    }
    // pink/violet (primary)
    return '#9B6EDC';
  }

  // TODO(zcankara) Timeline colors
  updateTimeline(isMapSelected) {
    let chunksNode = this.timelineChunks;
    this.removeAllChildren(chunksNode);
    let chunks = this.chunks;
    let max = chunks.max(each => each.size());
    let start = this.timelineEntries.startTime;
    let end = this.timelineEntries.endTime;
    let duration = end - start;
    const timeToPixel = chunks.length * kChunkWidth / duration;
    let addTimestamp = (time, name) => {
      let timeNode = this.div('timestamp');
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
      let node = this.div();
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
    this.asyncSetTimelineChunkBackground(backgroundTodo)

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
    this.redraw(isMapSelected);
  }

  handleChunkMouseMove(event) {
    if (this.isLocked) return false;
    let chunk = event.target.chunk;
    if (!chunk) return;
    // topmost map (at chunk.height) == map #0.
    let relativeIndex =
        Math.round(event.layerY / event.target.offsetHeight * chunk.size());
    let map = chunk.at(relativeIndex);
    this.dispatchEvent(new CustomEvent(
      'mapchange', {bubbles: true, composed: true, detail: map}));
  }

  handleChunkClick(event) {
    this.isLocked = !this.isLocked;
  }

  handleChunkDoubleClick(event) {
    this.isLocked = true;
    let chunk = event.target.chunk;
    if (!chunk) return;
    this.dispatchEvent(new CustomEvent(
      'showmaps', {bubbles: true, composed: true, detail: chunk}));
  }

  drawOverview() {
    const height = 50;
    const kFactor = 2;
    let canvas = this.backgroundCanvas;
    canvas.height = height;
    canvas.width = window.innerWidth;
    let ctx = canvas.getContext('2d');
    let chunks = this.timelineEntries.chunkSizes(canvas.width * kFactor);
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
    this.timelineOverview.style.backgroundImage =
        'url(' + imageData + ')';
  }

  redraw(isMapSelected) {
    let canvas = this.timelineCanvas;
    canvas.width = (this.chunks.length + 1) * kChunkWidth;
    canvas.height = kChunkHeight;
    let ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, kChunkHeight);
    if (!isMapSelected) return;
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
    let [x, y] = map.position(this.chunks);
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
    let [x, y] = map.position(this.chunks);
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
    let current = this.map;
    while (current && nofEdges < kMaxOutgoingEdges) {
      nofEdges += current.children.length;
      stack.push(current);
      current = current.parent();
    }
    ctx.save();
    this.drawOutgoingEdges(ctx, this.map, 3);
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
    this.markSelectedMap(ctx, this.map);
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

});
