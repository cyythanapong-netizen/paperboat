# The host pump: libpaperboat_host runs the engine; its threads put one event at a time on a
# queue, `pb_host_next` hands it to this thread, and `pb_host_reply` answers it. Julia is only
# ever entered from here, never from an engine thread.

const TYPES = Dict{String, DataType}()
const INSTANCES = Dict{Int, Any}()
const LOGS = Dict{Int, Vector{Any}}()
const LIBRARY = Ref{Ptr{Cvoid}}(C_NULL)

"""A JSON value as plain Julia (Dicts, Vectors, numbers, text)."""
plain(value) = value
plain(value::JSON3.Object) = Dict{String, Any}(string(k) => plain(v) for (k, v) in pairs(value))
plain(value::JSON3.Array) = [plain(v) for v in value]

"""The host library: `\$PAPERBOAT_HOST_LIB`, else the checkout's build."""
function host_library()
  given = get(ENV, "PAPERBOAT_HOST_LIB", "")
  isempty(given) || return given
  root = normpath(joinpath(@__DIR__, "..", "..", ".."))
  name = "libpaperboat_host." * Libdl.dlext
  for candidate in (joinpath(root, "engine", "build", "host", name),
                    joinpath(root, "engine", "build", "bundle-install", "lib", name))
    isfile(candidate) && return candidate
  end
  throw(ArgumentError("libpaperboat_host not found: build it (cmake --build engine/build --target paperboat_host) " *
                      "or set PAPERBOAT_HOST_LIB"))
end

"""`log(ctx, "text")` sends a Console line with this run's engine."""
function log(ctx::Context, text::AbstractString, level::AbstractString = "info")
  push!(get!(LOGS, ctx.id, Any[]), Dict("level" => level, "text" => string(text)))
  nothing
end

drain_logs(id::Int) = pop!(LOGS, id, Any[])

function construct(type_name::String, params)
  type = get(TYPES, type_name, nothing)
  type === nothing && throw(ArgumentError("the host serves no type \"$type_name\""))
  values = plain(params)
  # The domain type takes its params as keywords, as the Python API does.
  return Base.invokelatest(type; (Symbol(k) => v for (k, v) in values)...)
end

function handle(id::Int, event::String, payload)
  if event == "new"
    instance = construct(string(payload["type"]), payload["params"])
    INSTANCES[id] = instance
    base = string(get(payload, "base", "block"))
    if base == "model" && instance isa Model
      return Dict("state" => collect(Float64, initial(instance)))
    end
    return Dict()
  end

  instance = get(INSTANCES, id, nothing)
  instance === nothing && throw(ArgumentError("no instance $id (the host called \"$event\" before \"new\")"))
  ctx = Context(id, Float64(get(payload, "t", 0.0)), Float64(get(payload, "dt", 0.0)), "")
  inputs = plain(get(payload, "inputs", Dict{String, Any}()))

  if event == "step"
    if instance isa Step
      step(instance, ctx, inputs)
      return Dict("outputs" => Dict{String, Any}(), "log" => drain_logs(id))
    elseif instance isa StationObject
      step(instance, ctx, inputs)
      return Dict("log" => drain_logs(id))
    else
      return Dict("outputs" => Dict{String, Any}(step(instance, ctx, inputs)), "log" => drain_logs(id))
    end
  elseif event == "value"
    return Dict("value" => value(instance, ctx.t), "log" => drain_logs(id))
  elseif event == "outputs"
    return Dict("outputs" => Dict{String, Any}(outputs(instance)), "log" => drain_logs(id))
  elseif event == "enter"
    enter(instance, ctx, inputs)
    return Dict("log" => drain_logs(id))
  elseif event == "tick"
    return Dict("port" => String(tick(instance, ctx, inputs)), "log" => drain_logs(id))
  elseif event == "may_start"
    return Dict("start" => Bool(may_start(instance)))
  elseif event == "ends_run"
    return Dict("end" => Bool(ends_run(instance)))
  elseif event == "restarts"
    return Dict("restart" => Bool(restarts(instance)))
  elseif event == "shared"
    return Dict("shared" => Bool(shared(instance)))
  elseif event == "read"
    if instance isa Model
      x = haskey(payload, "x") ? collect(Float64, plain(payload["x"])) : Float64[]
      return Dict("outputs" => Dict{String, Any}(read(instance, x)), "log" => drain_logs(id))
    end
    return Dict("outputs" => Dict{String, Any}(read(instance)), "log" => drain_logs(id))
  elseif event == "entities"
    if instance isa StationObject
      return Dict("entities" => [entity_json(e) for e in entities(instance, string(get(payload, "id", "")))])
    end
    return Dict("entities" => [entity_json(e) for e in entities(instance)])
  elseif event == "link"
    link(instance, string(get(payload, "slot", "")))
    return Dict("log" => drain_logs(id))
  elseif event == "derivatives"
    x = collect(Float64, plain(payload["x"]))
    return Dict("dx" => collect(Float64, derivatives(instance, ctx.t, x, inputs)), "log" => drain_logs(id))
  end
  throw(ArgumentError("unknown host event \"$event\""))
