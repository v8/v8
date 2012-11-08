// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"use strict";

var InternalObjectIsFrozen = $Object.isFrozen;
var InternalObjectFreeze = $Object.freeze;

var observationState = %GetObservationState();
if (IS_UNDEFINED(observationState.observerInfoMap)) {
  observationState.observerInfoMap = %CreateObjectHashTable();
  observationState.objectInfoMap = %CreateObjectHashTable();
}

function InternalObjectHashTable(table) {
  this.table = table;
}

InternalObjectHashTable.prototype = {
  get: function(key) {
    return %ObjectHashTableGet(this.table, key);
  },
  set: function(key, value) {
    this.table = %ObjectHashTableSet(this.table, key, value);
  },
  has: function(key) {
    return %ObjectHashTableHas(this.table, key);
  }
};

var observerInfoMap = new InternalObjectHashTable(
    observationState.observerInfoMap);
var objectInfoMap = new InternalObjectHashTable(observationState.objectInfoMap);

function ObjectObserve(object, callback) {
  if (!IS_SPEC_OBJECT(object))
    throw MakeTypeError("observe_non_object", ["observe"]);
  if (!IS_SPEC_FUNCTION(callback))
    throw MakeTypeError("observe_non_function", ["observe"]);
  if (InternalObjectIsFrozen(callback))
    throw MakeTypeError("observe_callback_frozen");

  if (!observerInfoMap.has(callback)) {
    // TODO: setup observerInfo.priority.
    observerInfoMap.set(callback, {
      pendingChangeRecords: null
    });
  }

  var objectInfo = objectInfoMap.get(object);
  if (IS_UNDEFINED(objectInfo)) {
    // TODO: setup objectInfo.notifier
    objectInfo = {
      changeObservers: new InternalArray(callback)
    };
    objectInfoMap.set(object, objectInfo);
    %SetIsObserved(object, true);
    return;
  }

  var changeObservers = objectInfo.changeObservers;
  if (changeObservers.indexOf(callback) >= 0)
    return;

  changeObservers.push(callback);
}

function ObjectUnobserve(object, callback) {
  if (!IS_SPEC_OBJECT(object))
    throw MakeTypeError("observe_non_object", ["unobserve"]);

  var objectInfo = objectInfoMap.get(object);
  if (IS_UNDEFINED(objectInfo))
    return;

  var changeObservers = objectInfo.changeObservers;
  var index = changeObservers.indexOf(callback);
  if (index < 0)
    return;

  changeObservers.splice(index, 1);
}

function EnqueueChangeRecord(changeRecord, observers) {
  for (var i = 0; i < observers.length; i++) {
    var observer = observers[i];
    var observerInfo = observerInfoMap.get(observer);

    // TODO: "activate" the observer

    if (IS_NULL(observerInfo.pendingChangeRecords)) {
      observerInfo.pendingChangeRecords = new InternalArray(changeRecord);
    } else {
      observerInfo.pendingChangeRecords.push(changeRecord);
    }
  }
}

function NotifyChange(type, object, name, oldValue) {
  var objectInfo = objectInfoMap.get(object);
  var changeRecord = (arguments.length < 4) ?
      { type: type, object: object, name: name } :
      { type: type, object: object, name: name, oldValue: oldValue };
  InternalObjectFreeze(changeRecord);
  EnqueueChangeRecord(changeRecord, objectInfo.changeObservers);
}

function ObjectNotify(object, changeRecord) {
  // TODO: notifier needs to be [[THIS]]
  if (!IS_STRING(changeRecord.type))
    throw MakeTypeError("observe_type_non_string");

  var objectInfo = objectInfoMap.get(object);
  if (IS_UNDEFINED(objectInfo))
    return;

  var newRecord = {
    object: object  // TODO: Needs to be 'object' retrieved from notifier
  };
  for (var prop in changeRecord) {
    if (prop === 'object')
      continue;
    newRecord[prop] = changeRecord[prop];
  }
  InternalObjectFreeze(newRecord);

  EnqueueChangeRecord(newRecord, objectInfo.changeObservers);
}

function ObjectDeliverChangeRecords(callback) {
  if (!IS_SPEC_FUNCTION(callback))
    throw MakeTypeError("observe_non_function", ["deliverChangeRecords"]);

  var observerInfo = observerInfoMap.get(callback);
  if (IS_UNDEFINED(observerInfo))
    return;

  var pendingChangeRecords = observerInfo.pendingChangeRecords;
  if (IS_NULL(pendingChangeRecords))
    return;

  observerInfo.pendingChangeRecords = null;
  var delivered = [];
  %MoveArrayContents(pendingChangeRecords, delivered);
  try {
    %Call(void 0, delivered, callback);
  } catch (ex) {}
}

function SetupObjectObserve() {
  %CheckIsBootstrapping();
  InstallFunctions($Object, DONT_ENUM, $Array(
    "deliverChangeRecords", ObjectDeliverChangeRecords,
    "notify", ObjectNotify,  // TODO: Remove when getNotifier is implemented.
    "observe", ObjectObserve,
    "unobserve", ObjectUnobserve
  ));
}

SetupObjectObserve();
