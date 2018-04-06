#!/bin/bash

# Clean left overs from previous builds if there are any
rm -f -R antlr4-runtime build lib 2> /dev/null
rm antlr4-cpp-runtime-macos.zip 2> /dev/null

# Binaries
xcodebuild -project runtime/antlrcpp.xcodeproj -target antlr4 -configuration Release
xcodebuild -project runtime/antlrcpp.xcodeproj -target antlr4_static -configuration Release
rm -f -R lib
mkdir lib
mv runtime/build/Release/libantlr4-runtime.a lib/
mv runtime/build/Release/libantlr4-runtime.dylib lib/

# Headers
rm -f -R antlr4-runtime
pushd runtime/src
find . -name '*.h' | cpio -pdm ../../antlr4-runtime
popd

# Zip up and clean up
zip -r antlr4-cpp-runtime-macos.zip antlr4-runtime lib

rm -f -R antlr4-runtime build lib

# Deploy
#cp antlr4-cpp-runtime-macos.zip ~/antlr/sites/website-antlr4/download
