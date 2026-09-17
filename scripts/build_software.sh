#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror ps/camera_udp.cpp -ldl -pthread -o build/camera_udp
python3 -m unittest discover -s tests -v
python3 -m py_compile pc/udp_receiver.py
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests/pixel_conversion.cpp -ldl -pthread -o build/pixel_conversion_test
./build/pixel_conversion_test
python3 tests/loopback.py
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests/blob_boxes.cpp -o build/blob_boxes_test
./build/blob_boxes_test
