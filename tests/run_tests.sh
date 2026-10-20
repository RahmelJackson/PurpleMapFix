#!/usr/bin/env sh
# Builds the MapData tests and runs them against freshly generated fixtures.
# Needs: a C++20 compiler (g++ or clang++), zlib development files, Python 3.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
build="$here/build"
mkdir -p "$build"
python3 "$here/make_fixtures.py" "$build/fixtures"
${CXX:-g++} -std=c++20 -O2 -Wall -Wextra -Werror -I"$here/../src" \
    "$here/test_mapdata.cpp" "$here/../src/MapData.cpp" -lz -o "$build/test_mapdata"
"$build/test_mapdata" "$build/fixtures"
