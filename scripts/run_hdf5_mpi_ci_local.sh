#!/usr/bin/env bash
#
# Reproduce the "HDF5 MPI Trace CI" GitHub Actions job locally, using the
# exact same container image (hdevarajan92/brahma-ci:latest) as
# .github/workflows/hdf5-mpi-trace-ci.yml, via either podman or docker --
# whichever is available. Runs the real ./autobuild.sh build + the
# gdb-wrapped test_c_hdf5_mpi verification step, so crashes (e.g. SIGSEGV)
# can be root-caused with a live backtrace instead of only a CI log.
#
# Engine selection: podman is preferred when both are available or when
# only podman is installed (e.g. on LLNL's Tuolumne and similar systems,
# where podman is provided instead of docker). Override with --engine.
#
# Why copy-in instead of a bind mount: the container runs as root (the
# workflow uses `options: --user root`), but the *build* steps `spack load`
# etc. run as the "spack" user's environment; regardless, a bind-mounted
# source directory keeps host file ownership/permissions which are commonly
# mismatched with the container's uid. Copying the source in (via
# `<engine> cp`) sidesteps this identically for both engines.
#
# Usage:
#   scripts/run_hdf5_mpi_ci_local.sh [options]
#
# Options:
#   --engine <podman|docker|auto>   Container engine to use (default: auto)
#   --hdf5 <spec>                   HDF5 spack spec (default: hdf5@1.12.3, matrix entry 1)
#   --mpi <spec>                    MPI spack spec (default: mpich@4.2.3, matrix entry 1)
#   --all                           Run the full CI matrix (both combinations) instead of one
#   --keep                          Don't remove the container when done (for interactive debugging)
#   --shell                         Drop into an interactive shell in the container instead of running CI
#   -h, --help                      Show this help
#
# Examples:
#   scripts/run_hdf5_mpi_ci_local.sh
#   scripts/run_hdf5_mpi_ci_local.sh --hdf5 hdf5@1.14.5 --mpi openmpi@5.0.6
#   scripts/run_hdf5_mpi_ci_local.sh --all
#   scripts/run_hdf5_mpi_ci_local.sh --keep --shell

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="hdevarajan92/brahma-ci:latest"
CONTAINER_NAME="dftracer-hdf5mpi-ci-local-$$"
CONTAINER_SRC_DIR="/root/dftracer"

# Matrix combinations, matching .github/workflows/hdf5-mpi-trace-ci.yml exactly.
MATRIX_HDF5=("hdf5@1.12.3" "hdf5@1.14.5")
MATRIX_MPI=("mpich@4.2.3" "openmpi@5.0.6")

ENGINE="auto"
HDF5_SPEC=""
MPI_SPEC=""
RUN_ALL=0
KEEP_CONTAINER=0
INTERACTIVE_SHELL=0

usage() {
  sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --engine) ENGINE="$2"; shift 2 ;;
    --hdf5) HDF5_SPEC="$2"; shift 2 ;;
    --mpi) MPI_SPEC="$2"; shift 2 ;;
    --all) RUN_ALL=1; shift ;;
    --keep) KEEP_CONTAINER=1; shift ;;
    --shell) INTERACTIVE_SHELL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# Engine detection
# ---------------------------------------------------------------------------
detect_engine() {
  if [ "$ENGINE" != "auto" ]; then
    echo "$ENGINE"
    return
  fi
  if command -v podman >/dev/null 2>&1; then
    echo "podman"
  elif command -v docker >/dev/null 2>&1; then
    echo "docker"
  else
    echo "ERROR: neither podman nor docker found on PATH" >&2
    exit 1
  fi
}

ENGINE="$(detect_engine)"
if ! command -v "$ENGINE" >/dev/null 2>&1; then
  echo "ERROR: requested engine '$ENGINE' not found on PATH" >&2
  exit 1
fi
echo "[run_hdf5_mpi_ci_local] Using container engine: $ENGINE"

# ---------------------------------------------------------------------------
# Container lifecycle
# ---------------------------------------------------------------------------
cleanup() {
  if [ "$KEEP_CONTAINER" -eq 0 ]; then
    "$ENGINE" rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
  else
    echo "[run_hdf5_mpi_ci_local] --keep set: leaving container running as '$CONTAINER_NAME'"
    echo "[run_hdf5_mpi_ci_local]   Attach with: $ENGINE exec -it -u root $CONTAINER_NAME bash"
    echo "[run_hdf5_mpi_ci_local]   Remove with: $ENGINE rm -f $CONTAINER_NAME"
  fi
}
STAGING_DIR="$(mktemp -d)"
cleanup_all() {
  rm -rf "$STAGING_DIR"
  cleanup
}
trap cleanup_all EXIT

