# @paperboatlabs/cli

The [Paperboat](https://github.com/paperboatlab/paperboat) command line.

```sh
npm install -g @paperboatlabs/cli
paperboat --version
paperboat start squero-studio-om  # Squero Studio OM
paperboat pkg list          # apps from other repositories
paperboat update --check    # is a newer paperboat on npm?
```

This package is a small launcher. The native binary comes from the platform package npm picks for
your machine (`@paperboatlabs/cli-linux-x64`, `@paperboatlabs/cli-win32-x64`, ...), installed as an
optional dependency, so install without `--omit=optional`.

A package install carries the CLI only. Building C++ backends and installing toolkits (they compile
into the engine) need paperboat built from source.
