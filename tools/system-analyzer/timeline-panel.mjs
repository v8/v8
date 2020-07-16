// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {defineCustomElement, V8CustomElement} from './helper.mjs';

defineCustomElement('timeline-panel', (templateText) =>
 class TimelinePanel extends V8CustomElement {
  constructor() {
    super(templateText);
    this.timelineOverview.addEventListener(
        'mousemove', e => this.handleTimelineIndicatorMove(e));
  }

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

  // Timeline related View component methods
  createBackgroundCanvas(){
    this.backgroundCanvas = document.createElement('canvas');
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

});
