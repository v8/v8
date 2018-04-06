## Demo application for the ANTLR 4 C++ target

This demo app shows how to build the ANTLR runtime both as dynamic and static library and how to use a parser generated from a simple demo grammar.

A few steps are necessary to get this to work:

- Download the current ANTLR jar and place it in this folder.
- Open the generation script for your platform (generate.cmd for Windows, generate.sh for *nix/OSX) and update the LOCATION var to the actual name of the jar you downloaded.
- Run the generation script. This will generate a test parser + lexer, along with listener + visitor classes in a subfolder named "generated". This is where the demo application looks for these files.
- Open the project in the folder that matches your system.
- Compile and run.

Compilation is done as described in the [runtime/cpp/readme.md](../README.md) file.
