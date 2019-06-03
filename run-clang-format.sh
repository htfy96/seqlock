#!/bin/bash
set -e
find include test -type f -name '*.[ch]pp' | xargs clang-format -i
