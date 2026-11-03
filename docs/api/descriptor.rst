Descriptors
===========

A backend's ``descriptor.toml`` says what its app offers: which packages and templates
are in the palette (customised or not), the starter graph and station, and how the
Station page shows the station. It is data; it never defines a block (code does) and code
never depends on what's in its starters. Unknown fields are errors, reported with where
they are (``blocks[2] (control/pid): has no param "kq"``).

Top level
---------

``domain`` (text)
   The domain's name. Templates registered in code under this package name
   (``counter/add``) are in the catalog without being listed.

``import`` (list of package names)
   Every template and station template of these packages joins the catalog under its
   default type. Palette order: the domain's own package, then each import in order.

``[[blocks]]``
   A template, used as is or customised. Fields: ``use`` (required: ``"<package>/<block>"``),
   ``type`` (a new type id; the default type stays too), ``label``, ``defaults`` (new param
   defaults: numbers, ``true``/``false`` for booleans, text for ``text``, ``key`` and ``file``
   params), ``config`` (fixed text settings), ``params`` (new params, on an extendable
   parent only). An entry with the same type as an imported one replaces it.

   **Defining a block on a C++ parent.** An extendable template (``signal/generator``) is a
   parent: an entry that ``use``\ s it may add ``params`` (``{ size = 2, fast = true, name =
   "x", key = { default = "Space", type = "key" } }``) and set any ``config``; the parent
   checks the definition at load (an expression that doesn't compile fails here, with where).
   The new block belongs to the domain.

   .. code-block:: toml

      [[blocks]]
      use = "signal/generator"
      type = "wobble"
      label = "Wobble"
      params = { size = 2, fast = true }
      config = { f = "size * sin((fast ? 4 : 1) * t)" }

   .. code-block:: toml

      [[blocks]]
      use = "control/pid"
      type = "pid-theta"
      label = "PID θ"
      defaults = { kp = 40, kd = 8 }

``[[objects]]``
   A station template, used as is or customised: ``use``, ``type``, ``label``, ``defaults``
   (property defaults).

``[graph]`` — the starter graph (optional; without it the Flow page opens empty)
   ``nodes``: ``{ id, type, params?, pos? }``. ``edges``: ``{ from = [node, port], to =
   [node, port] }``. ``groups``: ``{ id, kind = "visual" | "story", label?, nodes,
   collapsed?, pos?, params? = { seconds } }`` (:doc:`flow`). Checked against the catalog
   at load: unknown types, ids and ports fail the backend's start, not a user's run.

``[station]`` — the starter station and its view (optional)
   ``view``: ``"scene"`` (default), ``"graph"`` or ``"web"``; ``page``: the web station's
   page, a path inside the backend's folder (``"station/index.html"``), required for
   ``"web"`` only. ``objects``: ``{ id, type, props?, links?, pos? }``; ``links`` maps each
   link slot to another object's id (checked: the slot exists, the object is there).
   :doc:`station`.

A complete example
------------------

.. code-block:: toml

   domain = "chain"
   import = ["step", "signal", "math", "dashboard"]

   [[blocks]]
   use = "math/threshold"
   label = "Over limit?"
   defaults = { level = 4.5 }

   [station]
   view = "graph"
   objects = [
     { id = "wall", type = "wall" },
     { id = "mass-1", type = "mass", props = { x0 = 1 } },
     { id = "spring-1", type = "spring", links = { a = "wall", b = "mass-1" } },
   ]

   [graph]
   nodes = [{ id = "start", type = "start" }, { id = "limit", type = "threshold" },
            { id = "spring-1", type = "spring" }, { id = "flag", type = "display" }]
   edges = [{ from = ["start", "next"], to = ["check", "in"] },
            { from = ["spring-1", "tension"], to = ["limit", "u"] },
            { from = ["limit", "y"], to = ["flag", "in"] }]
   groups = [{ id = "check", kind = "story", nodes = ["limit", "flag"] }]

Loading
-------

C++: :cpp:func:`paperboat::toolkit::load_descriptor_file` (with the registry holding the
domain's templates). Python: :py:class:`paperboat.Backend` does it. Text props of type
``file`` are relative to the descriptor's folder.
