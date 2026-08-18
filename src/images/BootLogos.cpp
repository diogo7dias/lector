#include "images/BootLogos.h"

#include <esp_random.h>

#include "images/bootlogo11.h"
#include "images/bootlogo12.h"
#include "images/bootlogo13.h"
#include "images/bootlogo15.h"
#include "images/bootlogo17.h"
#include "images/bootlogo19.h"

namespace bootlogos {
const uint8_t* const kAll[] = {BootLogo11, BootLogo12, BootLogo13, BootLogo15, BootLogo17, BootLogo19};
const int kCount = sizeof(kAll) / sizeof(kAll[0]);

const uint8_t* byIndex(const uint8_t index) { return kAll[index % kCount]; }

uint8_t randomIndex() { return static_cast<uint8_t>(esp_random() % kCount); }
}  // namespace bootlogos
