// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class State {
  _timeSelection = { start: 0, end: Infinity };
  _map;
  _ic;
  _selectedMapLogEntries;
  _selectedIcLogEntries;
  _selectedSourcePositions;
  _nofChunks;
  _chunks;
  _icTimeline;
  _mapTimeline;
  _minStartTime = Number.POSITIVE_INFINITY;
  _maxEndTime = Number.NEGATIVE_INFINITY;
  get minStartTime() {
    return this._minStartTime;
  }
  get maxEndTime() {
    return this._maxEndTime;
  }
  _updateTimeRange(timeline) {
    this._minStartTime = Math.min(this._minStartTime, timeline.startTime);
    this._maxEndTime = Math.max(this._maxEndTime, timeline.endTime);
  }
  get mapTimeline() {
    return this._mapTimeline;
  }
  set mapTimeline(timeline) {
    this._updateTimeRange(timeline);
    timeline.startTime = this._minStartTime;
    timeline.endTime = this._maxEndTime;
    this._mapTimeline = timeline;
  }
  set icTimeline(timeline) {
    this._updateTimeRange(timeline);
    timeline.startTime = this._minStartTime;
    timeline.endTime = this._maxEndTime;
    this._icTimeline = timeline;
  }
  get icTimeline() {
    return this._icTimeline;
  }
  set chunks(value) {
    //TODO(zcankara) split up between maps and ics, and every timeline track
    this._chunks = value;
  }
  get chunks() {
    //TODO(zcankara) split up between maps and ics, and every timeline track
    return this._chunks;
  }
  get nofChunks() {
    return this._nofChunks;
  }
  set nofChunks(count) {
    this._nofChunks = count;
  }
  get map() {
    //TODO(zcankara) rename as selectedMapEvents, array of selected events
    return this._map;
  }
  set map(value) {
    //TODO(zcankara) rename as selectedMapEvents, array of selected events
    if (!value) return;
    this._map = value;
  }
  get ic() {
    //TODO(zcankara) rename selectedICEvents, array of selected events
    return this._ic;
  }
  set ic(value) {
    //TODO(zcankara) rename selectedIcEvents, array of selected events
    if (!value) return;
    this._ic = value;
  }
  get selectedMapLogEntries() {
    return this._selectedMapLogEntries;
  }
  set selectedMapLogEntries(value) {
    if (!value) return;
    this._selectedMapLogEntries = value;
  }
  get selectedSourcePositions() {
    return this._selectedSourcePositions;
  }
  set selectedSourcePositions(value) {
    this._selectedSourcePositions = value;
  }
  get selectedIcLogEntries() {
    return this._selectedIcLogEntries;
  }
  set selectedIcLogEntries(value) {
    if (!value) return;
    this._selectedIcLogEntries = value;
  }
  get timeSelection() {
    return this._timeSelection;
  }
  get entries() {
    if (!this.map) return {};
    return {
      map: this.map.id, time: this.map.time
    }
  }
}

export { State };