echo "[run_hdf5_mpi_ci_local] Pulling ${IMAGE} ..."
if ! "$ENGINE" pull "$IMAGE"; then
  echo "[run_hdf5_mpi_ci_local] Pull failed (offline?); trying to use a local copy if present ..."
  "$ENGINE" image exists "$IMAGE" || {
    echo "ERROR: image ${IMAGE} not available locally and could not be pulled" >&2
    exit 1
  }
fi

echo "[run_hdf5_mpi_ci_local] Starting container ${CONTAINER_NAME} (as root, matching CI's options: --user root) ..."
"$ENGINE" run -d --name "$CONTAINER_NAME" --user root "$IMAGE" sleep infinity >/dev/null

echo "[run_hdf5_mpi_ci_local] Copying source tree into container (excluding build/.git dirs) ..."
# Copy into a staging dir on the host first so we can exclude build*/ and
# .git/ without needing tar/rsync support inside the (possibly minimal)
# container image.
tar -C "$REPO_ROOT" \
  --exclude='./build' --exclude='./build-*' --exclude='./.git' \
  --exclude='./dftracer-build' --exclude='./dftracer-install' \
  --exclude='./data' --exclude='./venv' --exclude='./install' \
  --exclude='./results' --exclude='./hydra_log' \
  --exclude='*.core' \
  -cf - . | tar -C "$STAGING_DIR" -xf -

"$ENGINE" exec --user root "$CONTAINER_NAME" mkdir -p "$CONTAINER_SRC_DIR"
"$ENGINE" cp "$STAGING_DIR/." "$CONTAINER_NAME:$CONTAINER_SRC_DIR/"

if [ "$INTERACTIVE_SHELL" -eq 1 ]; then
  echo "[run_hdf5_mpi_ci_local] Dropping into interactive shell (source is at $CONTAINER_SRC_DIR) ..."
  echo "[run_hdf5_mpi_ci_local]   Load an environment with:"
  echo "[run_hdf5_mpi_ci_local]     source /home/spack/spack/share/spack/setup-env.sh"
  echo "[run_hdf5_mpi_ci_local]     spack load hdf5@1.12.3 mpich@4.2.3 cmake"
  KEEP_CONTAINER=1
  "$ENGINE" exec -it -u root -w "$CONTAINER_SRC_DIR" "$CONTAINER_NAME" bash
  exit 0
fi

