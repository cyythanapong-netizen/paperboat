// signal/fft: the magnitude spectrum of the last N samples of a signal.
//
// A clocked block, so it runs once per pass like any signal block. It keeps a ring buffer
// of the signal's last `size` samples; once full it transforms them (pocketfft, real to
// complex) and outputs the amplitudes below Nyquist and the frequency of the strongest bin.
// A block instance owns its buffer, so two FFTs never share samples.

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <numbers>
#include <vector>

#include "common.hpp"
#include "pocketfft_hdronly.h"

namespace paperboat::toolkit::signal {
namespace {

constexpr std::size_t kSizes[] = {64, 128, 256, 512, 1024};

class Fft final : public Block {
 public:
  Fft(const Values& p, const Config&)
      : size_(kSizes[std::clamp(static_cast<int>(get_or(p, "size", 2.0)), 0, 4)]),
        window_(static_cast<int>(get_or(p, "window", 0.0))),
        rate_(get_or(p, "rate", 0.0)),
        samples_(size_, 0.0),
        spectrum_(size_ / 2, 0.0),
        windowed_(size_, 0.0),
        transformed_(size_ / 2 + 1) {}

  Values step(const Context& ctx, const Values& in) override {
    samples_[position_] = get_or(in, "u");
    position_ = (position_ + 1) % size_;
    if (filled_ < size_) ++filled_;
    if (filled_ < size_) return output(ctx);  // not enough samples yet: a quiet spectrum
    transform(ctx);
    return output(ctx);
  }

 private:
  double weight(std::size_t i) const {
    const double n = static_cast<double>(size_);
    const double x = 2.0 * std::numbers::pi * static_cast<double>(i) / (n - 1.0);
    switch (window_) {
      case 1: return 0.5 - 0.5 * std::cos(x);              // Hann
      case 2: return 0.54 - 0.46 * std::cos(x);            // Hamming
      default: return 1.0;                                  // rectangular
    }
  }

  void transform(const Context& ctx) {
    double sum = 0.0;
    for (std::size_t i = 0; i < size_; ++i) {
      const double w = weight(i);
      sum += w;
      windowed_[i] = samples_[(position_ + i) % size_] * w;
    }
    const pocketfft::shape_t shape{size_};
    const pocketfft::stride_t stride_in{sizeof(double)};
    const pocketfft::stride_t stride_out{sizeof(std::complex<double>)};
    pocketfft::r2c<double>(shape, stride_in, stride_out, 0, true, windowed_.data(), transformed_.data(), 1.0);
    const double scale = 2.0 / (sum > 0.0 ? sum : 1.0);
    peak_bin_ = 0;
    for (std::size_t k = 0; k < spectrum_.size(); ++k) {
      const double amplitude = std::abs(transformed_[k]) * (k == 0 ? scale * 0.5 : scale);
      spectrum_[k] = amplitude;
      if (amplitude > spectrum_[peak_bin_]) peak_bin_ = k;
    }
    const double rate = rate_ > 0.0 ? rate_ : (ctx.dt > 0.0 ? 1.0 / ctx.dt : 1.0);
    peak_hz_ = static_cast<double>(peak_bin_) * rate / static_cast<double>(size_);
  }

  Values output(const Context&) const {
    return {{"magnitude", spectrum_}, {"peak_hz", filled_ < size_ ? 0.0 : peak_hz_}};
  }

  std::size_t size_;
  int window_;
  double rate_;
  std::vector<double> samples_;
  std::vector<double> spectrum_;
  std::vector<double> windowed_;
  std::vector<std::complex<double>> transformed_;
  std::size_t position_ = 0;
  std::size_t filled_ = 0;
  std::size_t peak_bin_ = 0;
  double peak_hz_ = 0.0;
};

}  // namespace

void add_fft(Registry& registry) {
  auto t = make_template(
      "signal", "fft", "FFT", {"u"}, {"magnitude", "peak_hz"},
      {{.id = "size", .type = "enum", .default_text = "256", .options = {"64", "128", "256", "512", "1024"}},
       {.id = "window", .type = "enum", .default_text = "none", .options = {"none", "hann", "hamming"}},
       {.id = "rate", .default_value = 0.0, .type = "number", .unit = "Hz"}},
      [](const Values& p, const Config& c) { return std::make_unique<Fft>(p, c); },
      "The amplitude spectrum of the last N samples of u (pocketfft), and its strongest "
      "frequency. rate 0 reads the run clock's.",
      {.compact = true, .glyph = "FFT", .value = "{size}"});
  t.def.outputs[0] = numbers_port("magnitude");
  t.def.cacheable = false;  // it buffers samples: a run-once must never reuse its output
  registry.add(std::move(t));
}

}  // namespace paperboat::toolkit::signal
