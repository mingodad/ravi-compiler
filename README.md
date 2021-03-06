![build](https://github.com/dibyendumajumdar/ravi-compiler/workflows/build/badge.svg)

# ravi-compiler
Experimental compiler for Ravi and Lua 5.3

The goal of this project is to create a standalone parser and compiler for Lua / Ravi 5.3. The output of the compiler will be C code suitable for JITing. The compiler library will consist of distinct modules:

* lexer (Done) - responsible for tokenizing an input buffer
* parser (Mostly Done) - responsible for generating abstract syntax tree (AST).
* typechecker (Mostly Done) - responsible for assigning types to variables when possible.
* linearizer (Work in progress) - responsible for constructing a linear IR representation of the AST.
* optimizer (TODO) - responsible for improving the code
* codegenerator (TODO) - responsible for generate C code

Our goal is to provide a re-usable Lua/Ravi parser/code generator that can be used by all kinds of tools and utilities.

## Documentation

Documentation is coming soon.

For now you can look at following:
* [WIP public api](https://github.com/dibyendumajumdar/ravi-compiler/blob/master/include/ravi_compiler.h)
* [Test inputs and outputs](https://github.com/dibyendumajumdar/ravi-compiler/blob/master/tests/expected/results.expected)

## Why

Lua's inbuilt parser and code generator is a work of art, very compact and low overhead but extremely fast. It uses minimal memory and produces bytecodes as it parses the source code (single pass compiler). This is great for Lua and Ravi given the use cases of these languages, but makes the parser and code generator hard to understand, play with, or reuse in tools such as IDEs. It also makes it harder to perform any advanced type checking or performance optimizations. 

This project will create a new parser and code generator that is not a replacement for the default one in Lua/Ravi but can be used for more specialised code generation, as well as as a means of understanding how the parser and code generator works.

## Technology

This project is written in C for maximum portability like Lua. I considered using Rust or D and may eventually port it to one of these languages but for now it is good old C.

## Building 

You will need CMake 3.12 or greater. The build steps are fairly simple on Linux:

```
mkdir build
cd build
cmake ..
make 
```

## Testing

At the moment we have a simple test driver program named `tparse`. It takes a string input which must be a valid Lua/Ravi chunk of code, and outputs the AST, the result of type checking and also any linear IR output if supported. Example of the output can be found in the `tests/expected` folder.

Suppose `tparse` was built in `build` folder then you can run the tests as follows:

```
cd tests && sh runtests.sh ../build/tparse
```

The test script compares the output to the expected output. Any difference will cause the test script to fail.
