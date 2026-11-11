"""Paperboat: write a backend's domain code in Julia on the C++ engine.

The engine does everything a backend does: it loads the descriptor, checks and runs graphs,
runs the toolkit's blocks and serves the protocol. Julia adds only the domain's own code, as
subtypes of the types below; `definition` says what the UI shows, and the methods are what the
engine calls:

    struct Gain <: Paperboat.Block
        k::Float64
    end
    Gain(; k = 1.0) = Gain(k)

    Paperboat.definition(::Type{Gain}) = Paperboat.block(
        "gain"; label = "Gain",
        inputs = [Paperboat.port("in")], outputs = [Paperboat.port("out")],
        params = [Paperboat.param("k", 1.0)])

    Paperboat.step(g::Gain, ctx, inputs) = Dict("out" => g.k * get(inputs, "in", 0.0))

    Paperboat.serve(Paperboat.Backend(descriptor = "descriptor.toml", blocks = [Gain]))

`serve` hands a JSON description of the types to `libpaperboat_host`, which serves until the
process stops and asks Julia for each step. Nothing here draws, solves or speaks the protocol.
"""
module Paperboat

using JSON3
using Libdl

export Block, Generator, Step, StationObject, Model, Backend, Context
export serve, run_starter, main, definition, block, generator, step_block, object, model
export port, param, vector_port, points_port
export step, value, enter, tick, outputs, may_start, ends_run, restarts, shared
export read, entities, link, initial, derivatives, log

include("api.jl")
include("host.jl")

end
