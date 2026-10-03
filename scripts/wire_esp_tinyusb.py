"""
PlatformIO pre-build script (env:x4pro): hand esp_tinyusb to the Arduino compile.

Why this exists
---------------
USB Drive runs on espressif/esp_tinyusb, added with `custom_component_add`.
pioarduino builds an added component inside the custom_sdkconfig rebuild and
copies its `.a` into framework-arduinoespressif32-libs, but the Arduino compile
that follows reads `pioarduino-build.py`, which ships prebuilt and is never
regenerated (the same gap scripts/patch_tickless_linker.py covers for
sections.ld). So that compile has neither esp_tinyusb's headers nor its libs,
and still carries the prebuilt `arduino_tinyusb` (TinyUSB built against a
different tusb_config.h).

What it does
------------
Appends esp_tinyusb's and TinyUSB's include dirs, CFG_TUSB_MCU (a public
compile option of the tinyusb component), and both libs. A pre script runs
before pioarduino-build.py appends its own lists, so these entries come first:
tusb.h and tusb_config.h resolve to the component's copies, and the linker takes
TinyUSB from libespressif__tinyusb before it reaches libarduino_tinyusb. A
post-link check reads the map and fails the build if any libarduino_tinyusb
object got in anyway.

CFG_TUD_ENDOINT0_SIZE is the lib-builder's spelling, which the core's
esp32-hal-tinyusb.c reads; esp_tinyusb's tusb_config.h spells it
CFG_TUD_ENDPOINT0_SIZE.

The core's own TinyUSB layer (esp32-hal-tinyusb.c, USB.cpp, USBMSC.cpp,
FirmwareMSC.cpp, USBCDC.cpp) is left out of the Arduino compile: lector never
uses it, and it would bring a second tinyusb_driver_install (the link fails on
it), TinyUSB descriptor and mount callbacks, and tud_msc_* callbacks of its own.
The SDK's UsbMassStorage drives esp_tinyusb and owns the MSC callbacks. The
rebuild still compiles those files (components/arduino_tinyusb exists for that),
but it does not ship them: only its IDF libs are copied.

The rebuild itself runs with this environment too (its component compiles
inherit these flags, where they do the same job). It links before the libs are
copied into the framework package, so LIBPATH also names the rebuild's own
output dirs; the Arduino compile deletes those and finds the copies instead.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import sys

if sys.platform.startswith("win"):
    # pioarduino wraps build middlewares on Windows and compiles every source anyway,
    # so the core's TinyUSB files would come back and the link would fail on a
    # duplicate tinyusb_driver_install. Say so instead of a confusing link error.
    sys.stderr.write("ERROR: env:x4pro (USB Drive) cannot be built on a Windows host; use Linux or macOS.\n")
    raise SystemExit(1)

managed = os.path.join(env.subst("$PROJECT_DIR"), "managed_components")
idf_out = os.path.join(env.subst("$BUILD_DIR"), "esp-idf")

env.Append(
    CPPPATH=[
        os.path.join(managed, "espressif__esp_tinyusb", "include"),
        os.path.join(managed, "espressif__tinyusb", "src"),
    ],
    CPPDEFINES=[
        ("CFG_TUSB_MCU", "OPT_MCU_" + env.BoardConfig().get("build.mcu").upper()),
        ("CFG_TUD_ENDOINT0_SIZE", "CFG_TUD_ENDPOINT0_SIZE"),
    ],
    LIBPATH=[
        os.path.join(idf_out, "espressif__esp_tinyusb"),
        os.path.join(idf_out, "espressif__tinyusb"),
    ],
    LIBS=["espressif__esp_tinyusb", "espressif__tinyusb"],
)

for name in ("esp32-hal-tinyusb.c", "USB.cpp", "USBMSC.cpp", "FirmwareMSC.cpp", "USBCDC.cpp"):
    env.AddBuildMiddleware(lambda node: None, "*/cores/esp32/" + name)


def check_one_tinyusb(source, target, env):
    """Fail the build if the prebuilt arduino_tinyusb reached the link.

    It is still on the link line after our libs. Any TinyUSB symbol
    libespressif__tinyusb lacks would be taken from it silently, compiled against
    a different tusb_config.h: struct layouts that disagree at run time.
    """
    map_path = os.path.join(env.subst("$BUILD_DIR"), env.subst("$PROGNAME") + ".map")
    try:
        with open(map_path, "r", errors="replace") as handle:
            text = handle.read()
    except OSError as exc:
        sys.stderr.write("WARNING: TinyUSB link not checked: %s\n" % exc)
        return
    if "libarduino_tinyusb.a(" in text:
        sys.stderr.write(
            "ERROR: %s links objects from libarduino_tinyusb.a. USB Drive must use "
            "esp_tinyusb's TinyUSB only; see scripts/wire_esp_tinyusb.py.\n" % map_path
        )
        raise SystemExit(1)
    print("TinyUSB link: esp_tinyusb only, no libarduino_tinyusb objects")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_one_tinyusb)
