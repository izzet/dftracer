#!/usr/bin/env python3
"""
ZMQ Sink Process for DFTracer Testing

This script runs a persistent ZeroMQ sink that:
1. Receives events on a PULL socket (port 5555)
2. Accepts control commands on a REP socket (port 5556)

Control commands (JSON):
- {"cmd": "count"} -> {"count": N}
- {"cmd": "get_events"} -> {"events": [...]}
- {"cmd": "clear"} -> {"status": "ok"}
- {"cmd": "stop"} -> {"status": "stopping"}
"""

import sys
import os
import json
import signal
import argparse
import threading
import time
import zmq


class ZMQSink:
    def __init__(self, event_port=5555, control_port=5556):
        self.event_port = event_port
        self.control_port = control_port
        self.events = []
        self.events_lock = threading.Lock()
        self.running = True
        self.context = None
        self.event_socket = None
        self.control_socket = None

    def setup_sockets(self):
        self.context = zmq.Context()

        # PULL socket for receiving events
        self.event_socket = self.context.socket(zmq.PULL)
        self.event_socket.bind(f"tcp://*:{self.event_port}")
        print(f"ZMQ Sink: Event socket bound to tcp://*:{self.event_port}")

        # REP socket for control commands
        self.control_socket = self.context.socket(zmq.REP)
        self.control_socket.bind(f"tcp://*:{self.control_port}")
        print(f"ZMQ Sink: Control socket bound to tcp://*:{self.control_port}")

    def handle_event(self):
        """Receive and store an event from the PULL socket."""
        try:
            # Receive raw data - may contain multiple JSON lines
            message = self.event_socket.recv(flags=zmq.NOBLOCK)
            data = message.decode("utf-8")

            # Split by newlines and parse each JSON line
            with self.events_lock:
                for line in data.strip().split("\n"):
                    if line.strip():
                        try:
                            event = json.loads(line)
                            self.events.append(event)
                        except json.JSONDecodeError as e:
                            print(f"ZMQ Sink: Error parsing JSON: {e}", file=sys.stderr)
            return True
        except zmq.Again:
            return False
        except Exception as e:
            print(f"ZMQ Sink: Error receiving event: {e}", file=sys.stderr)
            return False

    def handle_control(self):
        """Handle a control command from the REP socket."""
        try:
            message = self.control_socket.recv_json(flags=zmq.NOBLOCK)
            cmd = message.get("cmd", "")

            if cmd == "count":
                with self.events_lock:
                    count = len(self.events)
                self.control_socket.send_json({"count": count})

            elif cmd == "get_events":
                with self.events_lock:
                    events = list(self.events)
                self.control_socket.send_json({"events": events})

            elif cmd == "clear":
                with self.events_lock:
                    self.events.clear()
                self.control_socket.send_json({"status": "ok"})

            elif cmd == "stop":
                self.control_socket.send_json({"status": "stopping"})
                self.running = False

            else:
                self.control_socket.send_json({"error": f"Unknown command: {cmd}"})

            return True
        except zmq.Again:
            return False
        except Exception as e:
            print(f"ZMQ Sink: Error handling control: {e}", file=sys.stderr)
            return False

    def run(self):
        """Main event loop using polling."""
        self.setup_sockets()

        poller = zmq.Poller()
        poller.register(self.event_socket, zmq.POLLIN)
        poller.register(self.control_socket, zmq.POLLIN)

        print("ZMQ Sink: Starting main loop...")

        while self.running:
            try:
                socks = dict(poller.poll(timeout=100))  # 100ms timeout

                if self.event_socket in socks:
                    self.handle_event()

                if self.control_socket in socks:
                    self.handle_control()

            except KeyboardInterrupt:
                print("ZMQ Sink: Interrupted")
                break
            except Exception as e:
                print(f"ZMQ Sink: Error in main loop: {e}", file=sys.stderr)

        self.cleanup()
        print("ZMQ Sink: Stopped")

    def cleanup(self):
        """Clean up sockets and context."""
        if self.event_socket:
            self.event_socket.close()
        if self.control_socket:
            self.control_socket.close()
        if self.context:
            self.context.term()


def main():
    parser = argparse.ArgumentParser(description="ZMQ Sink for DFTracer testing")
    parser.add_argument(
        "--event-port",
        type=int,
        default=5555,
        help="Port for event PULL socket (default: 5555)",
    )
    parser.add_argument(
        "--control-port",
        type=int,
        default=5556,
        help="Port for control REP socket (default: 5556)",
    )
    args = parser.parse_args()

    sink = ZMQSink(event_port=args.event_port, control_port=args.control_port)

    # Handle SIGTERM gracefully
    def signal_handler(signum, frame):
        print("ZMQ Sink: Received signal to stop")
        sink.running = False

    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    sink.run()


if __name__ == "__main__":
    main()
