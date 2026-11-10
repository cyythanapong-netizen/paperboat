// The optional opencv package: image stages on synthetic Mats, no picture files needed
// except where the block itself takes one.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "paperboat/toolkit/opencv.hpp"
#include "paperboat/toolkit/registry.hpp"

using namespace paperboat::toolkit;
using Catch::Matchers::WithinAbs;

namespace {

const Registry& registry() {
  static const Registry with_opencv = [] {
    Registry r = builtins();
    opencv::register_opencv(r);
    return r;
  }();
  return with_opencv;
}

std::unique_ptr<Block> make(const std::string& name, Values params = {}, Config config = {}) {
  const Template* t = registry().find(name);
  REQUIRE(t != nullptr);
  return t->make(params, config);
}

cv::Mat image_at(const Values& out) {
  const cv::Mat* image = opencv::image_in(out, "image");
  REQUIRE(image != nullptr);
  return *image;
}

cv::Mat scene() {
  cv::Mat image = cv::Mat::zeros(100, 100, CV_8UC3);
  cv::rectangle(image, {10, 20, 20, 20}, cv::Scalar(255, 255, 255), cv::FILLED);
  cv::rectangle(image, {60, 55, 30, 20}, cv::Scalar(255, 255, 255), cv::FILLED);
  return image;
}

std::filesystem::path temp_file(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("paperboat-opencv-" + name);
}

}  // namespace

TEST_CASE("opencv geometry resizes, crops, flips and rotates") {
  const cv::Mat source = scene();

  const auto resized = make("opencv/resize", {{"width", 40.0}, {"height", 0.0}, {"scale", 1.0}, {"interpolation", 0.0}});
  CHECK(image_at(resized->step(Context{}, {{"image", opencv::image_out(source)}})).size() == cv::Size(40, 40));

  const auto cropped = make("opencv/crop", {{"x", 10.0}, {"y", 20.0}, {"width", 20.0}, {"height", 20.0}});
  CHECK(image_at(cropped->step(Context{}, {{"image", opencv::image_out(source)}})).size() == cv::Size(20, 20));

  const auto flipped = make("opencv/flip", {{"mode", 1.0}});
  CHECK(image_at(flipped->step(Context{}, {{"image", opencv::image_out(source)}})).size() == source.size());

  const auto rotated = make("opencv/rotate", {{"angle", 90.0}, {"scale", 1.0}, {"expand", 1.0}});
  CHECK(image_at(rotated->step(Context{}, {{"image", opencv::image_out(source)}})).size() == cv::Size(100, 100));
}

TEST_CASE("opencv colour and tone stages keep an 8-bit image") {
  const cv::Mat source = scene();
  for (const char* name : {"opencv/gray", "opencv/convert", "opencv/normalize", "opencv/equalize", "opencv/clahe",
                           "opencv/sobel", "opencv/laplacian"}) {
    const auto block = make(name);
    const cv::Mat out = image_at(block->step(Context{}, {{"image", opencv::image_out(source)}}));
    CHECK(out.depth() == CV_8U);
    CHECK(!out.empty());
  }
  const auto gray = make("opencv/gray");
  CHECK(image_at(gray->step(Context{}, {{"image", opencv::image_out(source)}})).channels() == 1);
  const auto hsv = make("opencv/convert", {{"space", 3.0}});
  CHECK(image_at(hsv->step(Context{}, {{"image", opencv::image_out(source)}})).channels() == 3);
}

TEST_CASE("opencv threshold and contours count the white blobs") {
  const cv::Mat source = scene();
  const auto threshold = make("opencv/threshold", {{"level", 128.0}, {"otsu", 0.0}, {"invert", 0.0}});
  const cv::Mat mask = image_at(threshold->step(Context{}, {{"image", opencv::image_out(source)}}));

  const auto contours = make("opencv/contours", {{"min_area", 50.0}, {"max_area", 0.0}, {"draw", 0.0}, {"thickness", 1.0}});
  const Values out = contours->step(Context{}, {{"image", opencv::image_out(mask)}});
  CHECK(get_or(out, "count") == 2.0);
  CHECK(get_or(out, "found") == 1.0);
  CHECK(get_or(out, "width") == 30.0);
  CHECK(get_or(out, "height") == 20.0);
  CHECK_THAT(get_or(out, "u"), WithinAbs(75.0, 1.0));
  CHECK_THAT(get_or(out, "v"), WithinAbs(65.0, 1.0));
}

TEST_CASE("opencv stats measure a white square") {
  cv::Mat image = cv::Mat::zeros(100, 100, CV_8U);
  cv::rectangle(image, {20, 20, 20, 20}, cv::Scalar(255), cv::FILLED);
  const auto stats = make("opencv/stats");
  const Values out = stats->step(Context{}, {{"image", opencv::image_out(image)}});
  CHECK(get_or(out, "pixels") == 10000.0);
  CHECK(get_or(out, "nonzero") == 400.0);
  CHECK_THAT(get_or(out, "mean"), WithinAbs(10.2, 1e-9));
  CHECK(get_or(out, "min") == 0.0);
  CHECK(get_or(out, "max") == 255.0);
}

