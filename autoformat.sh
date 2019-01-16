#!/bin/bash

echo "Formatting..."
root=`dirname ${BASH_SOURCE[0]}`
echo $root
time clang-format -style="{BasedOnStyle: llvm, IndentWidth: 4}" -i $root/*.c $root/*.h $root/*.cpp
echo "Done"
