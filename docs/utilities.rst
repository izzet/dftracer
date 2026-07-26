========================
DFTracer Utility scripts
========================

This section describes the utilities provided by DFTracer to assist users with logs.

----------

All scripts are installed with DFTracer in the installation's directories bin folder.

Please see the `DFTracer Utilities Documentation <https://dftracer.readthedocs.io/projects/utils/en/latest/index.html>`_ for
more detailed documentation on these utilities.

To browse a trace interactively, the `DFTracer Viewer <https://marketplace.visualstudio.com/items?itemName=rayandrew.dftracer-viewer>`_
VS Code extension (`source <https://github.com/rayandrew/vscode-dftracer-viewer>`_)
opens :code:`.pfw` and :code:`.pfw.gz` files directly. See :doc:`perfetto` for
loading a trace into Perfetto UI instead.

++++++++++++++++++++
Additional Utilities
++++++++++++++++++++

Sparse Git Clone
^^^^^^^^^^^^^^^^

The script enables sparse git-clone of dftracer traces of the specified branch into 
the specified local directory. It optionally provides interactive selection of directories to clone.

.. code-block:: bash

    <install-dir>/bin/usage: git_sparse_clone_interactive <remote repository> <branch> <local directory> [directory]

Arguments for this script are:

1. **-r repository name** name of remote repository
2. **-b branch name**     name of remote branch
3. **-l local directory** name of local directory to clone into       
4. **-d directory**       optional comma-separated list of directories to sparse-clone.

Sparse Git Push
^^^^^^^^^^^^^^^

The script enables sparse git-push of dftracer traces to the specified remote repository
and branch. It optionally provides interactive selection of directories to push.

.. code-block:: bash

    <install-dir>/bin/usage: git_sparse_push_interactive <remote repository> <branch> [directory]

Arguments for this script are:

1. **-r repository name** name of remote repository
2. **-b branch name**     name of remote branch
3. **-d directory**       optional comma-separated list of directories to sparse-push.
