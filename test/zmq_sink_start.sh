#!/bin/bash
# Start the ZMQ sink process for testing
# Usage: zmq_sink_start.sh <python_exe> <log_file> [event_port] [control_port]

PYTHON_EXE=$1
LOG_FILE=$2
EVENT_PORT=${3:-5555}
CONTROL_PORT=${4:-5556}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Kill any existing sink on these ports
pkill -f "zmq_sink.py.*--event-port ${EVENT_PORT}" 2>/dev/null || true

# Start the sink in background
$PYTHON_EXE $SCRIPT_DIR/zmq_sink.py --event-port $EVENT_PORT --control-port $CONTROL_PORT > "$LOG_FILE" 2>&1 &
SINK_PID=$!

# Save PID for later cleanup
echo $SINK_PID > "${LOG_FILE}.pid"

# Wait for sink to be ready (poll control socket using ZMQ)
MAX_RETRIES=30
RETRY_COUNT=0
while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    # Use Python to check if sink is up (ZMQ sockets need ZMQ protocol)
    RESULT=$($PYTHON_EXE -c "
import zmq
import sys
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.setsockopt(zmq.RCVTIMEO, 100)
sock.setsockopt(zmq.SNDTIMEO, 100)
sock.connect('tcp://localhost:$CONTROL_PORT')
try:
    sock.send_json({'cmd': 'count'})
    result = sock.recv_json()
    print('OK')
    sys.exit(0)
except:
    sys.exit(1)
finally:
    sock.close()
    ctx.term()
" 2>/dev/null)
    if [ "$RESULT" = "OK" ]; then
        echo "ZMQ Sink started with PID $SINK_PID on event port $EVENT_PORT, control port $CONTROL_PORT"
        exit 0
    fi
    RETRY_COUNT=$((RETRY_COUNT + 1))
    sleep 0.1
done

echo "Failed to start ZMQ Sink" >&2
exit 1
