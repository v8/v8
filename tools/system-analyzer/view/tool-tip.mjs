// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {App} from '../index.mjs'

import {FocusEvent} from './events.mjs';
import {DOM, ExpandableText, V8CustomElement} from './helper.mjs';

DOM.defineCustomElement(
    'view/tool-tip', (templateText) => class Tooltip extends V8CustomElement {
      _targetNode;
      _content;
      _isHidden = true;
      _logEntryClickHandler = this._handleLogEntryClick.bind(this);
      _logEntryRelatedHandler = this._handleLogEntryRelated.bind(this);

      constructor() {
        super(templateText);
        this._intersectionObserver = new IntersectionObserver((entries) => {
          if (entries[0].intersectionRatio <= 0) {
            this.hide();
          } else {
            this.show();
            this.requestUpdate(true);
          }
        });
        document.addEventListener('click', (e) => this.hide());
      }

      _update() {
        if (!this._targetNode || this._isHidden) return;
        const rect = this._targetNode.getBoundingClientRect();
        rect.x += rect.width / 2;
        let atRight = this._useRight(rect.x);
        let atBottom = this._useBottom(rect.y);
        if (atBottom) {
          rect.y += rect.height;
        }
        this._setPosition(rect, atRight, atBottom);
        this.requestUpdate(true);
      }

      set positionOrTargetNode(positionOrTargetNode) {
        if (positionOrTargetNode.nodeType === undefined) {
          this.position = positionOrTargetNode;
        } else {
          this.targetNode = positionOrTargetNode;
        }
      }

      set targetNode(targetNode) {
        this._intersectionObserver.disconnect();
        this._targetNode = targetNode;
        if (targetNode) {
          this._intersectionObserver.observe(targetNode);
          this.requestUpdate(true);
        }
      }

      set position(position) {
        this._targetNode = undefined;
        this._setPosition(
            position, this._useRight(position.x), this._useBottom(position.y));
      }

      _setPosition(viewportPosition, atRight, atBottom) {
        const horizontalMode = atRight ? 'right' : 'left';
        const verticalMode = atBottom ? 'bottom' : 'top';
        this.bodyNode.className = horizontalMode + ' ' + verticalMode;
        const pageX = viewportPosition.x + window.scrollX;
        this.style.left = `${pageX}px`;
        const pageY = viewportPosition.y + window.scrollY;
        this.style.top = `${pageY}px`;
      }

      _useBottom(viewportY) {
        return viewportY <= 400;
      }

      _useRight(viewportX) {
        return viewportX < document.documentElement.clientWidth / 2;
      }

      set content(content) {
        if (!content) return this.hide();
        this.show();
        if (typeof content === 'string') {
          this.contentNode.innerHTML = content;
          this.contentNode.className = 'textContent';
        } else if (content?.nodeType && nodeType?.nodeName) {
          this._setContentNode(content);
        } else {
          this._setContentNode(new TableBuilder(this, content).fragment);
        }
      }

      _setContentNode(content) {
        const newContent = DOM.div();
        newContent.appendChild(content);
        this.contentNode.replaceWith(newContent);
        newContent.id = 'content';
      }

      _handleLogEntryClick(e) {
        this.dispatchEvent(new FocusEvent(e.currentTarget.data));
      }

      _handleLogEntryRelated(e) {
        this.dispatchEvent(new SelectRelatedEvent(e.currentTarget.data));
      }

      hide() {
        this._isHidden = true;
        this.bodyNode.style.display = 'none';
        this.targetNode = undefined;
      }

      show() {
        this.bodyNode.style.display = 'block';
        this._isHidden = false;
      }

      get bodyNode() {
        return this.$('#body');
      }

      get contentNode() {
        return this.$('#content');
      }
    });

class TableBuilder {
  _instance;

  constructor(tooltip, descriptor) {
    this._fragment = new DocumentFragment();
    this._table = DOM.table('properties');
    this._tooltip = tooltip;
    for (let key in descriptor) {
      const value = descriptor[key];
      this._addKeyValue(key, value);
    }
    this._addFooter();
    this._fragment.appendChild(this._table);
  }

  _addKeyValue(key, value) {
    if (key == 'title') return this._addTitle(value);
    if (key == '__this__') {
      this._instance = value;
      return;
    }
    const row = this._table.insertRow();
    row.insertCell().innerText = key;
    const cell = row.insertCell();
    if (value == undefined) return;
    if (App.isClickable(value)) {
      cell.innerText = value.toString();
      cell.className = 'clickable';
      cell.onclick = this._logEntryClickHandler;
      cell.data = value;
    } else {
      new ExpandableText(cell, value.toString());
    }
  }

  _addTitle(value) {
    const title = DOM.element('h3');
    title.innerText = value;
    this._fragment.appendChild(title);
  }

  _addFooter() {
    if (this._instance === undefined) return;
    const td = this._table.createTFoot().insertRow().insertCell();
    let button =
        td.appendChild(DOM.button('Show', this._tooltip._logEntryClickHandler));
    button.data = this._instance;
    button = td.appendChild(
        DOM.button('Show Related', this._tooltip._logEntryRelatedClickHandler));
    button.data = this._instance;
  }

  get fragment() {
    return this._fragment;
  }
}
