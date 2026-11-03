Quickstart
==========

A backend is a folder of its own: a descriptor, its domain code, and a few lines that
serve it. Both versions below make the same app: a counter whose **Add** block adds its
input to a running total, shown on the Dashboard.

C++
---

.. code-block:: text

   counter-cpp/
     CMakeLists.txt     its own project: pulls in the engine, links paperboat::core
     descriptor.toml
     main.cpp

``main.cpp``:

.. code-block:: cpp

   #include <paperboat/sdk.hpp>

   // A block: owns its state; step() turns inputs into outputs.
   struct Add final : paperboat::Block {
     explicit Add(const paperboat::Values& params) : step_by(paperboat::get_or(params, "by")) {}
     paperboat::Values step(const paperboat::Context& ctx, const paperboat::Values& in) override {
       total += step_by * paperboat::get_or(in, "u");
       if (total > 100) ctx.log("over 100", "warn");   // to the Console, tagged with this block
       return {{"total", total}};
     }
     double step_by, total = 0;
   };

   int main(int argc, char** argv) {
     // Static: catalog entries point into the registry, so both live as long as the server.
     static const paperboat::Registry registry = [] {
       paperboat::Registry r = paperboat::builtins();
       r.add(paperboat::define_block<Add>("counter", {.type = "add", .label = "Add",
                                                      .inputs = {{"u"}}, .outputs = {{"total"}},
                                                      .params = {{.id = "by", .default_value = 1}}}));
       return r;
     }();
     static const auto catalog = paperboat::load_descriptor_file(PAPERBOAT_DESCRIPTOR, registry);
     return paperboat::main({.engine = "counter", .catalog = &catalog}, argc, argv, 8780);
   }

``CMakeLists.txt``:

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.25)
   project(counter LANGUAGES CXX)
   set(CMAKE_CXX_STANDARD 20)
   set(PAPERBOAT_ENGINE_DIR "/path/to/paperboat/engine" CACHE PATH "")
   add_subdirectory(${PAPERBOAT_ENGINE_DIR} paperboat EXCLUDE_FROM_ALL)
   add_executable(counter main.cpp)
   target_link_libraries(counter PRIVATE paperboat::core)
   target_compile_definitions(counter PRIVATE PAPERBOAT_DESCRIPTOR="${CMAKE_CURRENT_SOURCE_DIR}/descriptor.toml")

Python
------

.. code-block:: text

   counter-python/
     descriptor.toml
     domain/blocks.py
     server.py

``domain/blocks.py``:

.. code-block:: python

   import paperboat

   class Add(paperboat.Block):
       type, label = "add", "Add"          # its definition: class attributes
       inputs, outputs = ["u"], ["total"]
       params = {"by": 1}

       def __init__(self, params):
           super().__init__(params)
           self.total = 0.0

       def step(self, ctx, inputs):        # what runs
           self.total += self.params["by"] * inputs.get("u", 0.0)
           if self.total > 100:
               ctx.log("over 100", "warn")
           return {"total": self.total}

``server.py``:

.. code-block:: python

   from pathlib import Path
   from domain.blocks import Add
   import paperboat

   if __name__ == "__main__":
       paperboat.serve(paperboat.Backend(engine="counter", descriptor=Path(__file__).with_name("descriptor.toml"),
                                         blocks=[Add], mode="clock", duration=5.0, port=8780))

The descriptor
--------------

The same for both. The domain's own package (``counter``: Add) comes without listing it:

.. code-block:: toml

   domain = "counter"
   import = ["base", "signal", "dashboard"]     # Constant, Key; Sine, Clock…; Graph, Display

   [graph]
   nodes = [
     { id = "one", type = "constant", params = { value = 1 } },
     { id = "add-1", type = "add", params = { by = 2 } },
     { id = "total", type = "display" },
   ]
   edges = [
     { from = ["one", "y"], to = ["add-1", "u"] },
     { from = ["add-1", "total"], to = ["total", "in"] },
   ]

Run it
------

.. code-block:: sh

   ./counter                 # or: python3 server.py   → ws://localhost:8780
   ./counter --run           # the starter once, headless: protocol messages as JSON lines

Open the UI on it: ``http://localhost:5173/?backend=ws://localhost:8780``. Press **Run**.
Then write ``expected.json`` and pass the tests (:doc:`testing`).
