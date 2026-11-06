#pragma once

// The opencv package: classic image processing as blocks, images on the wires.
//
// - Station: an Image station object (a picture file, set on the Station page). Its linked
//   block puts the picture on the Flow page: `image` out.
// - Flow: blocks with `image` ports (gray, blur, threshold, morphology, edges…) evaluated
//   in dependency order, usually once.
// - Dashboard: Image view shows any image (a stage, a result) as an instrument.
// - Your own blocks take an image in with image_in() and send one out with image_out(),
//   like the parts example's counter.
//
// Optional: built only when OpenCV is found. A backend registers it next to the builtins
// and imports "opencv" in its descriptor:
//
//   tk::Registry r = tk::builtins();
//   tk::opencv::register_opencv(r);

#include <filesystem>
#include <string>
#include <string_view>

#include <opencv2/core.hpp>

#include "paperboat/toolkit/registry.hpp"

namespace paperboat::toolkit::opencv {

inline constexpr std::string_view kImage = "image";  // the handle kind and port type

// The image at input `port`, or null if it's unwired or not an image.
inline const cv::Mat* image_in(const Values& in, std::string_view port) { return get_handle<cv::Mat>(in, port, kImage); }
inline Value image_out(cv::Mat image) { return make_handle(std::string(kImage), std::move(image)); }
inline PortDef image_port(std::string id) { return {std::move(id), std::string(kImage)}; }

cv::Mat load(const std::filesystem::path& file);  // throws if it can't be read
std::string data_url(const cv::Mat& image);       // a JPEG data: URL, for an image entity
cv::Mat as_gray(const cv::Mat& image);            // one channel, converting if needed
// An `image` entity of `image`, `width` scene units across, centred on (x, y).
Entity image_entity(std::string id, const cv::Mat& image, double x, double y, double width);

void register_opencv(Registry& registry);

}  // namespace paperboat::toolkit::opencv
