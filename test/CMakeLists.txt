cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

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

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
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
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(statusbar_reflow)
add_subdirectory(dither_memory)
add_subdirectory(serialization_bounds)
add_subdirectory(fixed_buffer)
add_subdirectory(memory_bounds)
add_subdirectory(large_folder_index_policy)
add_subdirectory(visible_entry_map)
add_subdirectory(wallpaper_direct_pick_policy)
add_subdirectory(sleep_favorite_move)
add_subdirectory(sleep_index_pick_policy)
add_subdirectory(sleep_rotation_policy)
add_subdirectory(list_window_policy)
add_subdirectory(wake_frame_handoff)
add_subdirectory(pxc_plane_packer)
add_subdirectory(whole_file_read_gate)
add_subdirectory(image_viewer_patch)
add_subdirectory(image_viewer_policy)
add_subdirectory(font_upload_policy)
add_subdirectory(display_refresh_policy)
add_subdirectory(font_preview_layout)
add_subdirectory(popup_refresh_mode)
add_subdirectory(button_response_policy)
add_subdirectory(pxc_overlay_timing)
add_subdirectory(reading_stats)
add_subdirectory(stats_dashboard_policy)
add_subdirectory(layout_heap_gate)
add_subdirectory(low_memory_render_tier)
add_subdirectory(arena)
add_subdirectory(wallpaper_rotation_cycle)
add_subdirectory(opds_feeds)
add_subdirectory(parsed_text_layout)
add_subdirectory(image_dimension_policy)
add_subdirectory(serialization_read_result)
add_subdirectory(buffered_file)
add_subdirectory(build_scratch)
add_subdirectory(inflate_stream)
add_subdirectory(make_shared_nothrow)
add_subdirectory(orphan_cache_policy)
