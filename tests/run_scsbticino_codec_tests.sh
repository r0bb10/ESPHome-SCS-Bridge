#!/usr/bin/env sh
set -eu

codec_binary="$(mktemp /tmp/scsbticino-codec-tests.XXXXXX)"
tx_binary="$(mktemp /tmp/scsbticino-tx-tests.XXXXXX)"
controller_binary="$(mktemp /tmp/scsbticino-controller-tests.XXXXXX)"
trap 'rm -f "$codec_binary" "$tx_binary" "$controller_binary"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/scs_bticino \
  components/scs_bticino/scsbticino_codec.cpp \
  components/scs_bticino/scsbticino_tx.cpp \
  tests/test_scsbticino_codec.cpp \
  -o "$codec_binary"
"$codec_binary"

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/scs_bticino \
  components/scs_bticino/scsbticino_codec.cpp \
  components/scs_bticino/scsbticino_tx.cpp \
  tests/test_scsbticino_tx.cpp \
  -o "$tx_binary"
"$tx_binary"

g++ -std=c++17 -Wall -Wextra -Werror -Icomponents/scs_bticino \
  components/scs_bticino/scsbticino_codec.cpp \
  components/scs_bticino/scsbticino_tx.cpp \
  tests/test_scsbticino_controller.cpp \
  -o "$controller_binary"
"$controller_binary"
