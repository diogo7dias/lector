#include "images/BootLogos.h"

#include "images/bootlogo0.h"
#include "images/bootlogo1.h"
#include "images/bootlogo10.h"
#include "images/bootlogo11.h"
#include "images/bootlogo12.h"
#include "images/bootlogo13.h"
#include "images/bootlogo14.h"
#include "images/bootlogo15.h"
#include "images/bootlogo16.h"
#include "images/bootlogo17.h"
#include "images/bootlogo19.h"
#include "images/bootlogo2.h"
#include "images/bootlogo21.h"
#include "images/bootlogo22.h"
#include "images/bootlogo23.h"
#include "images/bootlogo24.h"
#include "images/bootlogo3.h"
#include "images/bootlogo4.h"
#include "images/bootlogo5.h"
#include "images/bootlogo6.h"
#include "images/bootlogo7.h"
#include "images/bootlogo8.h"
#include "images/bootlogo9.h"

namespace bootlogos {
// First 5 (BootLogo0..4) are the branded "READ TILL YOU DIE" skull crests; the
// rest are the extra user logos. Keep the skulls first so kSkullCount indexes them.
const uint8_t* const kAll[] = {BootLogo0,  BootLogo1,  BootLogo2,  BootLogo3,  BootLogo4,  BootLogo5,
                               BootLogo6,  BootLogo7,  BootLogo8,  BootLogo9,  BootLogo10, BootLogo11,
                               BootLogo12, BootLogo13, BootLogo14, BootLogo15, BootLogo16, BootLogo17,
                               BootLogo19, BootLogo21, BootLogo22, BootLogo23, BootLogo24};
const int kCount = sizeof(kAll) / sizeof(kAll[0]);
const int kSkullCount = 5;
}  // namespace bootlogos
