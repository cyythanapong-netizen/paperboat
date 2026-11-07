#pragma once

// Squero Studio OM's server (#160 contracts §5): protocol squero/0 over WebSocket, separate
// from paperboat/0. One omc for the process, shared by every client:
//
//   client → {"id": 1, "op": "omc", "expr": "getClassNames()"}
//   server → {"id": 1, "ok": true, "result": "{Modelica}"} | {"id": 1, "ok": false, "error": "…"}
//   server → {"op": "status", "state": "ready" | "checking" | "failed", "version": "…"}  (on
//            connect and on every change)
//
// Expressions pass only through allowed() (no system(), no runScript). Plain HTTP on the
// same port serves the built Studio app from `app` when given.

#include <filesystem>
#include <string>

#include "paperboat/modelica/omc.hpp"

namespace paperboat::modelica {

// One squero/0 message handled; returns the reply (empty for a message that wants none).
std::string handle(Omc& omc, const std::string& message);

// Serves until killed. Returns 1 when the port can't be taken.
int serve_squero(int port, const std::filesystem::path& app = {}, Omc::Options options = {});

}  // namespace paperboat::modelica
