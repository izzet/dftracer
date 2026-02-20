#!/bin/bash

# Detect the best available fabric protocol for bedrock.
# Checks for CXI fabric availability (used on Cray EX systems with Slingshot).
# Returns 'ofi+cxi' if CXI devices are found, otherwise falls back to 'tcp'.
#
# Environment variable that can override detection:
# - DFTRACER_MOFKA_FABRIC_PROTOCOL: Force a specific protocol (e.g., 'ofi+cxi', 'tcp')

detect_fabric_protocol() {
    # Allow environment variable override
    if [ -n "$DFTRACER_MOFKA_FABRIC_PROTOCOL" ]; then
        echo "$DFTRACER_MOFKA_FABRIC_PROTOCOL"
        return 0
    fi

    # Check for CXI devices (Cray EX / Slingshot interconnect)
    # CXI devices typically appear as /dev/cxi* or /dev/hfi*
    local cxi_devices
    cxi_devices=$(ls /dev/cxi* /dev/hfi* 2>/dev/null | head -1)
    if [ -n "$cxi_devices" ]; then
        echo "ofi+cxi"
        return 0
    fi

    # Check for libfabric CXI provider availability
    if command -v fi_info &>/dev/null; then
        if fi_info -p cxi &>/dev/null; then
            echo "ofi+cxi"
            return 0
        fi
    fi

    # Fall back to TCP for local development or systems without CXI
    echo "tcp"
}

# PID file for tracking the server process
PID_FILE="${4:-mofka_server.pid}"

# Detect fabric protocol
FABRIC_PROTOCOL=$(detect_fabric_protocol)

echo "Using fabric protocol: $FABRIC_PROTOCOL"

# Start the server with detected or overridden protocol
$1 "$FABRIC_PROTOCOL" -c $2 -v trace 1> $3 2>&1 &
SERVER_PID=$!

# Save PID to file for later termination
echo "$SERVER_PID" > "$PID_FILE"

echo "Server started with PID: $SERVER_PID (saved to $PID_FILE)"

# Wait for the group file to be created before tests proceed.
GROUP_FILE="mofka.group.json"
for _ in $(seq 1 30); do
    if [ -s "$GROUP_FILE" ]; then
        sleep 3
        exit 0
    fi
    # Check if process is still running
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Server process (PID: $SERVER_PID) died unexpectedly" >&2
        rm -f "$PID_FILE"
        exit 1
    fi
    sleep 1
done

echo "Timed out waiting for $GROUP_FILE" >&2
exit 1
