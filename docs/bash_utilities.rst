========================
Bash Utility scripts 
========================

This section describes the bash utilities that are compatible with DFTracer logs

----------

------------------
Handling .pfw files
------------------

The DFTracer format with extension .pfw is uncompressed file which can be viewed using the following utilities.

1. `vim` : Edit .pfw files
2. `cat`, `head`, or `tail`: view portion of the pfw files


----------------------
Handling .pfw.gz files
----------------------

The DFtracer compressed format with .pfw extension can be first decompressed using gzip and then piped to the above .pfw utilities.

.. code-block:: bash

    gzip -c -d `echo *.gz` | head

--------------------
Extracting JSON data
--------------------

Once the uncompressed data is parsed. The JSON utility `jq` can be used to parse args.

A trace is newline-delimited JSON with no enclosing array, so it can be fed to
`jq` directly.

For uncompressed files

.. code-block:: bash

    cat *.pfw | jq -c '.'


For compressed files

.. code-block:: bash

    gzip -c -d `echo *.gz` | jq -c '.'

We can extract specific fields from these JSON lines as follows

1. `jq -c '.name'`: extracts all the names of events
2. `jq -c '.cat'`: extracts all the category of events
3. `jq -c '.type'`: extracts the instrumentation layer that produced each event
4. `jq -c '.ph'`: extracts the record kind
5. `jq -c '.args.hostname'`: extracts the fields from extra args like hostname in this case.

See :doc:`trace_format` for the values `type` and `ph` can take. To keep only
the individual events from POSIX and STDIO interception, for example:

.. code-block:: bash

    cat *.pfw | jq -c 'select(.ph == 1 and .type == 3)'

Useful querying using jq
************************

Extract unique functions with their counts from traces.

.. code-block:: bash

    cat *.pfw | jq -c '.name' | sort | uniq -c 

Extract unique categories with their counts from traces.

.. code-block:: bash

    cat *.pfw | jq -c '.cat' | sort | uniq -c 

Extract unique process id and thread id combination with their counts from traces.

.. code-block:: bash

    cat *.pfw | jq -c '"\(.pid) \(.tid)"' | sort | uniq -c 

Extract min timestamp

.. code-block:: bash

    cat *.pfw | jq -c '.ts | tonumber' | sort -n | tail -1

Extract max timestamp

.. code-block:: bash

    cat *.pfw | jq -c '.ts | tonumber' | sort -n | tail -n 1


For more commands on `jq` refer to  `JQ Manual
<https://jqlang.github.io/jq/manual/>`_.


-------------------
Querying AI DFTracer
-------------------

.. code-block:: bash

   function extract_duration() {
        local name="$1"     # Event name to search for
        local cat="$2"      # Category to search in (optional)
        
        # Default to searching by "name" field if no category specified
        if [ -z "$cat" ]; then
            cat="name"
        fi
        
        # Extract duration data for the specified event:
        # 1. Decompress all .gz files
        # 2. Filter for events matching the category and name
        # 3. Clean non-printable characters
        # 4. Parse JSON and extract PID and duration
        # 5. Sum durations by PID and find maximum
        # 6. Convert from microseconds to seconds
        gzip -dc *.gz | \
            grep "\"$cat\":\"$name\"" | \
            LC_ALL=C sed 's/[^[:print:]\r\t]//g' | \
            jq -R -c "fromjson?" | \
            jq -c '"\(.pid) \(.dur)"' | \
            awk '{dur[$1]+=$2} END{max=0; for(p in dur) if(dur[p]>max) max=dur[p]; print max/1000000 " seconds"}'
    }


Overall
*******

.. code-block:: bash

    extract_duration "ai_root"
    extract_duration "train"
    extract_duration "epoch"

Checkpointing
*************

.. code-block:: bash

    extract_duration "restart"
    extract_duration "capture"

Compute
*******

.. code-block:: bash

    extract_duration "fetch.block"
    extract_duration "compute"
    extract_duration "backward"
    extract_duration "forward"

I/O
***

.. code-block:: bash

    extract_duration "fetch.iter"
    extract_duration "item"
    extract_duration "POSIX" "cat"
