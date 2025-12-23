# =============================================================
# FF-to-FF Timing Graph Extraction (Standalone)
# No ORFS required - just needs ODB, liberty, and SDC files
# =============================================================
#
# Usage (with environment variables):
#   ODB=<design.odb> LIB="<lib1> <lib2>" SDC=<constraint.sdc> OUTPUT=<output.csv> \
#       openroad -no_init extract_ff_graph.tcl
#
# Example:
#   ODB=results/3_place.odb \
#   LIB="lib/seq.lib lib/inv.lib" \
#   SDC=constraint.sdc \
#   OUTPUT=ff_graph.csv \
#       ./build/bin/openroad -no_init JY/extract_ff_graph.tcl
# =============================================================

puts "=============================================="
puts "FF-to-FF Timing Graph Extraction"
puts "=============================================="

# Get parameters from environment variables
if {![info exists ::env(ODB)]} {
    puts "ERROR: ODB environment variable not set"
    puts "Usage: ODB=<design.odb> LIB=\"<lib1> <lib2>\" SDC=<sdc> OUTPUT=<csv> openroad -no_init extract_ff_graph.tcl"
    exit 1
}
if {![info exists ::env(LIB)]} {
    puts "ERROR: LIB environment variable not set"
    exit 1
}
if {![info exists ::env(SDC)]} {
    puts "ERROR: SDC environment variable not set"
    exit 1
}

set odb_file $::env(ODB)
set lib_files $::env(LIB)
set sdc_file $::env(SDC)

if {[info exists ::env(OUTPUT)]} {
    set output_file $::env(OUTPUT)
} else {
    set output_file "ff_timing_graph.csv"
}

if {[info exists ::env(SPEF)]} {
    set spef_file $::env(SPEF)
} else {
    set spef_file ""
}

puts "ODB:    $odb_file"
puts "LIB:    $lib_files"
puts "SDC:    $sdc_file"
puts "Output: $output_file"
puts "=============================================="

# Check files exist
if {![file exists $odb_file]} {
    puts "ERROR: ODB file not found: $odb_file"
    exit 1
}
if {![file exists $sdc_file]} {
    puts "ERROR: SDC file not found: $sdc_file"
    exit 1
}

# Load liberty files
puts "\nLoading liberty files..."
foreach lib $lib_files {
    if {[file exists $lib]} {
        puts "  $lib"
        read_liberty $lib
    } else {
        puts "  WARNING: $lib not found, skipping"
    }
}

# Load design
puts "\nLoading design..."
read_db $odb_file

# Read timing constraints
puts "Loading SDC..."
read_sdc $sdc_file

# Read SPEF if provided
if {$spef_file ne "" && [file exists $spef_file]} {
    puts "Loading SPEF..."
    read_spef $spef_file
}

# Report design info
set regs [all_registers]
puts "\nDesign: [llength $regs] registers"

# Extract FF timing graph
puts "\n=============================================="
puts "Extracting FF-to-FF timing graph..."
puts "=============================================="

cts::extract_ff_timing_graph $output_file

puts "\n=============================================="
puts "Done! Output: $output_file"
puts "=============================================="

exit
