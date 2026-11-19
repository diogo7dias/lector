#pragma once

#include "ImageToFramebufferDecoder.h"

// First frame of a non-interlaced GIF (AnimatedGIF), drawn like a PNG: scaled to the box,
// dithered to four levels, streamed to the .pxc cache. Crop and alpha-keep fields of
// RenderConfig are not used: chapters never set them for a GIF, and a transparent pixel
// is composited onto white.
class GifToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  static bool getDimensionsStatic(const std::string& imagePath, ImageDimensions& out);

  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) override;

  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override {
    return getDimensionsStatic(imagePath, dims);
  }

  static bool supportsFormat(const std::string& extension);
  const char* getFormatName() const override { return "GIF"; }
};
