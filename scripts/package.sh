#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
grep -qx COMPLETE build/fpga.status
pkg=build/package/kv260-blob-overlay
mkdir -p "$pkg"
cat > build/color.bif <<'BIF'
all:
{
    [destination_device = pl] build/color_system.bit
}
BIF
"${VIVADO_BIN:-/mnt/data/Xilinx/Vivado/2022.2/bin}/bootgen" -arch zynqmp -image build/color.bif -w -o "$pkg/kv260-blob-overlay.bit.bin"
dtc -@ -I dts -O dtb -o "$pkg/kv260-blob-overlay.dtbo" vivado/overlay.dts
printf '{"shell_type":"XRT_FLAT"}\n' > "$pkg/shell.json"
cp ps/camera_udp.cpp ps/color_boxes.hpp ps/run_live.sh "$pkg/"
cp build/timing_summary.rpt build/utilization.rpt build/drc.rpt "$pkg/"
(cd "$pkg" && sha256sum kv260-blob-overlay.bit.bin kv260-blob-overlay.dtbo shell.json camera_udp.cpp color_boxes.hpp run_live.sh > SHA256SUMS)
echo "PACKAGE_READY=$pkg (not deployed or board-validated)"
