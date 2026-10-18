#include "TrustCertificatePrompt.h"

#include <I18n.h>
#include <TrustedPins.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "activities/util/ConfirmationActivity.h"

bool askToTrustRejectedCertificate(Activity& activity, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   std::function<void(bool trusted)> onAnswer) {
  trusted_pins::Rejection rejection;
  if (!trusted_pins::takeRejected(rejection)) return false;

  // The fingerprint first: the body is one line, and it is the part to compare
  // against what the server shows.
  char fingerprint[40];
  trusted_pins::formatFingerprint(rejection.sha, fingerprint, sizeof(fingerprint));
  const std::string body = std::string(fingerprint) + " (" + rejection.host + ")";

  activity.startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_TRUST_CERT_PROMPT), body),
      [rejection, onAnswer = std::move(onAnswer)](const ActivityResult& result) {
        onAnswer(!result.isCancelled && trusted_pins::save(rejection.host, rejection.sha));
      });
  return true;
}
