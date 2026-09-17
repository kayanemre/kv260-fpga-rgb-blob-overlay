# Run from any directory: vivado -mode batch -source vivado/create_bd.tcl
set root [file normalize [file join [file dirname [info script]] ..]]
set build [file join $root build vivado]
if {[info exists ::env(COLOR_BUILD_DIR)]} {set build [file normalize $::env(COLOR_BUILD_DIR)]}
if {[file exists [file join $build color_detector.xpr]]} {
    error "Build already exists: $build. Use its project or choose a fresh build directory."
}
create_project color_detector $build -part xck26-sfvc784-2LV-c
set_param general.maxThreads 4
set cache [file join $root build ip_cache]
file mkdir $cache
config_ip_cache -use_cache_location $cache
add_files [list [file join $root rtl color_detector_axis.v] [file join $root rtl color_detector_dma_wrapper.v]]
create_bd_design color_system
set PS_0 [create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.4 PS_0]
source [file join $root vivado ps_preset.tcl]
# Keep the board's DDR/MIO peripheral setup, reduce PL to one 100 MHz domain.
set_property -dict [list \
 CONFIG.PSU__USE__M_AXI_GP0 {1} CONFIG.PSU__MAXIGP0__DATA_WIDTH {32} \
 CONFIG.PSU__USE__M_AXI_GP1 {0} CONFIG.PSU__USE__M_AXI_GP2 {0} \
 CONFIG.PSU__USE__S_AXI_GP0 {0} CONFIG.PSU__USE__S_AXI_GP1 {0} \
 CONFIG.PSU__USE__S_AXI_GP2 {1} CONFIG.PSU__SAXIGP2__DATA_WIDTH {64} \
 CONFIG.PSU__USE__S_AXI_GP3 {0} CONFIG.PSU__USE__S_AXI_GP4 {0} \
 CONFIG.PSU__USE__S_AXI_GP5 {0} CONFIG.PSU__USE__S_AXI_GP6 {0} \
 CONFIG.PSU__FPGA_PL0_ENABLE {1} CONFIG.PSU__FPGA_PL1_ENABLE {0} \
 CONFIG.PSU__FPGA_PL2_ENABLE {0} CONFIG.PSU__FPGA_PL3_ENABLE {0} \
 CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100} \
 CONFIG.PSU__GPIO_EMIO__PERIPHERAL__ENABLE {0} \
 CONFIG.PSU__NUM_FABRIC_RESETS {1} CONFIG.PSU__USE__IRQ0 {1} CONFIG.PSU__USE__IRQ1 {0} \
] $PS_0
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 dma
set_property -dict [list CONFIG.c_include_sg {0} CONFIG.c_sg_length_width {23} \
 CONFIG.c_include_mm2s {1} CONFIG.c_include_s2mm {1} CONFIG.c_addr_width {32} \
 CONFIG.c_m_axis_mm2s_tdata_width {32} CONFIG.c_s_axis_s2mm_tdata_width {32} \
 CONFIG.c_m_axi_mm2s_data_width {64} CONFIG.c_m_axi_s2mm_data_width {64} \
 CONFIG.c_mm2s_burst_size {16} CONFIG.c_s2mm_burst_size {16}] [get_bd_cells dma]
