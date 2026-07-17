#include "ImageDecoderFactory.h"

#include <Logging.h>

#include <memory>
#include <new>
#include <string>

#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  std::string ext = imagePath;
  size_t dotPos = ext.rfind('.');
  if (dotPos != std::string::npos) {
    ext = ext.substr(dotPos);
    for (auto& c : ext) {
      c = tolower(c);
    }
  } else {
    ext = "";
  }

  if (JpegToFramebufferConverter::supportsFormat(ext)) {
    if (!jpegDecoder) {
      // nothrow: decoders are created lazily on the image path, which often
      // runs on an already-starved heap; bare new would abort() on OOM.
      jpegDecoder.reset(new (std::nothrow) JpegToFramebufferConverter());
      if (!jpegDecoder) {
        LOG_ERR("DEC", "OOM: JPEG decoder");
        return nullptr;
      }
    }
    return jpegDecoder.get();
  } else if (PngToFramebufferConverter::supportsFormat(ext)) {
    if (!pngDecoder) {
      pngDecoder.reset(new (std::nothrow) PngToFramebufferConverter());
      if (!pngDecoder) {
        LOG_ERR("DEC", "OOM: PNG decoder");
        return nullptr;
      }
    }
    return pngDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
