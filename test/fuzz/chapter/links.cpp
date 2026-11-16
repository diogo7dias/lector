// Link stubs for what the chapter build references but this harness does not exercise:
// image decoding and drawing. ImageBlock's constructor and serialize() match lib/ exactly.
#include <FsHelpers.h>
#include <Serialization.h>

#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/ImageDecoderFactory.h"

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

bool ImageBlock::serialize(HalFile& file) {
  serialization::writeString(file, imagePath);
  serialization::writeString(file, srcPath);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  return true;
}

std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}

bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  return FsHelpers::hasJpgExtension(std::string_view(path)) || FsHelpers::hasPngExtension(std::string_view(path));
}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
