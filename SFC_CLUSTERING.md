# Enhanced Space-Filling-Curve Sink Clustering for TritonCTS

This branch (`sfc-clustering`) adds three improvements to the leaf-level sink
clustering in OpenROAD's clock tree synthesizer, TritonCTS. All of them are
off by default; existing flows are unchanged unless you pass the new options.

| Option | What it does |
|---|---|
| `-sfc_optimizer dp_rp` | Replaces the greedy sweep along the Hilbert curve with the optimal segmentation of the same order (restricted-partitioning DP, Kahng & Robins). The cluster count is pinned to the greedy result so the two are compared at equal leaf-buffer cost. |
| `-sfc_3opt` | Improves the curve order before segmentation with neighbour-limited 2-Opt and Or-Opt moves. Requires `-sfc_optimizer dp_rp`. |
| `-sfc_refine` | After segmentation, moves and swaps boundary sinks between neighbouring clusters when that lowers the summed cluster cost. |
| `-sfc_cost diameter\|radius` | Per-cluster cost: maximum pairwise Manhattan distance (default) or Manhattan radius about the cluster centre. |
| `-sfc_balance <w>` | Fanout-balance penalty on the cluster cost, in units of the maximum cluster diameter. `0` (default) disables it. |

Recommended combination:

```tcl
clock_tree_synthesis -sink_clustering_enable \
    -sfc_optimizer dp_rp -sfc_3opt -sfc_refine -sfc_balance 0.25
```

## Where the code is

Everything lives under `src/cts`:

| File | Role |
|---|---|
| `src/ClusterRefine.h`, `src/ClusterRefine.cpp` | New. The 3-Opt tour improvement, the DP-RP segmentation, and geometric helpers (pairwise diameter, L1 centre). Depends only on `Util.h`. |
| `src/SinkClustering.cpp`, `src/SinkClustering.h` | The DP entry point (`findBestMatchingDpRp`), a shared `clusterCost` / `clusterFeasible` pair used by the DP, the refinement pass and reporting, and the refinement itself (`refineClusters`). |
| `src/CtsOptions.h` | The five `-sfc_*` options and their accessors. |
| `src/TritonCTS.tcl`, `src/TritonCTS.i` | Tcl argument parsing and SWIG bindings for `clock_tree_synthesis`, `set_cts_config`, `reset_cts_config`. |
| `test/sfc_dp_rp.tcl`, `test/sfc_dp_rp.ok` | Regression test: 300 sinks on a grid with all three techniques enabled. |
| `README.md` | Option reference (the standard TritonCTS documentation, extended). |

The whole change is one commit on top of upstream OpenROAD `b076432014`:

```bash
git log --oneline b076432014..sfc-clustering
```

## Build

```bash
git clone --recursive -b sfc-clustering https://github.com/ytliu8464/OpenROAD.git
cd OpenROAD
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build -j$(nproc)
```

`--recursive` is required: the build needs the OpenSTA, abc and slang submodules.
See `etc/DependencyInstaller.sh` for system prerequisites (same as upstream OpenROAD).

## Test

```bash
cd build
ctest -R sfc_dp_rp          # the new test
ctest -R '^cts'             # the full CTS suite (55 tests)
```

## Use

Interactively, or in any OpenROAD Tcl script:

```tcl
read_liberty ...; read_lef ...; read_def ...; read_sdc ...
set_wire_rc -clock -layer metal5
clock_tree_synthesis -sink_clustering_enable -buf_list BUF_X4 \
    -sfc_optimizer dp_rp -sfc_3opt -sfc_refine -sfc_balance 0.25
```

With OpenROAD-flow-scripts, set `CTS_ARGS` (it replaces the whole default
argument list, so keep the defaults you need) and point the flow at this build:

```bash
export OPENROAD_EXE=/path/to/OpenROAD/build/bin/openroad
export CTS_ARGS="-sink_clustering_enable -repair_clock_nets \
    -sfc_optimizer dp_rp -sfc_3opt -sfc_refine -sfc_balance 0.25"
make DESIGN_CONFIG=./designs/nangate45/jpeg/config.mk
```

`set_debug_level CTS clustering 1` prints the accepted 3-Opt moves, the
DP-versus-greedy cost, and the refinement moves.

## Notes

- Options default off, so the branch is drop-in safe for existing flows.
- `-sfc_3opt` without `-sfc_optimizer dp_rp` warns and does nothing (the greedy
  sweep consumes the curve order directly).
- The DP falls back to the greedy result if no feasible partition exists
  (warning `CTS-0251`).
- The base commit is upstream `b076432014` (August 2026). Rebasing onto a
  newer master should be straightforward for `ClusterRefine.*` and
  `SinkClustering.*`; the option-plumbing files are where conflicts would
  appear.
