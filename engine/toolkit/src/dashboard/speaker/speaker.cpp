// dashboard/speaker: plays a numeric audio buffer on the Board.
//
// The backend sends one control step's PCM samples as a numeric vector; the instrument
// queues them through Web Audio. The engine only samples the vector, so this block is a
// Monitor with one `any` input and no outputs.

#include "common.hpp"
#include "dashboard/instruments_shared.hpp"

namespace paperboat::toolkit::dashboard {
namespace {

class Speaker final : public Block {
 public:
  Values step(const Context&, const Values&) override { return {}; }
};

}  // namespace

void add_speaker(Registry& registry) {
  Template t;
  t.name = "dashboard/speaker";
  t.def = {.type = "speaker",
           .label = "Speaker",
           .kind = Kind::Monitor,
           .inputs = {{"in", "any"}},
           .outputs = {},
           .params = {{.id = "rate", .default_value = 44100.0, .unit = "Hz"},
                      {.id = "volume", .default_value = 0.8}},
           .instrument = "speaker",
           .whole_vector = true,
           .description = "Plays a control step's PCM vector through the browser's audio output."};
  t.make = [](const Values&, const Config&) { return std::make_unique<Speaker>(); };
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::dashboard
