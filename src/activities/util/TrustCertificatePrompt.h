#pragma once

#include <functional>

class Activity;
class GfxRenderer;
class MappedInputManager;

// After a request failed on a certificate nothing vouches for (trusted_pins::noteIfRejected):
// shows its fingerprint and asks whether to trust it. Returns false when there is no
// rejection to ask about. onAnswer(true) runs once the pin is saved, so the caller can
// retry; onAnswer(false) on Cancel or a failed save.
bool askToTrustRejectedCertificate(Activity& activity, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   std::function<void(bool trusted)> onAnswer);
