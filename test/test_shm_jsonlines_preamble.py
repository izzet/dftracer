"""Source-level contract for streaming JsonLines initialization."""

from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "src/dftracer/core/serialization/json_line.cpp"


def test_shm_jsonlines_omits_the_non_json_array_preamble():
    source = SOURCE.read_text(encoding="utf-8")
    assert "#if DFTRACER_WRITER_TYPE_MOFKA || DFTRACER_WRITER_TYPE_SHM\n  return 0;" in source
    # File and ZMQ-style JSON-array writers retain their historical preamble.
    assert "#else\n  buffer[0] = '[';\n  buffer[1] = '\\n';\n  return 2;" in source
