#!/bin/bash
set -o errexit

# Created 2016, Mike Lischke (public domain)

# This script is used to generate source files from the test grammars in the same folder. The generated files are placed
# into a subfolder "generated" which the demo project uses to compile a demo binary.

# There are 2 ways of running the ANTLR generator here.

# 1) Running from jar. Use the given jar (or replace it by another one you built or downloaded) for generation.
#LOCATION=antlr4-4.5.4-SNAPSHOT.jar
#java -jar $LOCATION -Dlanguage=Cpp -listener -visitor -o generated/ -package antlrcpptest TLexer.g4 TParser.g4
#java -jar $LOCATION -Dlanguage=Cpp -listener -visitor -o generated/ -package antlrcpptest -XdbgST TLexer.g4 TParser.g4
#java -jar $LOCATION -Dlanguage=Java -listener -visitor -o generated/ -package antlrcpptest TLexer.g4 TParser.g4

# 2) Running from class path. This requires that you have both antlr3 and antlr4 compiled. In this scenario no installation
#    is needed. You just compile the java class files (using "mvn compile" in both the antlr4 and the antlr3 root folders).
#    The script then runs the generation using these class files, by specifying them on the classpath.
#    Also the string template jar is needed. Adjust CLASSPATH if you have stored the jar in a different folder as this script assumes.
#    Furthermore is assumed that the antlr3 folder is located side-by-side with the antlr4 folder. Adjust CLASSPATH if not.
#    This approach is especially useful if you are working on a target stg file, as it doesn't require to regenerate the
#    antlr jar over and over again.
CLASSPATH=../../../tool/resources/:ST-4.0.8.jar:../../../tool/target/classes:../../../runtime/Java/target/classes:../../../../antlr3/runtime/Java/target/classes

java -cp $CLASSPATH org.antlr.v4.Tool -Dlanguage=Cpp -listener -visitor -o generated/ -package antlrcpptest TLexer.g4 TParser.g4
#java -cp $CLASSPATH org.antlr.v4.Tool -Dlanguage=Cpp -listener -visitor -o generated/ -package antlrcpptest -XdbgST TLexer.g4 TParser.g4
#java -cp $CLASSPATH org.antlr.v4.Tool -Dlanguage=Java -listener -visitor -o generated/ TLexer.g4 TParser.g4
