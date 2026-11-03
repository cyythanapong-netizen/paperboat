// Images: loading, converting, and the Image station object.

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "paperboat/toolkit/opencv.hpp"

namespace paperboat::toolkit::opencv {

cv::Mat load(const std::filesystem::path& file) {
  cv::Mat image = cv::imread(file.string(), cv::IMREAD_COLOR);
  if (image.empty()) throw std::runtime_error("cannot read image \"" + file.string() + "\"");
  return image;
}

namespace {

std::string base64(const std::vector<uchar>& bytes) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const unsigned n = (unsigned{bytes[i]} << 16) | (i + 1 < bytes.size() ? unsigned{bytes[i + 1]} << 8 : 0u) |
                       (i + 2 < bytes.size() ? unsigned{bytes[i + 2]} : 0u);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
  }
  return out;
}

// A picture on the Station: the file named by its `file` property, `width` units across.
class ImageObject final : public StationObject {
 public:
  explicit ImageObject(const Values& props)
      : file_(get_text(props, "file")), width_(get_or(props, "width", 6)), image_(load(file_)) {}

  Values read() const override { return {{"image", image_out(image_)}}; }
  void step(const Context&, const Values&) override {}
  std::vector<Entity> entities(const std::string& id, double z) const override {
    auto e = image_entity(id, image_, 0, 0, width_);
    e.pos[2] = z;
    return {e};
  }

 private:
  std::string file_;
  double width_;
  cv::Mat image_;
};

}  // namespace

std::string data_url(const cv::Mat& image) {
  cv::Mat show;
  if (image.depth() != CV_8U) cv::normalize(image, show, 0, 255, cv::NORM_MINMAX, CV_8U);
  else show = image;
  if (show.channels() == 1) cv::cvtColor(show, show, cv::COLOR_GRAY2BGR);
  std::vector<uchar> jpeg;
  cv::imencode(".jpg", show, jpeg, {cv::IMWRITE_JPEG_QUALITY, 85});
  return "data:image/jpeg;base64," + base64(jpeg);
}

cv::Mat as_gray(const cv::Mat& image) {
  if (image.channels() == 1) return image;
  cv::Mat gray;
  cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  return gray;
}

Entity image_entity(std::string id, const cv::Mat& image, double x, double y, double width) {
  const double height = width * image.rows / std::max(image.cols, 1);
  return {.id = std::move(id), .shape = "image", .pos = {x, y, 0}, .size = {width, height, 0}, .src = data_url(image)};
}

void add_image_object(Registry& registry) {
  registry.add(StationTemplate{
      .name = "opencv/image",
      .def = {.type = "image",
              .label = "Image",
              .role = "body",
              .props = {{.id = "file", .default_value = 0, .type = "file", .default_text = ""}, {"width", 6.0}},
              .links = {},
              .block = BlockDef{.type = "image",
                                .label = "Image",
                                .kind = Kind::Probe,
                                .inputs = {},
                                .outputs = {image_port("image")},
                                .params = {}}},
      .make = [](const Values& props) { return std::make_unique<ImageObject>(props); },
  });
}

}  // namespace paperboat::toolkit::opencv
