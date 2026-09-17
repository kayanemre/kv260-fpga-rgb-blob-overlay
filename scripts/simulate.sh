#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
VIVADO_BIN=${VIVADO_BIN:-/mnt/data/Xilinx/Vivado/2022.2/bin}
mkdir -p build/sim
cd build/sim
"$VIVADO_BIN/xvlog" ../../rtl/color_detector_axis.v ../../rtl/color_detector_axis_tb.v
"$VIVADO_BIN/xelab" color_detector_axis_tb -s detector_tb
"$VIVADO_BIN/xsim" detector_tb -runall
grep -q 'PASS: colors' xsim.log