TEST_CASE("opencv combine and annotate work on two images") {
  const cv::Mat source = scene();
  const cv::Mat mask = cv::Mat::zeros(source.size(), CV_8U);
  cv::rectangle(mask, {10, 20, 20, 20}, cv::Scalar(255), cv::FILLED);

  const auto masked = make("opencv/bitwise", {{"op", 0.0}});
  CHECK(image_at(masked->step(Context{}, {{"image", opencv::image_out(source)}, {"image2", opencv::image_out(mask)}}))
            .size() == source.size());

  const auto blended = make("opencv/blend", {{"alpha", 0.5}});
  CHECK(image_at(blended->step(Context{}, {{"image", opencv::image_out(source)}, {"image2", opencv::image_out(source)}}))
            .size() == source.size());

  const auto difference = make("opencv/absdiff");
  CHECK(image_at(difference->step(Context{}, {{"image", opencv::image_out(source)}, {"image2", opencv::image_out(source)}}))
            .size() == source.size());

  const auto draw = make("opencv/draw", {{"shape", 0.0}, {"color", 0.0}, {"thickness", 2.0}, {"scale", 0.5}},
                         {{"text", "part"}});
  CHECK(image_at(draw->step(Context{}, {{"image", opencv::image_out(source)}, {"u", 50.0}, {"v", 50.0},
                                       {"width", 20.0}, {"height", 20.0}}))
            .size() == source.size());
}

TEST_CASE("opencv hough lines and circles find a drawn shape") {
  cv::Mat lines = cv::Mat::zeros(100, 100, CV_8U);
  cv::line(lines, {10, 50}, {90, 50}, cv::Scalar(255), 1);
  const auto hough_lines = make("opencv/hough_lines", {{"threshold", 30.0}, {"min_length", 30.0}, {"max_gap", 5.0},
                                                       {"thickness", 1.0}});
  const Values line_out = hough_lines->step(Context{}, {{"image", opencv::image_out(lines)}});
  CHECK(get_or(line_out, "count") >= 1.0);
  CHECK(get_or(line_out, "length") > 70.0);

  cv::Mat circle = cv::Mat::zeros(100, 100, CV_8U);
  cv::circle(circle, {50, 50}, 20, cv::Scalar(255), 1);
  const auto hough_circles = make("opencv/hough_circles", {{"dp", 1.2}, {"min_dist", 30.0}, {"param1", 100.0},
                                                           {"param2", 20.0}, {"min_radius", 10.0},
                                                           {"max_radius", 30.0}, {"thickness", 1.0}});
  const Values circle_out = hough_circles->step(Context{}, {{"image", opencv::image_out(circle)}});
  CHECK(get_or(circle_out, "count") >= 1.0);
  CHECK(get_or(circle_out, "radius") > 15.0);
}

TEST_CASE("opencv template_match finds a template file") {
  cv::Mat template_image = cv::Mat::zeros(20, 20, CV_8U);
  cv::rectangle(template_image, {0, 0, 10, 10}, cv::Scalar(255), cv::FILLED);
  const auto path = temp_file("template.png");
  REQUIRE(cv::imwrite(path.string(), template_image));

  cv::Mat image = cv::Mat::zeros(100, 100, CV_8U);
  template_image.copyTo(image(cv::Rect(40, 40, 20, 20)));

  const auto match = make("opencv/template_match", {{"method", 1.0}, {"threshold", 0.8}, {"thickness", 2.0}},
                          {{"template", path.string()}});
  const Values out = match->step(Context{}, {{"image", opencv::image_out(image)}});
  CHECK(get_or(out, "found") == 1.0);
  CHECK(get_or(out, "score") > 0.8);
  CHECK_THAT(get_or(out, "u"), WithinAbs(50.0, 1.0));
  CHECK_THAT(get_or(out, "v"), WithinAbs(50.0, 1.0));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("opencv save writes a file") {
  const cv::Mat source = scene();
  const auto path = temp_file("saved.png");
  const auto save = make("opencv/save", {{"format", 0.0}, {"quality", 90.0}, {"once", 1.0}},
                         {{"file", path.string()}});
  const Values out = save->step(Context{}, {{"image", opencv::image_out(source)}});
  CHECK(get_or(out, "saved") == 1.0);
  CHECK(std::filesystem::is_regular_file(path));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("opencv background learns the first frame and reports a change") {
  const auto background = make("opencv/background", {{"rate", 0.5}, {"threshold", 10.0}, {"open", 0.0}});
  const cv::Mat first = cv::Mat::zeros(100, 100, CV_8U);
  const Values start = background->step(Context{}, {{"image", opencv::image_out(first)}});
  CHECK(get_or(start, "changed") == 0.0);

  cv::Mat second = first.clone();
  cv::rectangle(second, {20, 20, 20, 20}, cv::Scalar(255), cv::FILLED);
  const Values moved = background->step(Context{}, {{"image", opencv::image_out(second)}});
  CHECK(get_or(moved, "changed") > 0.0);
}

TEST_CASE("opencv centroid finds the largest blob") {
  cv::Mat image = cv::Mat::zeros(100, 100, CV_8U);
  cv::rectangle(image, {20, 30, 40, 20}, cv::Scalar(255), cv::FILLED);
  const auto centroid = make("opencv/centroid");
  const Values out = centroid->step(Context{}, {{"image", opencv::image_out(image)}});
  CHECK(get_or(out, "found") == 1.0);
  CHECK_THAT(get_or(out, "u"), WithinAbs(39.5, 1.0));
  CHECK_THAT(get_or(out, "v"), WithinAbs(39.5, 1.0));
  CHECK_THAT(get_or(out, "area"), WithinAbs(741.0, 1.0));
}
