// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

class DisassemblyView extends TextView {
  constructor(id, broker, sortedPositionList) {
    super(id, broker, null, false);
    this.pos_start = -1;
    let view = this;
    let ADDRESS_STYLE = {
      css: 'tag',
      location: function(text) {
        ADDRESS_STYLE.last_address = text;
        return undefined;
      }
    };
    let ADDRESS_LINK_STYLE = {
      css: 'tag',
      link: function(text) {
        view.select(function(location) { return location.address == text; }, true, true);
      }
    };
    let UNCLASSIFIED_STYLE = {
      css: 'com'
    };
    let NUMBER_STYLE = {
      css: 'lit'
    };
    let COMMENT_STYLE = {
      css: 'com'
    };
    let POSITION_STYLE = {
      css: 'com',
      location: function(text) {
        view.pos_start = Number(text);
      }
    };
    let OPCODE_STYLE = {
      css: 'kwd',
      location: function(text) {
        return {
          address: ADDRESS_STYLE.last_address
        };
      }
    };
    let patterns = [
      [
        [/^0x[0-9a-f]{8,16}/, ADDRESS_STYLE, 1],
        [/^.*/, UNCLASSIFIED_STYLE, -1]
      ],
      [
        [/^\s+\d+\s+[0-9a-f]+\s+/, NUMBER_STYLE, 2],
        [/^.*/, null, -1]
      ],
      [
        [/^\S+\s+/, OPCODE_STYLE, 3],
        [/^\S+$/, OPCODE_STYLE, -1],
        [/^.*/, null, -1]
      ],
      [
        [/^\s+/, null],
        [/^[^\(;]+$/, null, -1],
        [/^[^\(;]+/, null],
        [/^\(/, null, 4],
        [/^;/, COMMENT_STYLE, 5]
      ],
      [
        [/^0x[0-9a-f]{8,16}/, ADDRESS_LINK_STYLE],
        [/^[^\)]/, null],
        [/^\)$/, null, -1],
        [/^\)/, null, 3]
      ],
      [
        [/^; debug\: position /, COMMENT_STYLE, 6],
        [/^.+$/, COMMENT_STYLE, -1]
      ],
      [
        [/^\d+$/, POSITION_STYLE, -1],
      ]
    ];
    view.setPatterns(patterns);
  }

  lineLocation(li) {
    let view = this;
    let result = undefined;
    for (let i = 0; i < li.children.length; ++i) {
      let fragment = li.children[i];
      let location = fragment.location;
      if (location != null) {
        if (location.address != undefined) {
          if (result === undefined) result = {};
          result.address = location.address;
        }
        if (view.pos_start != -1) {
          if (result === undefined) result = {};
          result.pos_start = view.pos_start;
          result.pos_end = result.pos_start + 1;
        }
      }
    }
    return result;
  }
}
