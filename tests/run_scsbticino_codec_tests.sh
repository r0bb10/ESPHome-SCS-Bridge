#!/usr/bin/env sh
set -eu

binary="$(mktemp /tmp/scsbticino-codec-tests.XXXXXX)"
trap 'rm -f "$binary"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/scs_bticino \
  components/scs_bticino/scsbticino_codec.cpp \
  tests/test_scsbticino_codec.cpp \
  -o "$binary"
"$binary"
