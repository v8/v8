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

  handleTimelineIndicatorMove(event) {
    if (event.buttons == 0) return;
    let timelineTotalWidth = this.timelineCanvas.offsetWidth;
    let factor = this.timelineOverview.offsetWidth / timelineTotalWidth;
    this.timeline.scrollLeft += event.movementX / factor;
  }

});
