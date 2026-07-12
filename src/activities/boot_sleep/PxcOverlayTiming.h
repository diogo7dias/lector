#pragma once

enum class PxcOverlayTiming { EveryPass, FinalComposite };
enum class PxcOverlayStage { Base, Lsb, Msb };

constexpr PxcOverlayTiming pxcViewerOverlayTiming() { return PxcOverlayTiming::EveryPass; }

constexpr bool shouldDrawPxcOverlay(const PxcOverlayTiming timing, const PxcOverlayStage stage, const bool grayscale) {
  return timing == PxcOverlayTiming::EveryPass || !grayscale || stage != PxcOverlayStage::Base;
}

constexpr bool shouldForceBwPxcOverlay(const PxcOverlayTiming timing, const PxcOverlayStage stage,
                                       const bool grayscale) {
  return grayscale && timing == PxcOverlayTiming::FinalComposite && stage != PxcOverlayStage::Base;
}
