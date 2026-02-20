#!/bin/bash

# Stop the mofka server using the PID file created during startup.
# This ensures safe termination of the correct process.

# PID file location (should match what was used in mofka_server_start.sh)
PID_FILE="${1:-mofka_server.pid}"

if [ -f "$PID_FILE" ]; then
    SERVER_PID=$(cat "$PID_FILE")
    
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Stopping server with PID: $SERVER_PID"
        kill "$SERVER_PID"
        
        # Wait for process to terminate
        for _ in $(seq 1 10); do
            if ! kill -0 "$SERVER_PID" 2>/dev/null; then
                echo "Server stopped successfully"
                rm -f "$PID_FILE"
                exit 0
            fi
            sleep 1
        done
        
        # Force kill if still running
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "Server did not stop gracefully, force killing..."
            kill -9 "$SERVER_PID" 2>/dev/null
            rm -f "$PID_FILE"
        fi
    else
        echo "Server process (PID: $SERVER_PID) is not running"
        rm -f "$PID_FILE"
    fi
else
    echo "PID file not found: $PID_FILE"
    echo "Falling back to pkill for 'bedrock'..."
    pkill -f "bedrock"
fi

exit 0
