================
Definitions
================

This section defines some terms used throughout the document.

1. *Category (cat)* : The sub-category of an event within its producing layer, such as ``POSIX`` or ``STDIO``. Free-form, and used to group similar events.
2. *Name (name)* : The name of the event.
3. *Type (type)* : The instrumentation layer that produced the event, as an integer from a closed set. See :doc:`trace_format`.
4. *Phase (ph)* : The kind of record: an individual event, a counter, an aggregate or metadata. See :doc:`trace_format`.

The structure of each DFTracer event is inspired by the `chrome tracing document <https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview>`_, but the format does not follow it: see :doc:`trace_format` and :doc:`perfetto`.