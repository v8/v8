// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

defineCustomElement('timeline-panel', (templateText) =>
 class TimelinePanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.timelineOverviewSelect.addEventListener(
        'mousemove', e => this.handleTimelineIndicatorMove(e));
  }


  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get timelineOverviewSelect() {
    return this.$('#timelineOverview');
  }

  get timelineOverviewIndicatorSelect() {
    return this.$('#timelineOverviewIndicator');
  }

  get timelineCanvasSelect() {
    return this.$('#timelineCanvas');
  }

  get timelineChunksSelect() {
    return this.$('#timelineChunks');
  }

  get timelineSelect() {
    return this.$('#timeline');
  }


  handleTimelineIndicatorMove(event) {
    if (event.buttons == 0) return;
    let timelineTotalWidth = this.timelineCanvasSelect.offsetWidth;
    let factor = this.timelineOverviewSelect.offsetWidth / timelineTotalWidth;
    this.timelineSelect.scrollLeft += event.movementX / factor;
  }

});
