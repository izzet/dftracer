<p align="center">
  <img src="docs/images/dftracer-logo-light.png" alt="DFTracer logo" width="320">
</p>

# DFTracer

[![Build and Test](https://github.com/llnl-asr/dftracer/actions/workflows/ci.yml/badge.svg)](https://github.com/llnl-asr/dftracer/actions/workflows/ci.yml)
[![Coverage Status](https://coveralls.io/repos/github/llnl-asr/dftracer/badge.svg?branch=develop)](https://coveralls.io/github/llnl-asr/dftracer?branch=develop)
[![Documentation Status](https://readthedocs.org/projects/dftracer/badge/?version=latest)](https://dftracer.readthedocs.io/en/latest/?badge=latest)
![PyPI - Version](https://img.shields.io/pypi/v/dftracer?label=PyPI)
![PyPI - Wheel](https://img.shields.io/pypi/wheel/dftracer?label=Wheel)
![PyPI - Python Version](https://img.shields.io/pypi/pyversions/dftracer?label=Python)
![PyPI - License](https://img.shields.io/pypi/l/dftracer?label=License)

## Overview

DFTracer is a tracing tool designed to capture both application-code and I/O-call level events from workflows. It provides a unified tracing interface, optimized trace format, and compression mechanism to enable efficient distributed analysis for large-scale AI-driven workloads.

## Installation

DFTracer needs Python 3.7 or newer. `pip` works in native Python and Conda
environments.

### From PyPI

```bash
pip install dftracer
```

This installs a prebuilt wheel that traces POSIX and STDIO I/O and needs nothing
from the host. To trace MPI, MPI-IO, HDF5, PAPI counters, HIP or CUDA as well,
add the prebuilt modules and compose them for your machine:

```bash
pip install --pre "dftracer[modules]"   # the core plus prebuilt module archives
dftracer-compose                        # link what this machine needs
dftracer_install_info --composition     # show what got linked
```

Run `dftracer-compose` in the environment you trace in (the same `module load`
or `spack load`) and again whenever that environment changes. See
[Composable install](https://dftracer.readthedocs.io/en/latest/composable_install.html)
for details.

Development builds are published from every merge into `develop`, versioned
`<last release>.postN.dev0`, as both wheels and an sdist. They are PEP 440
pre-releases, so `pip` only selects them when asked:

```bash
pip install --pre dftracer                # newest prerelease
pip install dftracer==2.1.0.post5.dev0    # a specific one
```

### From source

A source build is needed to change build options, or to build against an MPI or
HDF5 that the prebuilt modules do not cover.

```bash
git clone https://github.com/llnl-asr/dftracer.git
cd dftracer                       # the default branch is develop
# git checkout tags/<Release> -b <Release>   # or build a specific release
pip install .
```

To enable MPI, HDF5 or HIP in a source build, set the options below in the
environment. MPI and HDF5 must be discoverable by CMake (`module load`,
`spack load`, ...):

```bash
DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON pip install .
# or build the published sdist instead of the wheel:
DFTRACER_ENABLE_MPI=ON DFTRACER_ENABLE_HDF5=ON \
  pip install --no-binary dftracer dftracer
```

`setup.py` reads these variables and passes them to CMake. All default to `OFF`
unless stated:

| Variable | Effect |
| --- | --- |
| `DFTRACER_ENABLE_MPI` | MPI rank in traces and MPI/MPI-IO interception |
| `DFTRACER_ENABLE_HDF5` | HDF5 interception |
| `DFTRACER_ENABLE_HIP_TRACING` | AMD GPU tracing, needs ROCm/rocprofiler-sdk |
| `DFTRACER_ENABLE_FTRACING` | function tracing via `-finstrument-functions` |
| `DFTRACER_ENABLE_DYNAMIC_DETECTION` | detect HWLOC, MPI and HIP at run time instead of link time |
| `DFTRACER_DISABLE_HWLOC` | HWLOC support, `ON` (disabled) by default |
| `DFTRACER_MPI_IMPL` | override MPI implementation detection |
| `DFTRACER_BUILD_TYPE` | `Release` (default) or `Debug` |

Requirements for a source build: a C++17 compiler whose standard library
provides `std::filesystem` (GCC 9 or newer; a system `libstdc++` older than the
compiler on the `PATH` can shadow it and fail the link), CMake 3.24 or newer,
and the development packages of whatever you enable. The C/C++ dependencies
(cpp-logger, GOTCHA, brahma, yaml-cpp, libuv) are cloned and built
automatically, so a source install needs access to their GitHub repositories.

For all build options see [docs/build.rst](docs/build.rst), or the
[rendered documentation](https://dftracer.readthedocs.io/en/latest/build.html).

### Verify the install

Run your application with `DFTRACER_ENABLE=1` and check that the trace contains
the categories you enabled (`MPI`, `MPIIO`, `HDF5`) and not only `POSIX`.

## Usage

DFTracer traces I/O calls automatically; the Python API adds application-level
events on top. A complete example:

```python
import os
import numpy as np
from dftracer.python import dftracer, dft_fn

log_inst = dftracer.initialize_log(logfile=None, data_dir=None, process_id=-1)
compute = dft_fn("COMPUTE")

# Tag a function: DFTracer records one event per call.
@compute.log
def preprocess(index):
    return np.random.randint(255, size=(8, 8, 1024), dtype=np.uint8)

# Plain I/O needs no annotation: calls under DFTRACER_DATA_DIR are traced automatically.
def write_sample(index, records):
    os.makedirs("data", exist_ok=True)
    np.savez(f"data/sample{index}.npz", x=records)

# Trace an arbitrary block.
with dft_fn(cat="block", name="epoch") as dft:
    for i in range(3):
        write_sample(i, preprocess(i))
    dft.update(step=3)

log_inst.finalize()  # flush and close the trace
```

Because `initialize_log` is given no `logfile` or `data_dir`, set them through
the environment:

```bash
# DFTracer appends the app name, hash and .pfw.gz, e.g. ./trace-<hash>-<app>.pfw.gz
export DFTRACER_LOG_FILE=$PWD/trace
# Colon separated paths whose I/O is traced
export DFTRACER_DATA_DIR=$PWD/data:/dev/shm
# Enable DFTracer (off by default)
export DFTRACER_ENABLE=1

python example.py
```

The default mode is `FUNCTION`. To trace I/O in code you cannot modify, or to
combine both, use `DFTRACER_INIT=PRELOAD` with `LD_PRELOAD`; see the
[API](https://dftracer.readthedocs.io/en/latest/api.html) and
[examples](https://dftracer.readthedocs.io/en/latest/examples.html) pages for C,
C++ and preload usage, and
[Visualizing traces](https://dftracer.readthedocs.io/en/latest/perfetto.html) to
open the result.

## Analyzing traces

DFAnalyzer analyzes DFTracer traces at scale with Dask:

```bash
pip install "dftracer[dfanalyzer]"
```

An older, self-contained version is available with `dftracer[dfanalyzer_old]`.
See the [DFAnalyzer docs](https://dftracer.readthedocs.io/en/latest/dfanalyzer_build.html).

## Documentation

* [Building DFTracer](https://dftracer.readthedocs.io/en/latest/build.html)
* [Composable install (MPI, HDF5, GPUs)](https://dftracer.readthedocs.io/en/latest/composable_install.html)
* [MPI support](https://dftracer.readthedocs.io/en/latest/mpi_support.html)
* [GPU tracing](https://dftracer.readthedocs.io/en/latest/gpu_tracing.html)
* [Docker](https://dftracer.readthedocs.io/en/latest/docker.html)
* [API](https://dftracer.readthedocs.io/en/latest/api.html) and [examples](https://dftracer.readthedocs.io/en/latest/examples.html)
* [Trace format](https://dftracer.readthedocs.io/en/latest/trace_format.html)
* [Visualizing traces](https://dftracer.readthedocs.io/en/latest/perfetto.html)
* [DFAnalyzer](https://dftracer.readthedocs.io/en/latest/dfanalyzer_build.html)

## Citation and Reference

The original SC'24 paper describes the design and implementation of the DFTracer code. Please cite this paper and the code if you use DFTracer in your research. 

```
@inproceedings{devarajan_dftracer_2024,
    address = {Atlanta, GA},
    title = {{DFTracer}: {An} {Analysis}-{Friendly} {Data} {Flow} {Tracer} for {AI}-{Driven} {Workflows}},
    shorttitle = {{DFTracer}},
    urldate = {2024-07-31},
    booktitle = {{SC24}: {International} {Conference} for {High} {Performance} {Computing}, {Networking}, {Storage} and {Analysis}},
    publisher = {IEEE},
    author = {Devarajan, Hariharan and Pottier, Loic and Velusamy, Kaushik and Zheng, Huihuo and Yildirim, Izzet and Kogiou, Olga and Yu, Weikuan and Kougkas, Anthony and Sun, Xian-He and Yeom, Jae Seung and Mohror, Kathryn},
    month = nov,
    year = {2024},
}

@misc{devarajan_dftracer_code_2024,
    type = {Github},
    title = {Github {DFTracer}},
    shorttitle = {{DFTracer}},
    url = {https://github.com/llnl-asr/dftracer.git},
    urldate = {2024-07-31},
    journal = {DFTracer: A multi-level dataflow tracer for capture I/O calls from worklows.},
    author = {Devarajan, Hariharan and Pottier, Loic and Velusamy, Kaushik and Zheng, Huihuo and Yildirim, Izzet and Kogiou, Olga and Yu, Weikuan and Kougkas, Anthony and Sun, Xian-He and Yeom, Jae Seung and Mohror, Kathryn},
    month = jun,
    year = {2024},
}
```

## Acknowledgments

This work was performed under the auspices of the U.S. Department of Energy by Lawrence Livermore National Laboratory under Contract DE-AC52-07NA27344; and under the auspices of the National Cancer Institute (NCI) by Frederick National Laboratory for Cancer Research (FNLCR) under Contract 75N91019D00024. This research used resources of the Argonne Leadership Computing Facility, a U.S. Department of Energy (DOE) Office of Science user facility at Argonne National Laboratory and is based on research supported by the U.S. DOE Office of Science-Advanced Scientific Computing Research Program, under Contract No. DE-AC02-06CH11357. Office of Advanced Scientific Computing Research under the DOE Early Career Research Program. Also, This material is based upon work partially supported by LLNL LDRD 23-ERD-045 and 24-SI-005. LLNL-CONF-857447.

## License

DFTracer is distributed under the terms of the MIT license.
All new contributions must be made under this license.

See [LICENSE](LICENSE) and [NOTICE](NOTICE) for details.

SPDX-License-Identifier: MIT

LLNL-CODE-2024514 — Applied Storage Research (ASR)
