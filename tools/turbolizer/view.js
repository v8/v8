// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

class View {
  constructor(id) {
    this.container = document.getElementById(id);
    this.divNode = this.createViewElement();
    this.divElement = d3.select(this.divNode);
  }

  isScrollable() {
    return false;
  }

  show(data, rememberedSelection) {
    this.container.appendChild(this.divElement.node());
    this.initializeContent(data, rememberedSelection);
  }

  hide() {
    this.deleteContent();
    this.container.removeChild(this.divNode);
  }

  detachSelection() {
    return null;
  }
}
