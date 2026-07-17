#include "ImageToFramebufferDecoder.h"

#include <Logging.h>

bool ImageToFramebufferDecoder::validateImageDimensions(int width, int height, const std::string& format) {
  if (width <= 0 || height <= 0) {
    LOG_ERR("IMG", "Invalid image dimensions (%dx%d %s)", width, height, format.c_str());
    return false;
  }
  // Per-axis cap first: rejects absurd headers cheaply and keeps the pixel
  // product far below any arithmetic limit.
  if (width > MAX_SOURCE_AXIS || height > MAX_SOURCE_AXIS) {
    LOG_ERR("IMG", "Image axis too large (%dx%d %s), max axis: %d", width, height, format.c_str(), MAX_SOURCE_AXIS);
    return false;
  }
  // 64-bit product: int32 overflow here (e.g. 60000x60000) previously wrapped
  // negative and slipped past the pixel cap.
  if (static_cast<int64_t>(width) * static_cast<int64_t>(height) > MAX_SOURCE_PIXELS) {
    LOG_ERR("IMG", "Image too large (%dx%d = %lld pixels %s), max supported: %d pixels", width, height,
            static_cast<long long>(width) * height, format.c_str(), MAX_SOURCE_PIXELS);
    return false;
  }
  return true;
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
