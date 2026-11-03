// The opencv package's blocks: the usual stages of a classic image pipeline. Each takes
// `image` in and gives `image` out; an unwired input gives no output.

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "paperboat/toolkit/opencv.hpp"

namespace paperboat::toolkit::opencv {

void add_image_object(Registry& registry);  // image.cpp

namespace {

int odd(double size) { return std::max(1, static_cast<int>(std::lround(size)) | 1); }
double p(const Values& params, std::string_view id) { return get_or(params, id); }

// A block that turns one image into another with a function.
class Filter final : public Block {
 public:
  using Fn = std::function<cv::Mat(const cv::Mat&, const Values& params)>;
  Filter(Fn fn, Values params) : fn_(std::move(fn)), params_(std::move(params)) {}
  Values step(const Context&, const Values& in) override {
    const cv::Mat* image = image_in(in, "image");
    if (!image) return {};
    return {{"image", image_out(fn_(*image, params_))}};
  }

 private:
  Fn fn_;
  Values params_;
};

void filter(Registry& registry, std::string type, std::string label, std::vector<ParamDef> params, Filter::Fn fn) {
  Template t;
  t.name = "opencv/" + type;
  t.def = {.type = std::move(type), .label = std::move(label), .kind = Kind::Function,
           .inputs = {image_port("image")}, .outputs = {image_port("image")}, .params = std::move(params)};
  t.make = [fn = std::move(fn)](const Values& q, const Config&) { return std::make_unique<Filter>(fn, q); };
  registry.add(std::move(t));
}

cv::Mat morph(const cv::Mat& image, const Values& q, cv::MorphTypes op) {
  const int k = odd(p(q, "size"));
  cv::Mat out;
  cv::morphologyEx(image, out, op, cv::getStructuringElement(cv::MORPH_ELLIPSE, {k, k}), {-1, -1},
                   std::max(1, static_cast<int>(p(q, "iterations"))));
  return out;
}

// Shows its input image on the Dashboard: an image instrument.
struct View final : Block {
  Values step(const Context&, const Values& in) override {
    const cv::Mat* image = image_in(in, "image");
    if (!image) return {};
    return {{"value", data_url(*image)}};
  }
};

}  // namespace

void register_opencv(Registry& registry) {
  add_image_object(registry);

  filter(registry, "gray", "Gray", {}, [](const cv::Mat& im, const Values&) { return as_gray(im); });

  filter(registry, "blur", "Blur (Gaussian)", {{"size", 5.0}}, [](const cv::Mat& im, const Values& q) {
    cv::Mat out;
    const int k = odd(p(q, "size"));
    cv::GaussianBlur(im, out, {k, k}, 0);
    return out;
  });

  filter(registry, "median", "Blur (median)", {{"size", 5.0}}, [](const cv::Mat& im, const Values& q) {
    cv::Mat out;
    cv::medianBlur(im, out, std::max(3, odd(p(q, "size"))));
    return out;
  });

  // Gray → black and white at `level` (0–255). otsu = 1 picks the level from the picture;
  // invert = 1 makes dark things white (parts on a light background).
  filter(registry, "threshold", "Threshold", {{"level", 128.0}, {"otsu", 1.0}, {"invert", 0.0}},
         [](const cv::Mat& im, const Values& q) {
           cv::Mat out;
           const int type = (p(q, "invert") > 0 ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) |
                            (p(q, "otsu") > 0 ? cv::THRESH_OTSU : 0);
           cv::threshold(as_gray(im), out, p(q, "level"), 255, type);
           return out;
         });

  // Threshold against the local mean: for uneven lighting.
  filter(registry, "adaptive_threshold", "Threshold (adaptive)", {{"size", 31.0}, {"c", 5.0}, {"invert", 0.0}},
         [](const cv::Mat& im, const Values& q) {
           cv::Mat out;
           cv::adaptiveThreshold(as_gray(im), out, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                                 p(q, "invert") > 0 ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY,
                                 std::max(3, odd(p(q, "size"))), p(q, "c"));
           return out;
         });

  // A mask of the pixels in an HSV box. Hue is 0–180; h_min > h_max wraps around red.
  filter(registry, "hsv_range", "HSV range",
         {{"h_min", 0.0}, {"h_max", 180.0}, {"s_min", 0.0}, {"s_max", 255.0}, {"v_min", 0.0}, {"v_max", 255.0}},
         [](const cv::Mat& im, const Values& q) {
           cv::Mat hsv;
           cv::Mat bgr = im;
           if (im.channels() == 1) cv::cvtColor(im, bgr, cv::COLOR_GRAY2BGR);
           cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
           auto range = [&](double a, double b) {
             cv::Mat m;
             cv::inRange(hsv, cv::Scalar(a, p(q, "s_min"), p(q, "v_min")), cv::Scalar(b, p(q, "s_max"), p(q, "v_max")), m);
             return m;
           };
           const double h0 = p(q, "h_min"), h1 = p(q, "h_max");
           return h0 <= h1 ? range(h0, h1) : cv::Mat(range(h0, 180) | range(0, h1));
         });

  filter(registry, "canny", "Edges (Canny)", {{"low", 50.0}, {"high", 150.0}}, [](const cv::Mat& im, const Values& q) {
    cv::Mat out;
    cv::Canny(as_gray(im), out, p(q, "low"), p(q, "high"));
    return out;
  });

  for (auto [type, label, op] : {std::tuple{"erode", "Erode", cv::MORPH_ERODE}, {"dilate", "Dilate", cv::MORPH_DILATE},
                                 {"open", "Open (remove specks)", cv::MORPH_OPEN},
                                 {"close", "Close (fill gaps)", cv::MORPH_CLOSE}}) {
    filter(registry, type, label, {{"size", 5.0}, {"iterations", 1.0}},
           [op = op](const cv::Mat& im, const Values& q) { return morph(im, q, op); });
  }

  filter(registry, "invert", "Invert", {}, [](const cv::Mat& im, const Values&) {
    cv::Mat out;
    cv::bitwise_not(im, out);
    return out;
  });

  // How far each white pixel is from black: touching blobs get one peak each.
  filter(registry, "distance", "Distance", {}, [](const cv::Mat& im, const Values&) {
    cv::Mat dist, out;
    cv::distanceTransform(as_gray(im), dist, cv::DIST_L2, 5);
    cv::normalize(dist, out, 0, 255, cv::NORM_MINMAX, CV_8U);
    return out;
  });

  Template view;
  view.name = "opencv/view";
  view.def = {.type = "view", .label = "Image view", .kind = Kind::Monitor, .inputs = {image_port("image")},
              .outputs = {}, .params = {}, .instrument = "image"};
  view.make = [](const Values&, const Config&) { return std::make_unique<View>(); };
  registry.add(std::move(view));
}

}  // namespace paperboat::toolkit::opencv
