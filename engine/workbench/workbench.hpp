#pragma once

// The workbench: a backend with the standard toolkits and no domain. Flow and Dashboard
// work as they are (a Simulink-style workbench); the Station page has nothing to show.
// It listens on the engine's default port (8765), so the web app connects to it as it is.

#include "paperboat/core/backend.hpp"
#include "paperboat/toolkit/descriptor.hpp"

namespace paperboat::workbench {

// The workbench's catalog: base, math, signal, control and dashboard, with no domain
// blocks. Static, because catalog entries point into it for as long as the server runs.
const toolkit::Catalog& catalog();

// The backend that serves the catalog. Static too, for the same reason.
const core::Backend& backend();

}  // namespace paperboat::workbench
