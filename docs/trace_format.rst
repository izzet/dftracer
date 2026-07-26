===================
DFTracer Format
===================

At a high-level, DFTracer events are inspired by the `chrome tracing document`_.
The similarity to chrome tracing format is in the structure of each event type.
Specifically, we use complete and metadata events within DFTracer.
However, it does not strictly follow the format due to parallelization reasons. 
Essentially, the DFTracer format is as follows

.. code-block:: bash

    [ # Marking the start of the trace 
    JSON LINES
    ] # Marking the end of the trace 

The "[" and "]" are required only for perfetto format.
Each JSON line is an event of two types: a) Complete Events and b) Metadata Events


----------

.. _`Event Types`:

----------------------------------------
Event Types
----------------------------------------

Every event carries a "type" column: a small integer naming the instrumentation
layer that produced it. It is a closed vocabulary, unlike "cat", which stays
free-form and holds the sub-category within that layer.

========  =============  ===================================================
 Value     Name           Produced by
========  =============  ===================================================
 0         UNKNOWN        Never written; fallback for unrecognised values
 1         DFTRACER       Internal events: start, end, hash and metadata
 2         C_APP          C API (``DFTRACER_C_*``)
 3         LIBC_IO        POSIX and STDIO interception (cat: POSIX, STDIO)
 4         HIP            rocprofiler (cat: the rocprofiler kind name)
 5         HDF5           HDF5 interception
 6         PYTHON         Python API (cat: caller-supplied)
 7         PSUTIL         dftracer_service telemetry (cat: sys, net, io)
 8         FINSTRUMENT    ``-finstrument-functions`` (cat: FUNC)
 9         CPP_APP        C++ API (``DFTRACER_CPP_*``)
 10        MPI            MPI and MPI-IO interception (cat: MPI, MPIIO)
========  =============  ===================================================

The numbering is append-only: values are never renumbered or reused, so a
reader can always interpret a trace written by an older DFTracer. A reader that
meets a value it does not know should treat it as UNKNOWN rather than fail.

Custom metadata events carry the type of the API that logged them, so metadata
written from Python is PYTHON and metadata written through the C API is C_APP.
The hash events (FH, SH, HH) and the process and thread metadata are DFTRACER:
they are emitted by the tracer's own bookkeeping. A hash record in particular
is written once by whichever layer first saw the string, so attributing it to
that layer would not be meaningful.

----------

----------------------------------------
Complete Events
----------------------------------------

A sample complete event looks like the following

.. code-block:: bash

    {"id":8,"name":"CUSTOM_BLOCK","cat":"CPP_APP","type":9,"pid":3308801,"tid":6617602,"ts":1727286231145121,"dur":1000054,"ph":"X","args":{"hhash":39537,"p_idx":7,"key":0,"level":3}}

Here, "id" refers to the index of the record in the process. Only for complete events.
"name" refers to the event name.
"cat" refers to category.
"type" refers to the instrumentation layer that produced the event (see `Event Types`_).
"pid" and "tid" are the process id and thread id, respectively.
"ts" and "dur" is the timestamp and duration of the event.
Finally, "args" is a dictionary of other events.

Within DFTracer, we store all filename, hostname, and string as hash values.
The actual value is stored as a separate Metadata Event.

----------------------------------------
Metadata Events
----------------------------------------

Within DFTracer, there are two types of metadata events. 
One is similar to the chrome tracing format, which modifies the name in the perfetto ui view. 
For this, we support process name (in case of MPI we show rank of the process) and thread name which shows process id.
Another type of metadata event supports are internal metadata events such as hash function. 
A sample hash event looks like the following

.. code-block:: bash

    {"id":1,"name":"HH","cat":"dftracer","type":0,"pid":3487304,"tid":3487304,"ph":"M","args":{"name":"corona211","value":51242}}


Here, "name" is the type of hash. "HH" for hostname hash, "FH" for filename hash, "SH" for general string hash.
"args.name" refers to the string that was hashed.
Finally, the "value" refers to the hash value. 


Finally, there are custom metadata events to store auxiliary information. 
An example of such event is below.

.. code-block:: bash

    {"name":"PR","cat":"dftracer","type":0,"pid":3487304,"tid":6974608,"ph":"M","args":{"name":"core_affinity","value":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47]}}

Here, the "name" "PR" represents a metadata event for process and the args contain the metadata name and its value. 


.. _`chrome tracing document`: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.yr4qxyxotyw
