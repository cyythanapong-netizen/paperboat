# The domain's vocabulary: what a block, a step and a station object are, and the JSON their
# `definition` builds for the engine.

"""A signal block: `step(block, ctx, inputs)` returns the outputs by port id."""
abstract type Block end

"""A source block: `value(source, t)` returns its output."""
abstract type Generator <: Block end

"""A block on a Step flow's path: `enter`, then `tick` until it names an output."""
abstract type Step end

"""A station object: properties, a linked block, and what it draws."""
abstract type StationObject end

"""A station object advanced by a solver: `initial(object)` and `derivatives(object, t, x, inputs)`."""
abstract type Model <: StationObject end

"""What a block sees on a step: the run clock, the step size, and its instance id."""
struct Context
  id::Int
  t::Float64
  dt::Float64
  node::String
end

# The methods the engine calls. A domain overrides the ones it uses.
step(::Block, ::Context, inputs) = Dict{String, Any}()
entities(::Block) = Any[]
value(::Generator, t::Float64) = 0.0

enter(::Step, ::Context, inputs) = nothing
tick(::Step, ::Context, inputs) = ""
outputs(::Step) = Dict{String, Any}()
may_start(::Step) = false
ends_run(::Step) = false
restarts(::Step) = false
shared(::Step) = false

read(::StationObject) = Dict{String, Any}()
step(::StationObject, ::Context, inputs) = nothing
entities(::StationObject, id::String) = Any[]
link(::StationObject, slot::String) = nothing

initial(::Model) = Float64[]
derivatives(::Model, t::Float64, x::Vector{Float64}, inputs) = zeros(length(x))

function Base.show(io::IO, block::Block)
  print(io, nameof(typeof(block)), "(", join(["$(name) = $(getfield(block, name))" for name in fieldnames(typeof(block))], ", "), ")")
end

# --- Definitions: what the UI and the engine know about a type -----------------------------

"""A port: `port("in")`, `port("trigger", type = "step")`, `port("force", unit = "N")`."""
function port(id; type = "signal", unit = "", dimension = nothing, components = Any[], broadcast = false)
  definition = Dict{String, Any}("id" => string(id), "type" => type, "unit" => unit)
  dimension === nothing || (definition["dimension"] = dimension)
  isempty(components) || (definition["components"] = components)
  broadcast && (definition["broadcast"] = true)
  definition
end

"""A vector port, optionally with its dimension or component labels."""
vector_port(id; dimension = nothing, components = Any[]) =
  port(id; type = "vector", dimension = dimension, components = components)

"""A list-of-points port."""
points_port(id; coordinates = Any[]) = port(id; type = "points", components = coordinates)

"""A setting: `param("k", 1.0)`, `param("name", ""; type = "text")`, `param("mode", 0; type = "enum", options = ["a", "b"])`."""
function param(id, default; type = "number", text = "", options = String[], unit = "")
  definition = Dict{String, Any}("id" => string(id), "type" => type)
  if type in ("number", "boolean")
    definition["default"] = default
  else
    definition["text"] = string(default)
  end
  text == "" || (definition["text"] = text)
  isempty(options) || (definition["options"] = options)
  unit == "" || (definition["unit"] = unit)
  definition
end

"""The definition of a block type: `block("gain"; label = "Gain", inputs = …, outputs = …)`."""
function block(type; label = "", kind = "function", inputs = Any[], outputs = Any[], params = Any[],
               base = "block", description = "", feedthrough = true, elementwise = false, flows = Any[],
               instrument = "")
  definition = Dict{String, Any}(
    "type" => string(type), "label" => isempty(label) ? string(type) : label, "kind" => kind,
    "inputs" => inputs, "outputs" => outputs, "params" => params, "base" => base,
    "feedthrough" => feedthrough, "elementwise" => elementwise, "description" => description,
  )
  isempty(flows) || (definition["flows"] = flows)
  isempty(instrument) || (definition["instrument"] = instrument)
  definition
end

"""A source block's definition; its output is `y` unless given."""
generator(type; output = "y", kwargs...) = block(type; kind = "source", base = "generator",
                                                 outputs = [port(output)], kwargs...)

"""A step block's definition: its step ports are the path in and out."""
step_block(type; label = "", inputs = Any[], outputs = Any[], params = Any[], kwargs...) =
  block(type; label = label, kind = "function", base = "step", inputs = inputs, outputs = outputs,
        params = params, kwargs...)

"""A station object's definition: `object("cart"; props = …, links = ["rail"])`."""
function object(type; label = "", role = "body", props = Any[], links = Any[], description = "", block = nothing)
  definition = Dict{String, Any}(
    "type" => string(type), "label" => isempty(label) ? string(type) : label, "role" => role,
    "props" => props, "links" => links, "description" => description, "base" => "object",
  )
  block === nothing || (definition["block"] = block)
  definition
end

"""A model's definition: `model("cart"; props = …, solver = "rk4")`."""
function model(type; label = "", props = Any[], links = Any[], description = "", solver = "rk4",
               inputs = Any[], outputs = Any[], block = nothing)
  definition = object(type; label = label, role = "body", props = props, links = links,
                      description = description,
                      block = block === nothing ?
                        Paperboat.block(type; kind = "probe", inputs = inputs, outputs = outputs) : block)
  definition["base"] = "model"
  definition["solver"] = solver
  definition
end

"""The type's definition. A domain defines one for every block and object it serves."""
function definition(::Type{T}) where {T}
  throw(ArgumentError("$(nameof(T)): define `Paperboat.definition(::Type{$nameof(T)})`"))
end

"""A backend: its descriptor, its domain's types and how it runs."""
Base.@kwdef struct Backend
  name::String = "julia"
  descriptor::String
  blocks::Vector{Any} = Any[]
  objects::Vector{Any} = Any[]
  port::Int = 8768
  mode::String = "clock"       # "clock" or "once"
  dt::Float64 = 0.01
  duration::Float64 = 10.0
  emit_every::Int = 4
  solver::String = "rk4"
  state::Bool = false
  view::Any = nothing
end
