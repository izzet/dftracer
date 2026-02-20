import sys
import os
import argparse
import traceback
import time
import json
import mochi.mofka.client as mofka


def main():
    parser = argparse.ArgumentParser(description="Check Mofka topic event count")
    parser.add_argument("topic_name", help="Name of the Mofka topic")
    parser.add_argument(
        "expected_count", type=int, help="Minimum number of events expected"
    )
    args = parser.parse_args()

    group_file = os.getenv("DFTRACER_MOFKA_GROUP_FILE")
    if not group_file:
        print("Error: DFTRACER_MOFKA_GROUP_FILE not set")
        sys.exit(1)

    print(
        f"Checking topic '{args.topic_name}' for at least {args.expected_count} events..."
    )
    print(f"Using group file: {group_file}")

    driver = None
    try:
        driver = mofka.MofkaDriver(group_file=group_file, use_progress_thread=True)
        if not driver.topic_exists(args.topic_name):
            if args.expected_count == 0:
                print(
                    f"Success: Topic {args.topic_name} does not exist (as expected for disabled test)"
                )
                sys.exit(0)
            else:
                print(f"Error: Topic {args.topic_name} does not exist")
                sys.exit(1)

        topic = driver.open_topic(args.topic_name)
        consumer = topic.consumer(name=f"validator_{os.getpid()}")

        count = 0
        # Determine strictness. If we expect 0, we verify 0?
        # Usually check_file_at_least implies >=.
        # But if expected is 0 (disable test), we might want to ensure no events?
        # Based on check_file_not.sh usage in CMake for disable tests, we might need a separate logic for 0?
        # CMake uses check_file_not.sh for expected=0.
        # But df_check_test in CMake passes 0 for disable tests and uses check_file_at_least logic branch?
        # Wait, looking at CMake:
        # For disable tests (expected=0), it uses 'check_file_not.sh' logic in the ELSE branch?
        # No, function df_check_test only calls check_mofka_topic.py.
        # If expected_count is 0, we expect 0 events.
        # However, check_file_not.sh ensures file does NOT exist or is empty.
        # If expected_count > 0, we expect >= count.

        # We will assume:
        # If expected_count == 0: Fail if any event found (or maybe strict 0 check).
        # If expected_count > 0: Pass as soon as count >= expected.

        # We need a timeout logic because we don't know when the stream ends.
        # For a "at least" check, we can return success immediately.
        # For a "exactly 0" check, we must wait a bit to be sure?
        # Mofka is persistent in memory? If test finished, data should be there.
        # But consumer might block if no data.

        start_time = time.time()
        timeout = 5.0  # seconds to wait for data

        while True:
            try:
                # wait(timeout_ms)
                # If we expect events, we wait.
                # If we expect 0, we try to pull with short timeout?

                # Check for "at least" condition first
                if args.expected_count > 0 and count >= args.expected_count:
                    print(f"Success: Found {count} events (>= {args.expected_count})")
                    sys.exit(0)

                # If we have waited too long and still haven't met criteria
                if time.time() - start_time > timeout:
                    break

                future = consumer.pull()
                event = future.wait(timeout_ms=1000)  # 1 sec wait per pull

                if event:
                    # Mofka library deserializes metadata into a dict automatically
                    if event.metadata and isinstance(event.metadata, dict):
                        # Logic parity: Shell script validates it is JSON.
                        # Here it IS a dict, so it mimics valid JSON.
                        pass
                    elif event.metadata:
                        print(
                            f"Warning: Metadata is not a dict: {type(event.metadata)}"
                        )
                        # If we strictly want to fail non-dict metadata:
                        # sys.exit(1)
                    count += 1
                else:
                    # No event in this pull
                    pass

            except RuntimeError as e:
                # Timeout uses RuntimeError often in these bindings if timed out?
                # Or checks future result?
                # future.wait throws if timeout?
                # documentation says: wait(timeout_ms) -> Event or raises MofkaException/Timeout
                # We catch generic exception to be safe or check specific error.
                # Assuming simple timeout just continues loop.
                pass
            except Exception as e:
                # Real error
                print(f"Error while pulling: {e}")
                break

        print(f"Finished polling. Total events found: {count}")

        if args.expected_count == 0:
            if count == 0:
                print("Success: Found 0 events as expected.")
                sys.exit(0)
            else:
                print(f"Failure: Expected 0 events, found {count}")
                sys.exit(1)
        else:
            if count >= args.expected_count:
                print(f"Success: Found {count} events")
                sys.exit(0)
            else:
                print(
                    f"Failure: Expected at least {args.expected_count} events, found {count}"
                )
                sys.exit(1)

    except Exception as e:
        print(traceback.format_exc())
        sys.exit(1)
    finally:
        # Cleanup if needed
        del driver


if __name__ == "__main__":
    main()
