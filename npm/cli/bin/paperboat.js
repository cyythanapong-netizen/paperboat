#!/usr/bin/env node
// @paperboat/cli: runs the native paperboat binary from the platform package npm installed
// (@paperboat/cli-<platform>-<arch>). PAPERBOAT_BIN overrides the binary, for development.
"use strict";
const { spawnSync } = require("node:child_process");
const os = require("node:os");
const path = require("node:path");

const key = `${process.platform}-${process.arch}`;
let bin = process.env.PAPERBOAT_BIN;
if (!bin) {
  try {
    const dir = path.dirname(require.resolve(`@paperboat/cli-${key}/package.json`));
    bin = path.join(dir, "bin", process.platform === "win32" ? "paperboat.exe" : "paperboat");
  } catch {
    console.error(
      `paperboat: no prebuilt binary for ${key} (@paperboat/cli-${key} is not installed).\n` +
        "It is an optional dependency: reinstall without --omit=optional / --no-optional, or build from source:\n" +
        "https://github.com/cyythanapong-netizen/paperboat",
    );
    process.exit(1);
  }
}

const run = spawnSync(bin, process.argv.slice(2), { stdio: "inherit" });
if (run.error) {
  console.error(`paperboat: cannot run ${bin}: ${run.error.message}`);
  process.exit(1);
}
process.exit(run.status ?? 128 + (os.constants.signals[run.signal] ?? 1));
