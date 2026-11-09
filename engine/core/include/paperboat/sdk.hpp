#pragma once

// The Paperboat SDK for C++: the one header a backend includes, and its whole public
// surface. Everything here is stable within a protocol version; headers it doesn't include
// (core/graph.hpp, core/run.hpp internals) may change. Reference: docs/api (C++ API).
//
// A backend is a descriptor (descriptor.toml: which packages, starters, how the Station
// shows) plus code for what only the domain has:
//
//   #include <paperboat/sdk.hpp>
//
//   struct Count : paperboat::Block {                        // a block: outputs from inputs
//     explicit Count(const paperboat::Values& params);
//     paperboat::Values step(const paperboat::Context& ctx, const paperboat::Values& in) override;
//   };
//
//   int main(int argc, char** argv) {
//     static const paperboat::Registry registry = [] {
//       paperboat::Registry r = paperboat::builtins();
//       r.add(paperboat::define_block<Count>("parts", {.type = "count", .label = "Count parts", …}));
//       return r;
//     }();
//     static const auto catalog = paperboat::load_descriptor_file(PAPERBOAT_DESCRIPTOR, registry);
//     return paperboat::main({.engine = "parts", .catalog = &catalog}, argc, argv, 8770);
//   }

#include "paperboat/core/backend.hpp"
#include "paperboat/core/executor.hpp"
#include "paperboat/toolkit/array.hpp"
#include "paperboat/toolkit/bases.hpp"
#include "paperboat/toolkit/block.hpp"
#include "paperboat/toolkit/descriptor.hpp"
#include "paperboat/toolkit/expression.hpp"
#include "paperboat/toolkit/model.hpp"
#include "paperboat/toolkit/registry.hpp"
#include "paperboat/toolkit/scene.hpp"
#include "paperboat/toolkit/solve.hpp"
#include "paperboat/toolkit/station.hpp"

namespace paperboat {

// Definitions: what the UI shows and the descriptor refers to.
using toolkit::BlockDef;
using toolkit::Kind;
using toolkit::ObjectDef;
using toolkit::ParamDef;
using toolkit::PortDef;

// What runs: blocks, steps, station objects, the domain's shared state.
using toolkit::Block;
using toolkit::Context;
using toolkit::DomainState;
using toolkit::Entity;
using toolkit::StationObject;
using toolkit::Step;

// Domain code's tools: solvers (paperboat::solve::make("rk4")), a station object that is a
// system of ODEs (Model: give derivatives(), it steps itself), and scene builders
// (paperboat::scene::rect, circle, rod, spring…).
namespace solve = toolkit::solve;
namespace scene = toolkit::scene;
using toolkit::Model;

// Kinds of block to derive from (bases.hpp): a signal f(t), a step over `seconds`.
using toolkit::Generator;
using toolkit::Timed;
using toolkit::generator_def;
using toolkit::timed_def;
using toolkit::Expression;
using toolkit::ExpressionError;

// Values on wires and in params; typed arrays for bulk data (a mesh, a field: array.hpp).
using toolkit::Array;
using toolkit::DType;
using toolkit::Config;
using toolkit::Handle;
using toolkit::KeySet;
using toolkit::List;
using toolkit::Table;
using toolkit::Vector;
using toolkit::VectorComponent;
using toolkit::vector_port;
using toolkit::Value;
using toolkit::Values;
using toolkit::get_handle;
using toolkit::get_if;
using toolkit::get_or;
using toolkit::get_vector;
using toolkit::get_text;
using toolkit::make_handle;

// Registering and loading.
using toolkit::Catalog;
using toolkit::DescriptorError;
using toolkit::DescriptorOptions;
using toolkit::Registry;
using toolkit::StationTemplate;
using toolkit::Template;
using toolkit::builtins;
using toolkit::define_block;
using toolkit::define_blocks;
using toolkit::define_object;
using toolkit::load_descriptor;
using toolkit::load_descriptor_file;

// Serving.
using core::Backend;
using core::RunSettings;
using core::Server;
using core::main;
using core::run_starter;
using core::serve;

// Run styles (executor.hpp): Backend::executor picks one per graph.
using core::Executor;
using core::Run;
using core::clock_executor;
using core::default_executor;
using core::once_executor;
using core::step_executor;

}  // namespace paperboat
