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
//
// The other ops are Studio's own, written here in C++ (Wave 3). Each is {id, op, …} →
// {id, ok: true, result: {…}} | {id, ok: false, error}:
//
//   open      {}                                → {file, model}      the model the server was started on
//   save      {model}                           → {file}             `save(model)`: the model's own .mo
//   icon      {class}                           → {icon, connectors, notice}
//                                                  the imported look of a class (`paperboat modelica icon`)
//   set_icon  {model, icon}                     → {}                 IconDef → Icon(graphics) annotation;
//                                                  the inverse of the importer, `g` and size only
//   generate  {source, model, out, dry?, path?} → {model, path, reason, out, notices, files?}
//                                                  pushes {op: "progress", id, stage, fraction} first, stage
//                                                  check|flatten|generate|build; `files` (relative path →
//                                                  text) only when `dry`, which writes nothing
//   simulate  {model, stop, vars?}              → {t, series, names}  a local check run with omc, 500
//                                                  intervals; series for `vars` (all when omitted),
//                                                  `names` every variable in the result
//
// set_icon and save refuse the Modelica Standard Library: Studio edits only the user's file.

#include <filesystem>
#include <functional>
#include <string>

#include "paperboat/modelica/omc.hpp"

namespace paperboat::modelica {

// What a handler may do besides answer: push a message to its client (progress), and which .mo
// the server was started on.
struct Session {
  std::function<void(const std::string&)> push = {};
  std::filesystem::path file = {};
};

// One squero/0 message handled; returns the reply (empty for a message that wants none).
std::string handle(Omc& omc, const std::string& message, const Session& session = {});

// Serves until killed. Returns 1 when the port can't be taken. `file` is the .mo to open
// (created as a stub model when it doesn't exist); empty = ./Untitled.mo.
int serve_squero(int port, const std::filesystem::path& app = {}, Omc::Options options = {},
                 const std::filesystem::path& file = {});

}  // namespace paperboat::modelica
