# =============================================================
# Test FF-to-FF Timing Graph Extraction
# Run after placement (before CTS)
# =============================================================

# Configuration - use flow_cts results
set FLOW_DIR "$::env(HOME)/Practice/ORFS/flow_cts"
set PLATFORM "asap7"
set DESIGN "gcd"

set RESULT_DIR "$FLOW_DIR/results/$PLATFORM/$DESIGN/base"
set DESIGN_DIR "$FLOW_DIR/designs/$PLATFORM/$DESIGN"
set PLATFORM_DIR "$FLOW_DIR/platforms/$PLATFORM"

puts "=============================================="
puts "FF-to-FF Timing Graph Extraction"
puts "(Before CTS - using placement result)"
puts "=============================================="
puts "Platform: $PLATFORM"
puts "Design:   $DESIGN"
puts "=============================================="

# Check if placement result exists
if {![file exists "$RESULT_DIR/3_place.odb"]} {
    puts "ERROR: $RESULT_DIR/3_place.odb not found"
    exit 1
}

# Load libraries
puts "\nLoading libraries..."
foreach lib [glob -nocomplain "$PLATFORM_DIR/lib/CCS/*.lib" "$PLATFORM_DIR/lib/CCS/*.lib.gz"] {
    read_liberty $lib
}

# Load design after placement (before CTS)
puts "\nLoading placed design (3_place.odb)..."
read_db $RESULT_DIR/3_place.odb

# Read timing constraints
read_sdc $DESIGN_DIR/constraint.sdc

# Report design info
set regs [all_registers]
puts "Design loaded: [llength $regs] registers"

# Extract FF timing graph
puts "\n=============================================="
puts "Extracting FF-to-FF timing graph..."
puts "(This is BEFORE CTS - no clock tree yet)"
puts "=============================================="
cts::extract_ff_timing_graph "JY/ff_timing_graph_gcd.csv"

puts "\n=============================================="
puts "Done! Output: JY/ff_timing_graph_gcd.csv"
puts "=============================================="

exit
