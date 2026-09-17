# KV260 carrier fan connection, from AMD k26_base_starter_kit default.xdc.
set_property PACKAGE_PIN A12 [get_ports fan_en_b]
set_property IOSTANDARD LVCMOS33 [get_ports fan_en_b]
set_property SLEW SLOW [get_ports fan_en_b]
set_property DRIVE 4 [get_ports fan_en_b]