end

# An entity as the protocol spells it. Julia's scene builder is `Dict`, so pass it through;
# the engine checks the shapes it knows.
entity_json(entity) = plain(entity)

function config_json(backend::Backend)
  config = Dict{String, Any}(
    "engine" => backend.name,
    "descriptor" => abspath(backend.descriptor),
    "port" => backend.port,
    "mode" => backend.mode,
    "dt" => backend.dt,
    "duration" => backend.duration,
    "emit_every" => backend.emit_every,
    "solver" => backend.solver,
    "state" => backend.state,
    "blocks" => [Paperboat.definition(type) for type in backend.blocks],
    "objects" => [Paperboat.definition(type) for type in backend.objects],
  )
  backend.view === nothing || (config["view"] = backend.view)
  config
end

function register_types(backend::Backend)
  empty!(TYPES)
  empty!(INSTANCES)
  empty!(LOGS)
  for type in backend.blocks
    TYPES[string(Paperboat.definition(type)["type"])] = type
  end
  for type in backend.objects
    TYPES[string(Paperboat.definition(type)["type"])] = type
  end
end

function with_host(backend::Backend, run_only::Bool; lib = host_library())
  register_types(backend)
  LIBRARY[] = Libdl.dlopen(lib, Libdl.RTLD_LAZY | Libdl.RTLD_GLOBAL)
  create = Libdl.dlsym(LIBRARY[], :pb_host_new)
  next_event = Libdl.dlsym(LIBRARY[], :pb_host_next)
  answer = Libdl.dlsym(LIBRARY[], :pb_host_reply)
  free = Libdl.dlsym(LIBRARY[], :pb_host_free)
  last_error = Libdl.dlsym(LIBRARY[], :pb_host_last_error)

  text = JSON3.write(config_json(backend))
  handle_ptr = ccall(create, Ptr{Cvoid}, (Cstring, Cint), text, run_only ? 1 : 0)
  if handle_ptr == C_NULL
    message = unsafe_string(ccall(last_error, Cstring, ()))
    error(isempty(message) ? "paperboat host failed to start" : message)
  end

  try
    while true
      event_pointer = ccall(next_event, Cstring, (Ptr{Cvoid},), handle_ptr)
      event_pointer == C_NULL && break
      event = JSON3.read(unsafe_string(event_pointer))
      result = try
        handle(Int(event["id"]), String(event["event"]), event["payload"])
      catch error
        Dict("error" => sprint(showerror, error))
      end
      ccall(answer, Cint, (Ptr{Cvoid}, Clonglong, Cstring), handle_ptr, Int64(event["seq"]), JSON3.write(result))
    end
  finally
    ccall(free, Cvoid, (Ptr{Cvoid},), handle_ptr)
  end
end

"""Serve a backend on `backend.port` until the process stops (Ctrl+C, or `paperboat stop`)."""
serve(backend::Backend; lib = host_library()) = with_host(backend, false; lib = lib)

"""Run the starter graph once, headless: JSON lines, as `paperboat check` reads them."""
run_starter(backend::Backend; lib = host_library()) = with_host(backend, true; lib = lib)

"""The backend's command line: `--port N` serves (the default), `--run` runs the starter once."""
function main(backend::Backend; lib = host_library())
  port = backend.port
  index = findfirst(==("--port"), ARGS)
  if index !== nothing && index < length(ARGS)
    port = parse(Int, ARGS[index + 1])
  end
  served = Backend(; name = backend.name, descriptor = backend.descriptor, blocks = backend.blocks,
                   objects = backend.objects, port = port, mode = backend.mode, dt = backend.dt,
                   duration = backend.duration, emit_every = backend.emit_every, solver = backend.solver,
                   state = backend.state, view = backend.view)
  "--run" in ARGS ? run_starter(served; lib = lib) : serve(served; lib = lib)
end
