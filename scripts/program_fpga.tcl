# scripts/program_fpga.tcl : Programmation du bitstream sur Digilent Arty A7-35T via Vivado
open_hw_manager
connect_hw_server -allow_non_jtag
open_hw_target

# Sélection du FPGA Artix-7 35T
set hw_device [lindex [get_hw_devices -filter {PART =~ "xc7a35t*"}] 0]
if {[string length $hw_device] == 0} {
    puts "ERREUR : Aucun FPGA Artix-7 35T détecté sur la chaîne JTAG !"
    exit 1
}

current_hw_device $hw_device
refresh_hw_device -update_hw_probes false $hw_device

# Programmation du fichier de bitstream
set bitstream "build/arty/gateware/arty.bit"
puts "Programmation de $bitstream sur $hw_device..."
set_property PROGRAM.FILE $bitstream $hw_device
program_hw_devices $hw_device

close_hw_target
disconnect_hw_server
close_hw_manager
puts "=== Programmation réussie ! ==="
