#!/bin/bash
set -e
find test -type f -name '*.cpp' | xargs -n1 -P4 clang-tidy -header-filter=include/.* $@