run_one_combination() {
  local hdf5="$1" mpi="$2"
  local build_dir="${CONTAINER_SRC_DIR}/dftracer-build-${hdf5//[@\/]/_}-${mpi//[@\/]/_}"
  local install_dir="${CONTAINER_SRC_DIR}/dftracer-install-${hdf5//[@\/]/_}-${mpi//[@\/]/_}"

  echo ""
  echo "==================================================================="
  echo "[run_hdf5_mpi_ci_local] Building + testing: ${hdf5}  /  ${mpi}"
  echo "==================================================================="

  # Mirrors .github/workflows/hdf5-mpi-trace-ci.yml's "Install system build
  # dependencies", "Build dftracer with HDF5 and MPI", and "Verify HDF5 and
  # MPI events in dftracer traces" steps.
  "$ENGINE" exec --user root "$CONTAINER_NAME" bash -lc "
    set -euo pipefail

    # Under rootless podman, apt's sandbox drop-privileges-to-_apt step fails
    # (no uid mapping for it in the container's limited user namespace) --
    # disable it since we're already running as root in an ephemeral container.
    export APT_OPTS='-o APT::Sandbox::User=root'
    apt-get \${APT_OPTS} update -qq
    apt-get \${APT_OPTS} install -y --no-install-recommends \
      build-essential ca-certificates git hwloc libhwloc-dev \
      libyaml-cpp-dev ninja-build pkg-config python3-dev python3-venv \
      zlib1g-dev gdb >/dev/null
    rm -rf /var/lib/apt/lists/*

    source /home/spack/spack/share/spack/setup-env.sh
    spack load '${hdf5}' '${mpi}' cmake

    HDF5_DIR=\$(spack location -i '${hdf5}')
    echo \"HDF5_DIR=\${HDF5_DIR}\"
    echo \"MPI wrappers: \$(which mpicc) \$(which mpicxx)\"

    export OMPI_ALLOW_RUN_AS_ROOT=1
    export OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

    cd '${CONTAINER_SRC_DIR}'
    rm -rf '${build_dir}' '${install_dir}'
    ./autobuild.sh \
      --install-mode cmake \
      --build-dir '${build_dir}' \
      --install-prefix '${install_dir}' \
      --with-hdf5 \"\${HDF5_DIR}\" \
      --with-c-compiler mpicc \
      --with-cxx-compiler mpicxx \
      --enable-mpi \
      --enable-tests \
      --jobs \"\$(nproc)\"

    VERIFY_TRACE_DIR=\"${build_dir}/trace-verify\"
    VERIFY_DATA_DIR=\"\${VERIFY_TRACE_DIR}/data\"
    mkdir -p \"\${VERIFY_DATA_DIR}\"

    PRELOAD_LIB=\$(find '${build_dir}' -name 'libdftracer_preload_dbg.so' | head -1)
    if [ -z \"\${PRELOAD_LIB}\" ]; then
      echo 'ERROR: libdftracer_preload_dbg.so not found under ${build_dir}'
      find '${build_dir}' -name '*.so' | grep -i preload || true
      exit 1
    fi
    echo \"Preload lib: \${PRELOAD_LIB}\"

    TEST_BIN='${build_dir}/bin/test_c_hdf5_mpi'
    if [ ! -x \"\${TEST_BIN}\" ]; then
      echo \"ERROR: test binary not found: \${TEST_BIN}\"
      exit 1
    fi

    MPIRUN_ROOT_FLAGS=''
    if [ '${mpi%%@*}' = 'openmpi' ]; then
      MPIRUN_ROOT_FLAGS='--allow-run-as-root'
    fi

    export DFTRACER_ENABLE=1
    export DFTRACER_INC_METADATA=1
    export DFTRACER_LOG_FILE=\"\${VERIFY_TRACE_DIR}/hdf5_mpi_verify\"
    export DFTRACER_DATA_DIR=/
    export DFTRACER_INIT=PRELOAD
    export DFTRACER_TRACE_COMPRESSION=1
    export DFTRACER_BIND_SIGNALS=0
    export OMPI_ALLOW_RUN_AS_ROOT=1
    export OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1
    export OMPI_MCA_btl_vader_single_copy_mechanism=none
    export LD_PRELOAD=\"\${PRELOAD_LIB}\"
    export LD_LIBRARY_PATH=\"\${HDF5_DIR}/lib:\${HDF5_DIR}/lib64:\${LD_LIBRARY_PATH:-}\"

    echo 'Running test_c_hdf5_mpi with dftracer preload (under gdb to capture a backtrace on crash)...'
    ulimit -c unlimited
    timeout 240 mpirun \${MPIRUN_ROOT_FLAGS} -np 2 \
      gdb -q -batch \
      -ex run \
      -ex 'thread apply all bt full' \
      -ex quit \
      --args \"\${TEST_BIN}\" \"\${VERIFY_DATA_DIR}\" 2>&1 | tee \"\${VERIFY_TRACE_DIR}/gdb.log\"
    GDB_STATUS=\${PIPESTATUS[0]}
    if grep -q 'SIGSEGV\|Program terminated' \"\${VERIFY_TRACE_DIR}/gdb.log\"; then
      echo 'ERROR: test_c_hdf5_mpi crashed, see backtrace above (also saved at ${build_dir}/trace-verify/gdb.log inside the container)'
      exit 1
    fi
    exit \"\${GDB_STATUS}\"
  "
}

if [ "$RUN_ALL" -eq 1 ]; then
  status=0
  for i in "${!MATRIX_HDF5[@]}"; do
    if ! run_one_combination "${MATRIX_HDF5[$i]}" "${MATRIX_MPI[$i]}"; then
      status=1
      echo "[run_hdf5_mpi_ci_local] FAILED: ${MATRIX_HDF5[$i]} / ${MATRIX_MPI[$i]}"
    fi
  done
  exit "$status"
else
  hdf5="${HDF5_SPEC:-${MATRIX_HDF5[0]}}"
  mpi="${MPI_SPEC:-${MATRIX_MPI[0]}}"
  run_one_combination "$hdf5" "$mpi"
fi
