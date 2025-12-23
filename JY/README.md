# FF-to-FF Timing Graph Extractor for 3D-CTS

Extract sequential (FF-to-FF) timing graph from placed designs for 3D clock tree synthesis research.

## Build

```bash
cd openroad_cts
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## Prerequisites

Load required modules before running (on UCSD servers):
```bash
module load tcl/8.6.6 yaml-cpp/0.8.0 gcc/12.2.0
```

## Usage

### Standalone (No ORFS required)

Extract timing graph from any placed ODB file using environment variables:

```bash
ODB=<design.odb> \
LIB="<lib1.lib> <lib2.lib>" \
SDC=<constraint.sdc> \
OUTPUT=<output.csv> \
    ./build/bin/openroad -no_init JY/extract_ff_graph.tcl
```

**Required environment variables:**
- `ODB`: Placed design database (ODB format)
- `LIB`: Liberty timing libraries (space-separated, quoted)
- `SDC`: Timing constraints file

**Optional environment variables:**
- `SPEF`: Parasitic extraction file (for more accurate timing)
- `OUTPUT`: Output CSV file (default: `ff_timing_graph.csv`)

**Example (ASAP7 GCD):**
```bash
cd openroad_cts

ODB=~/Practice/ORFS/flow_cts/results/asap7/gcd/base/3_place.odb \
LIB="~/Practice/ORFS/flow_cts/platforms/asap7/lib/NLDM/asap7sc7p5t_SEQ_RVT_FF_nldm_220123.lib \
     ~/Practice/ORFS/flow_cts/platforms/asap7/lib/NLDM/asap7sc7p5t_INVBUF_RVT_FF_nldm_220122.lib.gz \
     ~/Practice/ORFS/flow_cts/platforms/asap7/lib/NLDM/asap7sc7p5t_SIMPLE_RVT_FF_nldm_211120.lib.gz" \
SDC=~/Practice/ORFS/flow_cts/designs/asap7/gcd/constraint.sdc \
OUTPUT=JY/ff_graph.csv \
    ./build/bin/openroad -no_init JY/extract_ff_graph.tcl
```

### With ORFS Integration

If using OpenROAD-flow-scripts, the extraction is automatically run:
- After **placement**: `ff_timing_graph_place.csv`
- After **CTS**: `ff_timing_graph_cts.csv`

Files are saved to `results/<platform>/<design>/base/`.

### TCL Command (Interactive)

```tcl
# Load design first, then:
cts::extract_ff_timing_graph "output.csv"
```

## Output Format

CSV with columns:
```
from_ff,to_ff,slack_ns,arrival_ns,required_ns,from_x,from_y,to_x,to_y
```

| Column | Description |
|--------|-------------|
| `from_ff` | Source FF instance name |
| `to_ff` | Destination FF instance name |
| `slack_ns` | Timing slack (negative = violation) |
| `arrival_ns` | Signal arrival time |
| `required_ns` | Required arrival time |
| `from_x`, `from_y` | Source FF location (DBU) |
| `to_x`, `to_y` | Destination FF location (DBU) |

## Implementation

### Modified Files

```
src/cts/src/
├── FFGraphExtractor.h      # Header
├── FFGraphExtractor.cpp    # Implementation
├── TritonCTS.h             # Added extractFFGraph()
├── TritonCTS.cpp           # Added extractFFGraph()
├── TritonCTS.i             # TCL binding
└── CMakeLists.txt          # Build config
```

### APIs Used

**ODB (Database):**
- `dbMaster::isSequential()` - Identify flip-flops
- `dbInst::getLocation()` - Get cell coordinates
- `dbInst::getName()` - Get instance names

**STA (Timing):**
- `vertexWorstSlackPath()` - Get critical path to endpoint
- `PathExpanded` - Trace path back to source
- `vertexRequired()` - Get required arrival time

## Files in JY/

| File | Description |
|------|-------------|
| `extract_ff_graph.tcl` | Standalone extraction script |
| `test_extract.tcl` | Test with ORFS flow_cts results |
| `README.md` | This documentation |

