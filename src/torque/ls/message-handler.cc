// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include "src/torque/ls/message-handler.h"

#include "src/torque/ls/globals.h"
#include "src/torque/ls/json-parser.h"
#include "src/torque/ls/message-pipe.h"
#include "src/torque/ls/message.h"
#include "src/torque/server-data.h"
#include "src/torque/source-positions.h"
#include "src/torque/torque-compiler.h"

namespace v8 {
namespace internal {
namespace torque {

DEFINE_CONTEXTUAL_VARIABLE(Logger)
DEFINE_CONTEXTUAL_VARIABLE(TorqueFileList)

namespace ls {

static const char kContentLength[] = "Content-Length: ";
static const size_t kContentLengthSize = sizeof(kContentLength) - 1;

static const char kFileUriPrefix[] = "file://";
static const int kFileUriPrefixLength = sizeof(kFileUriPrefix) - 1;

JsonValue ReadMessage() {
  std::string line;
  std::getline(std::cin, line);

  if (line.rfind(kContentLength) != 0) {
    // Invalid message, we just crash.
    Logger::Log("[fatal] Did not find Content-Length ...\n");
    v8::base::OS::Abort();
  }

  const int content_length = std::atoi(line.substr(kContentLengthSize).c_str());
  std::getline(std::cin, line);
  std::string content(content_length, ' ');
  std::cin.read(&content[0], content_length);

  Logger::Log("[incoming] ", content, "\n\n");

  return ParseJson(content);
}

void WriteMessage(JsonValue& message) {
  std::string content = SerializeToString(message);

  Logger::Log("[outgoing] ", content, "\n\n");

  std::cout << kContentLength << content.size() << "\r\n\r\n";
  std::cout << content << std::flush;
}

namespace {

void RecompileTorque() {
  Logger::Log("[info] Start compilation run ...\n");

  LanguageServerData::Get() = LanguageServerData();
  SourceFileMap::Get() = SourceFileMap();

  TorqueCompilerOptions options;
  options.output_directory = "";
  options.verbose = false;
  options.collect_language_server_data = true;
  options.abort_on_lint_errors = false;
  CompileTorque(TorqueFileList::Get(), options);

  Logger::Log("[info] Finished compilation run ...\n");
}

void HandleInitializeRequest(InitializeRequest request, MessageWriter writer) {
  InitializeResponse response;
  response.set_id(request.id());
  response.result().capabilities().textDocumentSync();
  response.result().capabilities().set_definitionProvider(true);

  // TODO(szuend): Register for document synchronisation here,
  //               so we work with the content that the client
  //               provides, not directly read from files.
  // TODO(szuend): Check that the client actually supports dynamic
  //               "workspace/didChangeWatchedFiles" capability.
  // TODO(szuend): Check if client supports "LocationLink". This will
  //               influence the result of "goto definition".
  writer(response.GetJsonValue());
}

void HandleInitializedNotification(MessageWriter writer) {
  RegistrationRequest request;
  // TODO(szuend): The language server needs a "global" request id counter.
  request.set_id(2000);
  request.set_method("client/registerCapability");

  Registration reg = request.params().add_registrations();
  auto options =
      reg.registerOptions<DidChangeWatchedFilesRegistrationOptions>();
  FileSystemWatcher watcher = options.add_watchers();
  watcher.set_globPattern("**/*.tq");
  watcher.set_kind(FileSystemWatcher::WatchKind::kAll);

  reg.set_id("did-change-id");
  reg.set_method("workspace/didChangeWatchedFiles");

  writer(request.GetJsonValue());
}

void HandleTorqueFileListNotification(TorqueFileListNotification notification) {
  CHECK_EQ(notification.params().object()["files"].tag, JsonValue::ARRAY);

  std::vector<std::string>& files = TorqueFileList::Get();
  Logger::Log("[info] Initial file list:\n");
  for (const auto& fileJson :
       notification.params().object()["files"].ToArray()) {
    CHECK(fileJson.IsString());
    // We only consider file URIs (there shouldn't be anything else).
    if (fileJson.ToString().rfind(kFileUriPrefix) != 0) continue;

    std::string file = fileJson.ToString().substr(kFileUriPrefixLength);
    files.push_back(file);
    Logger::Log("    ", file, "\n");
  }

  // The Torque compiler expects to see some files first,
  // we need to order them in the correct way.
  std::sort(files.begin(), files.end(),
            [](const std::string& a, const std::string& b) {
              if (a.find("base.tq") != std::string::npos) return true;
              if (b.find("base.tq") != std::string::npos) return false;

              if (a.find("array.tq") != std::string::npos) return true;
              if (b.find("array.tq") != std::string::npos) return false;

              return false;
            });

  RecompileTorque();
}

void HandleGotoDefinitionRequest(GotoDefinitionRequest request,
                                 MessageWriter writer) {
  GotoDefinitionResponse response;
  response.set_id(request.id());

  std::string file = request.params().textDocument().uri();
  CHECK_EQ(file.rfind(kFileUriPrefix), 0);
  SourceId id = SourceFileMap::GetSourceId(file.substr(kFileUriPrefixLength));

  // If we do not know about the source file, send back an empty response,
  // i.e. we did not find anything.
  if (!id.IsValid()) {
    response.SetNull("result");
    writer(response.GetJsonValue());
    return;
  }

  LineAndColumn pos{request.params().position().line(),
                    request.params().position().character()};

  if (auto maybe_definition = LanguageServerData::FindDefinition(id, pos)) {
    SourcePosition definition = *maybe_definition;

    std::string definition_file = SourceFileMap::GetSource(definition.source);
    response.result().set_uri(kFileUriPrefix + definition_file);

    Range range = response.result().range();
    range.start().set_line(definition.start.line);
    range.start().set_character(definition.start.column);
    range.end().set_line(definition.end.line);
    range.end().set_character(definition.end.column);
  } else {
    response.SetNull("result");
  }

  writer(response.GetJsonValue());
}

void HandleChangeWatchedFilesNotification(
    DidChangeWatchedFilesNotification notification) {
  // TODO(szuend): Implement updates to the TorqueFile list when create/delete
  //               notifications are received. Currently we simply re-compile.
  RecompileTorque();
}

}  // namespace

void HandleMessage(JsonValue& raw_message, MessageWriter writer) {
  Request<bool> request(raw_message);

  // We ignore responses for now. They are matched to requests
  // by id and don't have a method set.
  // TODO(szuend): Implement proper response handling for requests
  //               that originate from the server.
  if (!request.has_method()) {
    Logger::Log("[info] Unhandled response with id ", request.id(), "\n\n");
    return;
  }

  const std::string method = request.method();
  if (method == "initialize") {
    HandleInitializeRequest(InitializeRequest(request.GetJsonValue()), writer);
  } else if (method == "initialized") {
    HandleInitializedNotification(writer);
  } else if (method == "torque/fileList") {
    HandleTorqueFileListNotification(
        TorqueFileListNotification(request.GetJsonValue()));
  } else if (method == "textDocument/definition") {
    HandleGotoDefinitionRequest(GotoDefinitionRequest(request.GetJsonValue()),
                                writer);
  } else if (method == "workspace/didChangeWatchedFiles") {
    HandleChangeWatchedFilesNotification(
        DidChangeWatchedFilesNotification(request.GetJsonValue()));
  } else {
    Logger::Log("[error] Message of type ", method, " is not handled!\n\n");
  }
}

}  // namespace ls
}  // namespace torque
}  // namespace internal
}  // namespace v8
