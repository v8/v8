// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class State {
  #timeSelection = {start: 0, end: Infinity};
  #map;
  #ic;
  #nofChunks;
  #chunks;
  #icTimeline;
  #mapTimeline;
  get mapTimeline(){
    return this.#mapTimeline;
  }
  set mapTimeline(value){
    this.#mapTimeline = value;
  }
  set icTimeline(value){
    this.#icTimeline = value;
  }
  get icTimeline(){
    return this.#icTimeline;
  }
  set chunks(value){
    //TODO(zcankara) split up between maps and ics, and every timeline track
    this.#chunks = value;
  }
  get chunks(){
    //TODO(zcankara) split up between maps and ics, and every timeline track
    return this.#chunks;
  }
  get nofChunks() {
    return this.#nofChunks;
  }
  set nofChunks(count) {
    this.#nofChunks = count;
  }
  get map() {
    //TODO(zcankara) rename as selectedMapEvents, array of selected events
    return this.#map;
  }
  set map(value) {
    //TODO(zcankara) rename as selectedMapEvents, array of selected events
    if(!value) return;
    this.#map = value;
  }
  get ic() {
    //TODO(zcankara) rename selectedICEvents, array of selected events
    return this.#ic;
  }
  set ic(value) {
    //TODO(zcankara) rename selectedIcEvents, array of selected events
    if(!value) return;
    this.#ic = value;
  }
  get timeSelection() {
    return this.#timeSelection;
  }
  get entries() {
    if (!this.map) return {};
    return {
      map: this.map.id, time: this.map.time
    }
  }
}

export { State };
