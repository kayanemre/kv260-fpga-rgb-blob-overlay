#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
trap 'echo FAILED > build/fpga.status' ERR
echo RUNNING > build/fpga.status
export COLOR_BUILD_DIR=${COLOR_BUILD_DIR:-/mnt/data/kria_build/kv260_color_overlay_20260917}
"${VIVADO_BIN:-/mnt/data/Xilinx/Vivado/2022.2/bin}/vivado" -mode batch -source vivado/create_bd.tcl -log build/vivado.log -journal build/vivado.jou
grep -q COLOR_BITSTREAM_COMPLETE build/vivado.log
echo COMPLETE > build/fpga.status
