// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// -------------------------------------------------------------------

(function(global, utils) {

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var Script = utils.ImportNow("Script");

// -------------------------------------------------------------------
// Script

/**
 * Set up the Script function and constructor.
 */
%FunctionSetInstanceClassName(Script, 'Script');
%AddNamedProperty(Script.prototype, 'constructor', Script,
                  DONT_ENUM | DONT_DELETE | READ_ONLY);


/**
 * Get information on a specific source position.
 * Returns an object with the following following properties:
 *   script     : script object for the source
 *   line       : source line number
 *   column     : source column within the line
 *   position   : position within the source
 *   sourceText : a string containing the current line
 * @param {number} position The source position
 * @param {boolean} include_resource_offset Set to true to have the resource
 *     offset added to the location
 * @return If line is negative or not in the source null is returned.
 */
function ScriptLocationFromPosition(position,
                                    include_resource_offset) {
  return %ScriptPositionInfo(this, position, !!include_resource_offset);
}


/**
 * If sourceURL comment is available returns sourceURL comment contents.
 * Otherwise, script name is returned. See
 * http://fbug.googlecode.com/svn/branches/firebug1.1/docs/ReleaseNotes_1.1.txt
 * and Source Map Revision 3 proposal for details on using //# sourceURL and
 * deprecated //@ sourceURL comment to identify scripts that don't have name.
 *
 * @return {?string} script name if present, value for //# sourceURL comment or
 * deprecated //@ sourceURL comment otherwise.
 */
function ScriptNameOrSourceURL() {
  // Keep in sync with Script::GetNameOrSourceURL.
  if (this.source_url) return this.source_url;
  return this.name;
}


utils.SetUpLockedPrototype(Script, [
    "source",
    "name",
    "source_url",
    "source_mapping_url",
    "line_offset",
    "column_offset"
  ], [
    "locationFromPosition", ScriptLocationFromPosition,
    "nameOrSourceURL", ScriptNameOrSourceURL,
  ]
);

// -------------------------------------------------------------------
// Message

function GetLineNumber(message) {
  var start_position = %MessageGetStartPosition(message);
  if (start_position == -1) return kNoLineNumberInfo;
  var script = %MessageGetScript(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return kNoLineNumberInfo;
  return location.line + 1;
}


//Returns the offset of the given position within the containing line.
function GetColumnNumber(message) {
  var script = %MessageGetScript(message);
  var start_position = %MessageGetStartPosition(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return -1;
  return location.column;
}


// Returns the source code line containing the given source
// position, or the empty string if the position is invalid.
function GetSourceLine(message) {
  var script = %MessageGetScript(message);
  var start_position = %MessageGetStartPosition(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return "";
  return location.sourceText;
}

%InstallToContext([
  "message_get_column_number", GetColumnNumber,
  "message_get_line_number", GetLineNumber,
  "message_get_source_line", GetSourceLine,
]);

});