create_bd_cell -type module -reference color_detector_dma_wrapper detector
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 control
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] [get_bd_cells control]
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 memory
set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1}] [get_bd_cells memory]
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset
# External reset polarity is inferred from PS pl_resetn0 (read-only in BD).
set_property CONFIG.C_AUX_RESET_HIGH {0} [get_bd_cells reset]
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 one
set_property CONFIG.CONST_VAL {1} [get_bd_cells one]
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 zero
set_property CONFIG.CONST_VAL {0} [get_bd_cells zero]
# Future driver interrupt support; the first application polls with IRQ enables off.
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 interrupts
set_property CONFIG.NUM_PORTS {2} [get_bd_cells interrupts]
connect_bd_net [get_bd_pins dma/mm2s_introut] [get_bd_pins interrupts/In0]
connect_bd_net [get_bd_pins dma/s2mm_introut] [get_bd_pins interrupts/In1]
connect_bd_net [get_bd_pins interrupts/dout] [get_bd_pins PS_0/pl_ps_irq0]
foreach {a b} {
 PS_0/M_AXI_HPM0_FPD control/S00_AXI
 control/M00_AXI dma/S_AXI_LITE
 dma/M_AXI_MM2S memory/S00_AXI
 dma/M_AXI_S2MM memory/S01_AXI
 memory/M00_AXI PS_0/S_AXI_HP0_FPD
 dma/M_AXIS_MM2S detector/S_AXIS
 detector/M_AXIS dma/S_AXIS_S2MM
} { connect_bd_intf_net [get_bd_intf_pins $a] [get_bd_intf_pins $b] }
foreach pin {PS_0/maxihpm0_fpd_aclk PS_0/saxihp0_fpd_aclk control/aclk memory/aclk
 dma/s_axi_lite_aclk dma/m_axi_mm2s_aclk dma/m_axi_s2mm_aclk detector/aclk reset/slowest_sync_clk} {
 connect_bd_net [get_bd_pins PS_0/pl_clk0] [get_bd_pins $pin]
}
connect_bd_net [get_bd_pins PS_0/pl_resetn0] [get_bd_pins reset/ext_reset_in]
connect_bd_net [get_bd_pins one/dout] [get_bd_pins reset/dcm_locked] [get_bd_pins reset/aux_reset_in]
connect_bd_net [get_bd_pins zero/dout] [get_bd_pins reset/mb_debug_sys_rst]
foreach pin {control/aresetn memory/aresetn dma/axi_resetn} {
 connect_bd_net [get_bd_pins reset/peripheral_aresetn] [get_bd_pins $pin]
}
connect_bd_net [get_bd_pins dma/mm2s_prmry_reset_out_n] [get_bd_pins detector/aresetn]
# Retain the carrier fan's existing TTC waveform connection.
create_bd_cell -type ip -vlnv xilinx.com:ip:xlslice:1.0 fan_slice
set_property -dict [list CONFIG.DIN_WIDTH {3} CONFIG.DIN_FROM {2} CONFIG.DIN_TO {2}] [get_bd_cells fan_slice]
create_bd_port -dir O fan_en_b
connect_bd_net [get_bd_pins PS_0/emio_ttc0_wave_o] [get_bd_pins fan_slice/Din]
connect_bd_net [get_bd_pins fan_slice/Dout] [get_bd_ports fan_en_b]
assign_bd_address -offset 0xA0000000 -range 0x10000 -target_address_space [get_bd_addr_spaces PS_0/Data] [get_bd_addr_segs dma/S_AXI_LITE/Reg]
foreach channel {Data_MM2S Data_S2MM} {
 assign_bd_address -offset 0 -range 0x80000000 -target_address_space [get_bd_addr_spaces dma/$channel] [get_bd_addr_segs PS_0/SAXIGP2/HP0_DDR_LOW]
}
validate_bd_design
if {[get_property CONFIG.C_EXT_RESET_HIGH [get_bd_cells reset]] != 0 || [get_property CONFIG.C_AUX_RESET_HIGH [get_bd_cells reset]] != 0} {error "Unexpected reset polarity"}
save_bd_design
write_bd_tcl -force [file join $root build color_system_bd.tcl]
generate_target all [get_files color_system.bd]
make_wrapper -files [get_files color_system.bd] -top
add_files [file join $build color_detector.gen sources_1 bd color_system hdl color_system_wrapper.v]
set_property top color_system_wrapper [current_fileset]
add_files -fileset constrs_1 [file join $root vivado board.xdc]
update_compile_order -fileset sources_1
puts "COLOR_BD_VALIDATED"
# Build in this isolated directory only. No programming/deployment commands.
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Synthesis failed"}
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation failed"}
open_run impl_1
report_timing_summary -file [file join $root build timing_summary.rpt]
report_utilization -file [file join $root build utilization.rpt]
report_drc -file [file join $root build drc.rpt]
set setup [get_timing_paths -delay_type max -max_paths 1]
set hold [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup]==0 || [llength $hold]==0} {error "Missing timing paths"}
if {[get_property SLACK $setup]<0 || [get_property SLACK $hold]<0} {error "Timing failed"}
set violations [get_drc_violations -quiet -filter {SEVERITY == Error || SEVERITY == {Critical Warning}}]
if {[llength $violations]} {error "DRC gate failed: $violations"}
# Generate via the managed run so XSA export can locate the bitstream.
close_design
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Bitstream generation failed"}
file copy -force [file join $build color_detector.runs impl_1 color_system_wrapper.bit] [file join $root build color_system.bit]
write_hw_platform -fixed -include_bit -force -file [file join $root build color_system.xsa]
puts "COLOR_BITSTREAM_COMPLETE"
