set root [file normalize [file join [file dirname [info script]] ..]]
set build /mnt/data/kria_build/kv260_color_overlay_20260917
if {[info exists ::env(COLOR_BUILD_DIR)]} {set build [file normalize $::env(COLOR_BUILD_DIR)]}
open_project [file join $build color_detector.xpr]
set_param general.maxThreads 4
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
