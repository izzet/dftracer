#!/bin/bash
# Stop the ZMQ sink process
# Usage: zmq_sink_stop.sh <control_port>

CONTROL_PORT=${1:-5556}

# Send stop command via control socket
echo '{"cmd": "stop"}' | timeout 5 nc -q 1 localhost $CONTROL_PORT > /dev/null 2>&1

# Give it a moment to shut down gracefully
sleep 0.5

# Force kill if still running
pkill -f "zmq_sink.py" 2>/dev/null || true

echo "ZMQ Sink stopped"
exit 0
