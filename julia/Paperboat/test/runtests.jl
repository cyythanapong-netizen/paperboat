using Test
using JSON3
using Paperboat

struct Gain <: Paperboat.Block
  k::Float64
end
Gain(; k = 1.0) = Gain(k)

Paperboat.definition(::Type{Gain}) = Paperboat.block(
  "gain";
  label = "Gain",
  inputs = [Paperboat.port("in")],
  outputs = [Paperboat.port("out")],
  params = [Paperboat.param("k", 1.0)],
)

Paperboat.step(block::Gain, ctx::Paperboat.Context, inputs) = Dict("out" => block.k * get(inputs, "in", 0.0))

struct Cart <: Paperboat.Model
  mass::Float64
end
Cart(; mass = 1.0) = Cart(mass)

Paperboat.definition(::Type{Cart}) = Paperboat.model(
  "cart";
  label = "Cart",
  props = [Paperboat.param("mass", 1.0)],
  inputs = [Paperboat.port("force", unit = "N")],
  outputs = [Paperboat.port("x", unit = "m"), Paperboat.port("v", unit = "m/s")],
)

Paperboat.initial(cart::Cart) = [0.0, 0.0]
Paperboat.derivatives(cart::Cart, t::Float64, x::Vector{Float64}, inputs) = [x[2], get(inputs, "force", 0.0) / cart.mass]
# A model reports the state its solver holds; the engine hands it to `read(model, x)`.
Paperboat.read(cart::Cart, x::Vector{Float64}) = Dict("x" => x[1], "v" => x[2])

@testset "definitions" begin
  gain = Paperboat.definition(Gain)
  @test gain["type"] == "gain"
  @test gain["kind"] == "function"
  @test gain["inputs"][1]["id"] == "in"
  @test gain["params"][1]["default"] == 1.0
  @test JSON3.read(JSON3.write(gain))["type"] == "gain"

  cart = Paperboat.definition(Cart)
  @test cart["base"] == "model"
  @test cart["solver"] == "rk4"
  @test cart["block"]["outputs"][2]["id"] == "v"

  @test_throws ArgumentError Paperboat.definition(Int)
end

@testset "blocks run" begin
  block = Gain(; k = 3.0)
  ctx = Paperboat.Context(1, 0.0, 0.01, "")
  @test Paperboat.step(block, ctx, Dict("in" => 2.0))["out"] == 6.0
  @test Paperboat.step(block, ctx, Dict{String, Any}())["out"] == 0.0
end

@testset "models integrate" begin
  cart = Cart(; mass = 2.0)
  @test Paperboat.initial(cart) == [0.0, 0.0]
  @test Paperboat.derivatives(cart, 0.0, [1.0, 0.5], Dict("force" => 4.0)) == [0.5, 2.0]
  @test Paperboat.read(cart, [1.0, 0.5]) == Dict("x" => 1.0, "v" => 0.5)
end

@testset "json to plain Julia" begin
  value = JSON3.read("{\"a\": [1, 2], \"b\": {\"c\": 3}}")
  plain = Paperboat.plain(value)
  @test plain["a"] == [1, 2]
  @test plain["b"]["c"] == 3
end

@testset "the host library" begin
  lib = try
    Paperboat.host_library()
  catch
    ""
  end
  if isempty(lib)
    @test_skip "build the engine first: cmake --build engine/build --target paperboat_host"
  else
    @test isfile(lib)
  end
end
