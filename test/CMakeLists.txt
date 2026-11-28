cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Host builds with AddressSanitizer + UndefinedBehaviorSanitizer:
#   cmake -S test -B build/test-asan -DLECTOR_SANITIZE=ON
option(LECTOR_SANITIZE "Build host tests with ASan and UBSan" OFF)
if(LECTOR_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  # Firmware code builds -fno-rtti while gtest has RTTI: vptr checks false-positive across them.
  add_compile_options(-fno-sanitize=vptr)
  add_link_options(-fsanitize=address,undefined)
endif()

# libFuzzer harnesses for the parsers that read untrusted input (clang only), see fuzz/README.md:
#   CC=clang CXX=clang++ cmake -S test -B build/fuzz -DLECTOR_FUZZ=ON
option(LECTOR_FUZZ "Build libFuzzer harnesses in test/fuzz" OFF)

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# JPEGDEC at the firmware pin with scripts/jpegdec_patches and the device's 32-bit bit reader,
# for jpeg_decode_accuracy and fuzz/image.
file(GLOB JPEGDEC_PATCHES ${REPO_ROOT}/scripts/jpegdec_patches/*.patch)
list(SORT JPEGDEC_PATCHES)
FetchContent_Declare(jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b  # as platformio.ini
  PATCH_COMMAND git checkout -- src
    COMMAND git apply ${JPEGDEC_PATCHES} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/image/jpegdec-device-register-width.patch
  UPDATE_DISCONNECTED ON
  SOURCE_SUBDIR no-cmake
)

# ArduinoJson at the firmware pin, headers only, for opds_server_store.
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2  # as platformio.ini
  SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(arduinojson)

# AnimatedGIF at the firmware pin with scripts/animatedgif_patches, the device's 32-bit register
# width and its MAX_WIDTH, for gif_support and fuzz/image.
file(GLOB ANIMATEDGIF_PATCHES ${REPO_ROOT}/scripts/animatedgif_patches/*.patch)
list(SORT ANIMATEDGIF_PATCHES)
FetchContent_Declare(animatedgif
  GIT_REPOSITORY https://github.com/bitbank2/AnimatedGIF.git
  GIT_TAG 67cfacafe12ef4926c7713342c57d2a4af040e5e  # as platformio.ini
  PATCH_COMMAND git checkout -- src
    COMMAND git apply ${ANIMATEDGIF_PATCHES} ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/image/animatedgif-device-register-width.patch
  UPDATE_DISCONNECTED ON
  SOURCE_SUBDIR no-cmake
)

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(font_manifest_parser)
add_subdirectory(shrink_to_fit)
add_subdirectory(ttf_family_scan)
add_subdirectory(sd_card_font)
add_subdirectory(differential_rounding)
add_subdirectory(kern_sparse)
add_subdirectory(utf8_compose)
add_subdirectory(text_encoding)
add_subdirectory(fs_helpers)
add_subdirectory(protected_paths)
add_subdirectory(progress_comparison)
add_subdirectory(koreader_document_id)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(link_back_stack)
add_subdirectory(back_trail)
add_subdirectory(silent_restart)
add_subdirectory(opds_filename)
add_subdirectory(opds_parser)
add_subdirectory(credential_bundle)
add_subdirectory(http_header)
add_subdirectory(opds_server_store)
add_subdirectory(opds_auth)
add_subdirectory(opds_token_store)
add_subdirectory(html_to_plain_text)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(reader_look_fields)
add_subdirectory(reader_prefs)
add_subdirectory(render_spec)
add_subdirectory(margin_link)
add_subdirectory(viewable_insets)
add_subdirectory(guide_dots_mode)
add_subdirectory(firmware_version)
add_subdirectory(firmware_flash_write)
add_subdirectory(cert_pin)
add_subdirectory(trusted_pins)
add_subdirectory(paragraph_number_layout)
add_subdirectory(string_utils)
add_subdirectory(fnv1a)
add_subdirectory(builtin_sizes)
add_subdirectory(battery_percent_poll)
add_subdirectory(bookorbit_missing)
add_subdirectory(clock_zones)
add_subdirectory(quote_text)
add_subdirectory(quote_underline)
add_subdirectory(ruled_lines)
add_subdirectory(ws_idle_clients)
add_subdirectory(keyboard_field)
add_subdirectory(keyboard_touch)
add_subdirectory(selection_style)
add_subdirectory(sleep_favorites)
add_subdirectory(sleep_rotation_policy)
add_subdirectory(sleep_queue_policy)
add_subdirectory(sleep_index_reconcile)
add_subdirectory(sleep_index_record)
add_subdirectory(sleep_folder_policy)
add_subdirectory(sleep_face_paint)
add_subdirectory(hold_button)
add_subdirectory(book_filing)
add_subdirectory(library_index)
add_subdirectory(library_search)
add_subdirectory(reader_preset_migration)
add_subdirectory(reader_presets)
add_subdirectory(favorite_queue_chain)
add_subdirectory(browser_row)
add_subdirectory(name_list)
add_subdirectory(credential_integrity)
add_subdirectory(css_parser)
add_subdirectory(dictionary)
add_subdirectory(idx_cursor)
add_subdirectory(comparison_layout)
add_subdirectory(double_click_detector)
add_subdirectory(settings_list_nav)
add_subdirectory(release_storage)
add_subdirectory(reader_return_history)
add_subdirectory(statusbar_reflow)
add_subdirectory(chapter_wipe)
add_subdirectory(reader_toolbar)
add_subdirectory(wifi_session)
add_subdirectory(wrapped_list_window)
add_subdirectory(wake_face_policy)
add_subdirectory(wake_sequence)
add_subdirectory(x3_panel_driver)
add_subdirectory(wake_classify)
add_subdirectory(low_battery_policy)
add_subdirectory(book_badge_label)
add_subdirectory(reading_percent)
add_subdirectory(recent_words)
add_subdirectory(nearby_position_sync)
add_subdirectory(nearby_file_transfer)
add_subdirectory(http_range_policy)
add_subdirectory(ota_boot_entry)
add_subdirectory(ota_retry_policy)
add_subdirectory(tls_heap_policy)
add_subdirectory(tls_loan_audit)
add_subdirectory(tls_scratch_layout)
add_subdirectory(opds_retry_policy)
add_subdirectory(fetch_url_policy)
add_subdirectory(trusted_roots)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(reader_progress_save_debouncer)
add_subdirectory(serialization_bounds)
add_subdirectory(spine_file_name_index)
add_subdirectory(table_span)
add_subdirectory(hold_repeat)
add_subdirectory(release_gate)
add_subdirectory(touch_frame_routing)
add_subdirectory(touch_metrics)
add_subdirectory(hint_band)
add_subdirectory(row_hit_test)
add_subdirectory(row_marks)
add_subdirectory(reader_touch)
add_subdirectory(list_swipe)
add_subdirectory(frontlight_boot_policy)
add_subdirectory(header_title)
add_subdirectory(list_scrollbar)
add_subdirectory(light_panel)
add_subdirectory(option_popup_paging)
add_subdirectory(orientation_cycle)
add_subdirectory(settings_grid)
add_subdirectory(two_tap_gate)
add_subdirectory(touch_act_once)
add_subdirectory(status_stack)
add_subdirectory(status_screens)
add_subdirectory(list_screens)
add_subdirectory(list_chrome)
add_subdirectory(grid_nav)
add_subdirectory(grid_screens)
add_subdirectory(device_look)
add_subdirectory(slider_field)
add_subdirectory(signal_meter)
add_subdirectory(ui_row_height)
add_subdirectory(ui_row_wrap)
add_subdirectory(row_slider)
add_subdirectory(adjust_strip)
add_subdirectory(button_gestures)
add_subdirectory(bound_action_scope)
add_subdirectory(sleep_timeout_guard)
add_subdirectory(button_router)
add_subdirectory(button_replay)
add_subdirectory(queued_turns)
add_subdirectory(reader_landing)
add_subdirectory(font_cache_manager)
add_subdirectory(activity_busy_label)
add_subdirectory(anchor_placement)
add_subdirectory(list_hold_nav)
add_subdirectory(firmware_retry_flow)
add_subdirectory(firmware_board_tag)
add_subdirectory(keys_only_look)
add_subdirectory(inflate_reader)
add_subdirectory(symbol_table)

add_subdirectory(serialized_value_guard)
add_subdirectory(button_mapping)
add_subdirectory(section_cache_validity)
add_subdirectory(idle_prewarm_neighbour)
add_subdirectory(watermark_build_bail)

add_subdirectory(collapsible_sections)
add_subdirectory(ligature_guard)
add_subdirectory(space_fallback)
add_subdirectory(word_spacing)
add_subdirectory(blockquote_inset)
add_subdirectory(embedded_style_mono)
add_subdirectory(keep_headings)
add_subdirectory(better_tables)
add_subdirectory(txt_justify)
add_subdirectory(image_fallback_cache)
add_subdirectory(chapter_read_error)
add_subdirectory(text_block_fonts)
add_subdirectory(book_metadata_cache)
add_subdirectory(zip_file)
add_subdirectory(xtc_parser)
add_subdirectory(sortes)
add_subdirectory(glyph_bitmap)
add_subdirectory(scale_to_fit)
add_subdirectory(dither_utils)
add_subdirectory(gif_support)
add_subdirectory(usb_drive_storage_guard)
add_subdirectory(bitmap_resample)
add_subdirectory(jpeg_decode_accuracy)
add_subdirectory(pinch_font_resize)
add_subdirectory(reader_font_swap_lock)
add_subdirectory(chapter_build_safety)
add_subdirectory(article_epub)
add_subdirectory(page_jump)
add_subdirectory(power_clock_policy)
add_subdirectory(wifi_radio_off_on_cancel)
add_subdirectory(panel_power_off)
add_subdirectory(auto_sync_join)
add_subdirectory(web_power_save)
add_subdirectory(idle_prewarm_timing)
add_subdirectory(busy_wait_isr)
add_subdirectory(footnote_popup)

if(LECTOR_FUZZ)
  add_subdirectory(fuzz)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME SharedBuiltinFontTables
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/scripts/test_shared_builtin_font_tables.py)
endif()
