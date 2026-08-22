add_executable(amberedit_tests
  main.cpp
  support/result_test.cpp
  config/cfg_file_test.cpp
  config/text_util_test.cpp
  config/area_pattern_test.cpp
  config/app_config_test.cpp
  config/temp_dir_test.cpp
  config/fidoconfig_parser_test.cpp
  config/manual_area_source_test.cpp
  config/areas_bbs_parser_test.cpp
  config/squish_cfg_parser_test.cpp
  config/ftn_address_test.cpp
  domain/address_pattern_test.cpp
  domain/area_test.cpp
  domain/message_test.cpp
  encoding/charset_detector_test.cpp
  encoding/iconv_recoder_test.cpp
  encoding/text_search_test.cpp
  msgbase/ftn_msgbase_test.cpp
  msgbase/format_drivers_test.cpp
  msgbase/jam_msgbase_test.cpp
  msgbase/sdm_msgbase_test.cpp
  msgbase/message_info_test.cpp
  msgbase/lastread_store_test.cpp
  msgbase/create_base_test.cpp
  echolist/echolist_parser_test.cpp
  echolist/echolist_db_test.cpp
  echolist/echolist_source_test.cpp
  echolist/echolist_compile_test.cpp
  echolist/echolist_area_source_test.cpp
  nodelist/nodelist_parser_test.cpp
  nodelist/nodelist_db_test.cpp
  nodelist/nodelist_source_test.cpp
  nodelist/nodelist_compile_test.cpp
  app/area_manager_test.cpp
  app/compose_prefill_test.cpp
  app/export_file_test.cpp
  app/import_file_test.cpp
  app/quoting_test.cpp
  app/copy_commands_test.cpp
  app/msg_template_test.cpp
  app/message_builder_test.cpp
  app/navigator_test.cpp
  ui/app_state_test.cpp
  ui/area_list_screen_test.cpp
  ui/area_list_format_test.cpp
  ui/msg_list_format_test.cpp
  ui/message_list_screen_test.cpp
  ui/message_read_screen_test.cpp
  ui/twit_test.cpp
  ui/find_test.cpp
  ui/export_dialog_test.cpp
  ui/hint_bar_test.cpp
  ui/import_dialog_test.cpp
  ui/info_dialog_test.cpp
  ui/error_log_test.cpp
  ui/keys_test.cpp
  ui/nodelist_dialog_test.cpp
  ui/compose_screen_test.cpp
  ui/change_message_test.cpp
  ui/other_area_test.cpp
  ui/carbon_copy_test.cpp
  ui/back_button_test.cpp
  ui/delete_line_button_test.cpp
  ui/menu_dialog_test.cpp
  ui/element_test.cpp
  ui/dialog_frame_test.cpp
  ui/list_page_test.cpp
  ui/wheel_throttle_test.cpp
  ui/quick_search_test.cpp
  ui/theme_test.cpp
  ui/text_layout_test.cpp
  ui/bbs_codes_test.cpp
  ui/text_editor_test.cpp
  ui/edit_layout_test.cpp
)

target_link_libraries(amberedit_tests PRIVATE
  # The nodelist library exports amberedit_core, which is why the core is not
  # named here as well: it would stand on the link line twice.
  amberedit_nodelist
  amberedit_echolist
  # For term::toGlyphs and the width measuring text_layout is built on. Linking
  # the whole terminal layer also drags in ncurses, which the tests never call —
  # cheap enough, and much simpler than splitting the library to avoid it.
  amberedit_term
  doctest::doctest
)
target_include_directories(amberedit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}   # for test_paths.hpp and test_strings.hpp
)

# The tests read fixtures from testdata/. The repository root is passed in as a
# macro so they do not depend on the working directory.
target_compile_definitions(amberedit_tests PRIVATE
  AMBEREDIT_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

# text_layout belongs to the AmberEdit (UI) target rather than amberedit_core, so the
# test needs the source directly.
target_sources(amberedit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/ui/area_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/bbs_codes.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/confirm_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/dialog_frame.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/dir_listing.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/error_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/error_log.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/export_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/export_mode_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/find_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/attributes_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/forward_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/hint_bar.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/import_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/info_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/input_field.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/keys.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/menu_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/nodelist_dialog.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/area_list_format.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/msg_list_format.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/text_layout.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/text_editor.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/edit_layout.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
)

# The area list, and the screens it hands over to — opening an area reaches the
# message list, which reaches the reader and the compose screens. They are one
# chain rather than four choices: linking the first means linking all of them.
# None of them touches the terminal, which is what lets the tests drive them.
target_sources(amberedit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/ui/screens/area_list_screen.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/screens/message_list_screen.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/screens/message_read_screen.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/screens/compose_screen.cpp
)

# doctest.cmake, from beside the package config the root CMakeLists found.
include(doctest)
doctest_discover_tests(amberedit_tests)
