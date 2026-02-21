#!/usr/bin/env python3
"""
Check ZMQ Sink for DFTracer Testing

This script queries the ZMQ sink to:
1. Get the count of collected events
2. Validate events are valid JSON
3. Clear the event buffer for the next test

Usage: check_zmq_sink.py <test_name> <expected_count> [control_port]
"""

import sys
import os
import json
import argparse
import zmq


def send_command(control_port, cmd):
    """Send a command to the ZMQ sink and return the response."""
    context = zmq.Context()
    socket = context.socket(zmq.REQ)
    socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
    socket.setsockopt(zmq.SNDTIMEO, 5000)

    try:
        socket.connect(f"tcp://localhost:{control_port}")
        socket.send_json(cmd)
        response = socket.recv_json()
        return response
    except zmq.error.Again:
        print(f"Error: Timeout connecting to ZMQ sink on port {control_port}")
        return None
    except Exception as e:
        print(f"Error communicating with ZMQ sink: {e}")
        return None
    finally:
        socket.close()
        context.term()


def validate_events(events):
    """Validate that all events are valid JSON objects."""
    for i, event in enumerate(events):
        if not isinstance(event, dict):
            print(f"Error: Event {i} is not a JSON object: {type(event)}")
            return False

        # Check for required fields (based on dftracer format)
        required_fields = ["name", "cat", "ph"]
        for field in required_fields:
            if field not in event:
                print(f"Warning: Event {i} missing field '{field}'")
                # Not a hard error, just warning

    return True


def main():
    parser = argparse.ArgumentParser(description="Check ZMQ Sink for DFTracer testing")
    parser.add_argument("test_name", help="Name of the test (for logging)")
    parser.add_argument(
        "expected_count",
        type=int,
        help="Minimum number of events expected (0 = expect no events)",
    )
    parser.add_argument(
        "--control-port",
        type=int,
        default=5556,
        help="Control port for ZMQ sink (default: 5556)",
    )
    parser.add_argument(
        "--no-clear", action="store_true", help="Don't clear events after checking"
    )
    args = parser.parse_args()

    print(f"Checking ZMQ sink for test '{args.test_name}'...")
    print(f"Expected at least {args.expected_count} events")

    # Get event count
    response = send_command(args.control_port, {"cmd": "count"})
    if response is None:
        print("Error: Failed to get event count from sink")
        sys.exit(1)

    count = response.get("count", 0)
    print(f"Found {count} events in sink")

    # Handle expected_count == 0 case (disabled test)
    if args.expected_count == 0:
        if count == 0:
            print("Success: Found 0 events as expected")
            sys.exit(0)
        else:
            print(f"Failure: Expected 0 events, found {count}")
            # Get and print events for debugging
            events_response = send_command(args.control_port, {"cmd": "get_events"})
            if events_response:
                print(
                    "Events found:",
                    json.dumps(events_response.get("events", []), indent=2),
                )
            sys.exit(1)

    # Check if we have enough events
    if count < args.expected_count:
        print(f"Failure: Expected at least {args.expected_count} events, found {count}")
        sys.exit(1)

    # Get events for validation
    events_response = send_command(args.control_port, {"cmd": "get_events"})
    if events_response is None:
        print("Error: Failed to get events from sink")
        sys.exit(1)

    events = events_response.get("events", [])

    # Validate events
    if not validate_events(events):
        print("Failure: Event validation failed")
        sys.exit(1)

    print(f"Success: Validated {count} events (>= {args.expected_count})")

    # Clear events for next test
    if not args.no_clear:
        clear_response = send_command(args.control_port, {"cmd": "clear"})
        if clear_response is None or clear_response.get("status") != "ok":
            print("Warning: Failed to clear events from sink")
        else:
            print("Cleared events from sink")

    sys.exit(0)


if __name__ == "__main__":
    main()
