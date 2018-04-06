#!/bin/bash

# Zip it
rm -f antlr4-cpp-runtime-source.zip
zip -r antlr4-cpp-runtime-source.zip "README.md" "cmake" "demo" "runtime" "CMakeLists.txt" "deploy-macos.sh" "deploy-source.sh" "deploy-windows.cmd" "VERSION" \
  -X -x "*.DS_Store*" "antlrcpp.xcodeproj/xcuserdata/*" "*Build*" "*DerivedData*" "*.jar" "demo/generated/*" "*.vscode*" "runtime/build/*"

# Add the license file from the ANTLR root as well.
pushd ../../
zip runtime/cpp/antlr4-cpp-runtime-source.zip LICENSE.txt
popd

# Deploy
#cp antlr4-cpp-runtime-source.zip ~/antlr/sites/website-antlr4/download
