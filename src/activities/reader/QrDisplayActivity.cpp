#include "QrDisplayActivity.h"

#include <I18n.h>

UiStatusActivity::StatusView QrDisplayActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_DISPLAY_QR);
  view.qrPayload = textPayload.c_str();
  // As large as the body allows: this is read by a camera, not by the reader.
  view.qrSize = 4096;
  // A dense black block over a differential waveform keeps the old page as
  // speckle inside the code, and a speckled code does not scan.
  view.refresh = HalDisplay::FULL_REFRESH;
  return view;
}
