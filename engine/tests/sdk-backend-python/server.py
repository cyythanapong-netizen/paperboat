"""A minimal Python backend for the installed packages (pb#223): the release jobs run
`paperboat check` on it with only a package installed, so `import paperboat` has to work
with no pip step (the CLI puts the package's lib/paperboat/python on PYTHONPATH)."""

import paperboat


class Gain(paperboat.Block):
    type, label = "gain-py", "Gain (Python)"
    inputs, outputs = ["u"], ["y"]
    params = {"k": 2.0}

    def step(self, ctx, inputs):
        return {"y": self.params["k"] * inputs.get("u", 0.0)}


class Offset(paperboat.Block):
    type, label = "offset-py", "Offset (Python)"
    inputs, outputs = ["u"], ["y"]
    params = {"by": 1.0}

    def step(self, ctx, inputs):
        y = inputs.get("u", 0.0) + self.params["by"]
        ctx.log(f"offset to {y:g}")
        return {"y": y}


paperboat.serve(paperboat.Backend(engine="python-sdk-backend", descriptor="descriptor.toml",
                                  blocks=[Gain, Offset], mode="once", port=8782))
