# Stands in for the lib-builder's arduino_tinyusb component in lector's core
# rebuild (custom_sdkconfig). With CONFIG_TINYUSB_ENABLED the Arduino core links a
# component of this name; this one has no sources and forwards to
# espressif/esp_tinyusb (env:x4pro only, via custom_component_add), so the core's
# USB files compile against esp_tinyusb's headers and tusb_config.h. Lector does
# not ship those files (scripts/wire_esp_tinyusb.py); the SDK's UsbMassStorage
# owns the TinyUSB callbacks. Envs without esp_tinyusb get an empty component.
idf_component_register()

# Linked after registration, like the core's maybe_add_component(): requirement
# expansion runs before BUILD_COMPONENTS is known, so a conditional REQUIRES
# could never see esp_tinyusb.
idf_build_get_property(build_components BUILD_COMPONENTS)
if(espressif__esp_tinyusb IN_LIST build_components)
    idf_component_get_property(esp_tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} INTERFACE ${esp_tinyusb_lib})
endif()
