set(RAINSTAR_TOOLKIT_SOURCE_DIR "" CACHE PATH "Pinned standalone GUI.Forms source")
if(RAINSTAR_TOOLKIT_SOURCE_DIR)
  set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_ENABLE_HARFBUZZ_TEXT ON CACHE BOOL "" FORCE)
  if(WIN32)
    set(GUI_FORMS_ENABLE_SKIA OFF CACHE BOOL "" FORCE)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GUI_FORMS_ENABLE_LINUX_HOST ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${RAINSTAR_TOOLKIT_SOURCE_DIR}" toolkit EXCLUDE_FROM_ALL)
  set(GUIForms_FONT_DIR "${RAINSTAR_TOOLKIT_SOURCE_DIR}/assets/fonts")
else()
# Keep the explicitly selected SDK ahead of ambient /usr/local headers. Mixing
# installed GUI.Forms headers from another SDK with these libraries breaks ABI.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application Threading)
set_property(TARGET GUIForms::Application PROPERTY IMPORTED_NO_SYSTEM TRUE)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES GUIForms::Application)
check_cxx_source_compiles("#include <gui_forms/live_surface.hpp>
int main() {
  gui_forms::LiveSurfaceDescription description{2, 2};
  description.opaque = true;
  description.pixel_format = gui_forms::native_live_surface_pixel_format();
  return gui_forms::LiveSurface::create(description) ? 0 : 1;
}" RAINSTAR_LIVE_SURFACE)
if(NOT RAINSTAR_LIVE_SURFACE)
  message(FATAL_ERROR "Plan Paint requires the pinned standalone GUI.Forms LiveSurface API.")
endif()
check_cxx_source_compiles("#include <gui_forms/canvas.hpp>
#include <type_traits>
static_assert(!std::is_final_v<gui_forms::RasterCanvas>);
int main() { return 0; }" RAINSTAR_EXTENSIBLE_CANVAS)
check_cxx_source_compiles("#include <gui_forms/basic_controls.hpp>
#include <gui_forms/application.hpp>
int main() {
  gui_forms::DropDownButton button(gui_forms::StableId(\"ribbon\"));
  button.set_drop_down_edge(gui_forms::DropDownButtonEdge::bottom);
  gui_forms::ApplicationWindowHandle handle;
  static_cast<void>(handle.toggle_full_screen());
  return 0;
}" RAINSTAR_RIBBON_CONTROLS)
check_cxx_source_compiles("#include <gui_forms/controls/scrollable_control/scrollable_control.hpp>
#include <gui_forms/events/input_events/input_events.hpp>
class Probe : public gui_forms::ScrollableControl {
 public:
  Probe() : ScrollableControl(gui_forms::StableId(\"probe\")) {}
  void check() { arrange_scroll_viewport({100, 100}, {200, 200}); }
};
int main() {
  Probe probe; probe.check();
  probe.set_cursor(gui_forms::CursorKind::resize_diagonal_down);
  return gui_forms::PhysicalKey::f12 == 0x45U ? 0 : 1;
}" RAINSTAR_SCROLLING_LAYOUT)
check_cxx_source_compiles("#include <gui_forms/host.hpp>
#include <type_traits>
#include <utility>
static_assert(std::is_same_v<decltype(std::declval<gui_forms::HostServices&>().read_clipboard_files()),
                             gui_forms::HostClipboardFilesResult>);
int main() { return 0; }" RAINSTAR_CLIPBOARD_FILES)
check_cxx_source_compiles("#include <gui_forms/control.hpp>
int main() { gui_forms::Control probe(gui_forms::StableId(\"probe\"));
probe.set_custom_cursor({}, gui_forms::CursorKind::crosshair); return 0; }
" RAINSTAR_CUSTOM_CURSORS)
if(NOT RAINSTAR_CUSTOM_CURSORS)
  message(FATAL_ERROR "Plan Paint requires the GUI.Forms custom cursor extension. See docs/CUSTOM_CURSORS.md.")
endif()
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT RAINSTAR_CLIPBOARD_FILES)
  message(FATAL_ERROR "Plan Paint requires the GUI.Forms file-reference clipboard service. Fetch the pinned SDK from third_party/gui-forms.lock.json.")
endif()
if(NOT RAINSTAR_EXTENSIBLE_CANVAS)
  message(FATAL_ERROR "Plan Paint requires GUI.Forms with the RasterCanvas extension (7444815 or later). See docs/GUI_FORMS_PORT.md.")
endif()
if(NOT RAINSTAR_RIBBON_CONTROLS)
  message(FATAL_ERROR "Plan Paint requires the GUI.Forms ribbon composition extension. See docs/GUI_FORMS_PORT.md.")
endif()
if(NOT RAINSTAR_SCROLLING_LAYOUT)
  message(FATAL_ERROR "Plan Paint requires the GUI.Forms scrolling-layout, keyboard, and diagonal-cursor extensions. See docs/GUI_FORMS_PORT.md.")
endif()
endif()
add_library(paint_forms STATIC src/forms/canvas_controls.cpp src/forms/gradient_controls.cpp src/forms/recovery.cpp src/forms/file_change.cpp src/forms/interface_theme.cpp src/cursors/tool_cursors.cpp src/forms/editor.cpp src/forms/pattern_canvas.cpp src/forms/display.cpp src/forms/ribbon.cpp src/forms/dialog.cpp src/forms/carpet_dialog.cpp src/forms/dither_dialog.cpp src/forms/icon_validation_dialog.cpp src/forms/icon_sheet_dialog.cpp src/forms/toolbar_strip_dialog.cpp src/forms/icon_export_dialog.cpp src/forms/asset_browser_dialog.cpp src/forms/asset_reveal.cpp src/forms/alpha_dialog.cpp src/forms/icon_effects_dialog.cpp src/forms/sticky_note_ribbon.cpp src/forms/numeric_transform_dialog.cpp src/forms/icon_palette_dialog.cpp src/forms/icon_states_dialog.cpp src/forms/icon_variant_export.cpp src/forms/icon_ribbon.cpp src/forms/icon_index_paint.cpp src/forms/icon_generation_dialog.cpp src/forms/icon_batch_dialog.cpp src/forms/icon_batch_stamp.cpp src/forms/icon_batch_palette.cpp src/forms/text.cpp src/forms/warp.cpp src/forms/atlas.cpp src/forms/selection.cpp src/forms/help.cpp src/forms/surface.cpp src/forms/guide.cpp src/forms/spirograph.cpp)
target_link_libraries(paint_forms PUBLIC paint_core GUIForms::Application)
target_include_directories(paint_forms PUBLIC src)
target_compile_definitions(paint_forms PUBLIC RAINSTAR_VERSION="${PAINT_RELEASE_VERSION}")
add_executable(plan-paint MACOSX_BUNDLE src/forms/main.cpp)
target_link_libraries(plan-paint PRIVATE paint_forms)
set_target_properties(plan-paint PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Plan Paint"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.malkuthsoft.planpaint"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PAINT_RELEASE_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PAINT_RELEASE_VERSION}")
# The consumer selects its font pack; optional CJK/emoji and extra mono faces
# remain available in the SDK without being bundled into Paint.
file(STRINGS "${PROJECT_SOURCE_DIR}/packaging/fonts.txt" paint_font_names)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/packaging/fonts.txt")
set(toolkit_fonts)
foreach(name IN LISTS paint_font_names)
  list(APPEND toolkit_fonts "${GUIForms_FONT_DIR}/${name}")
endforeach()
file(GLOB cairo_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/fonts/cairo-unicode/*")
file(GLOB poster_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/fonts/poster/*")
list(APPEND toolkit_fonts ${cairo_files} ${poster_files})
if(APPLE)
  enable_language(OBJCXX)
  target_sources(paint_forms PRIVATE src/platform_mac.mm src/file_reveal_mac.mm)
  target_link_libraries(paint_forms PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
  target_compile_definitions(paint_forms PRIVATE RAINSTAR_FORMS_NATIVE_PRINT=1)
  set_source_files_properties(${toolkit_fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
  target_sources(plan-paint PRIVATE ${toolkit_fonts} assets/app-icon.icns)
  set_source_files_properties(assets/app-icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(plan-paint PROPERTIES MACOSX_BUNDLE_ICON_FILE app-icon.icns)
endif()
if(WIN32)
  target_sources(paint_forms PRIVATE src/print_windows.cpp src/desktop_windows.cpp src/file_reveal_windows.cpp)
  # Explorer's Details tab and SmartScreen read the vendor from VERSIONINFO.
  string(REPLACE "." ";" paint_version_parts "${PAINT_RELEASE_VERSION}")
  list(GET paint_version_parts 0 PAINT_VERSION_MAJOR)
  list(GET paint_version_parts 1 PAINT_VERSION_MINOR)
  list(GET paint_version_parts 2 PAINT_VERSION_PATCH)
  configure_file("${PROJECT_SOURCE_DIR}/packaging/windows_version.rc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc" @ONLY)
  target_sources(plan-paint PRIVATE packaging/windows.rc "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc")
  target_include_directories(plan-paint PRIVATE assets)
  set_target_properties(plan-paint PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(plan-paint PRIVATE /ENTRY:mainCRTStartup)
  endif()
  target_link_libraries(paint_forms PRIVATE comdlg32 gdi32 ole32 oleaut32 shell32)
  target_compile_definitions(paint_forms PRIVATE RAINSTAR_FORMS_NATIVE_PRINT=1)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(paint_forms PRIVATE src/forms/linux_desktop.cpp src/file_reveal_linux.cpp)
  target_compile_definitions(paint_forms PRIVATE RAINSTAR_FORMS_NATIVE_PRINT=1)
  add_executable(paint-linux-print-tests tests/linux_print.cpp)
  target_link_libraries(paint-linux-print-tests PRIVATE paint_forms)
  find_program(RAINSTAR_GHOSTSCRIPT gs)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(RAINSTAR_GHOSTSCRIPT AND Python3_Interpreter_FOUND)
    add_test(NAME paint-linux-print COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/linux_print_render.py" "$<TARGET_FILE:paint-linux-print-tests>" "${RAINSTAR_GHOSTSCRIPT}")
  endif()
endif()
if(NOT MSVC)
  target_compile_options(paint_forms PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(paint-file-reveal-tests tests/file_reveal_tests.cpp)
target_link_libraries(paint-file-reveal-tests PRIVATE paint_forms)
add_test(NAME paint-file-reveal COMMAND paint-file-reveal-tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND Python3_Interpreter_FOUND)
  add_test(NAME paint-file-reveal-linux COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/file_reveal_linux.py" "$<TARGET_FILE:paint-file-reveal-tests>")
endif()
add_executable(paint-cursor-tests tests/cursor_tests.cpp)
target_link_libraries(paint-cursor-tests PRIVATE paint_forms)
add_test(NAME paint-cursors COMMAND paint-cursor-tests)
add_executable(paint-forms-tests tests/forms_tests.cpp)
target_link_libraries(paint-forms-tests PRIVATE paint_forms)
add_test(NAME paint-forms COMMAND paint-forms-tests --general)
add_test(NAME paint-icon-production-forms COMMAND paint-forms-tests --icon-production)
install(TARGETS plan-paint BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(paint-forms-native-tests MACOSX_BUNDLE tests/forms_native.cpp)
  target_link_libraries(paint-forms-native-tests PRIVATE paint_forms)
  if(APPLE)
    target_sources(paint-forms-native-tests PRIVATE ${toolkit_fonts})
    set_target_properties(paint-forms-native-tests PROPERTIES
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.malkuthsoft.planpaint.forms.validation")
  endif()
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Follow the executable for custom output directories and multi-config builds.
    # One producer owns these shared resources even under parallel builds.
    set(forms_runtime_directory "$<TARGET_FILE_DIR:plan-paint>")
    set(forms_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/forms-runtime-$<CONFIG>.stamp")
    set(forms_runtime_fonts ${toolkit_fonts})
    set(runtime_font_commands)
    foreach(font IN LISTS forms_runtime_fonts)
      list(APPEND runtime_font_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "${font}" "${forms_runtime_directory}/fonts")
    endforeach()
    add_custom_command(OUTPUT "${forms_runtime_stamp}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${forms_runtime_directory}/fonts"
      ${runtime_font_commands}
      COMMAND ${CMAKE_COMMAND} -E touch "${forms_runtime_stamp}"
      DEPENDS ${forms_runtime_fonts}
      VERBATIM)
    add_custom_target(paint-forms-runtime DEPENDS "${forms_runtime_stamp}")
    if(WIN32)
      add_dependencies(paint-forms-runtime GUIForms::Application GUIForms::Threading)
      add_custom_command(TARGET paint-forms-runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>" "${forms_runtime_directory}"
        VERBATIM)
    endif()
    foreach(forms_target IN ITEMS plan-paint paint-forms-native-tests paint-forms-tests)
      add_dependencies(${forms_target} paint-forms-runtime)
    endforeach()
  endif()
  add_test(NAME paint-forms-native COMMAND paint-forms-native-tests)
  add_test(NAME paint-clipboard-native COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/native_clipboard.py" "$<TARGET_FILE:paint-forms-native-tests>")
  set_tests_properties(paint-clipboard-native PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 90)
  add_test(NAME paint-healing-native COMMAND paint-forms-native-tests --healing)
  add_test(NAME paint-icon-production-native COMMAND paint-forms-native-tests --icon-production)
  add_test(NAME paint-icon-svg-native COMMAND paint-forms-native-tests --icon-svg)
  add_test(NAME paint-icon-formats-native COMMAND paint-forms-native-tests --icon-formats)
  add_test(NAME paint-icon-sheet-native COMMAND paint-forms-native-tests --icon-sheet)
  add_test(NAME paint-icon-export-native COMMAND paint-forms-native-tests --icon-export)
  add_test(NAME paint-alpha-tools-native COMMAND paint-forms-native-tests --alpha-tools)
  add_test(NAME paint-external-file-native COMMAND paint-forms-native-tests --external-file)
  add_test(NAME paint-icon-validation-native COMMAND paint-forms-native-tests --icon-validation)
  add_test(NAME paint-toolbar-strip-native COMMAND paint-forms-native-tests --toolbar-strip)
  add_test(NAME paint-numeric-transform-native COMMAND paint-forms-native-tests --numeric-transform)
  add_test(NAME paint-advanced-effects-native COMMAND paint-forms-native-tests --advanced-effects)
  add_test(NAME paint-icon-effects-native COMMAND paint-forms-native-tests --icon-effects)
  add_test(NAME paint-icon-palette-native COMMAND paint-forms-native-tests --icon-palette)
  add_test(NAME paint-icon-index-paint-native COMMAND paint-forms-native-tests --icon-index-paint)
  add_test(NAME paint-icon-indexed-native COMMAND paint-forms-native-tests --icon-indexed)
  add_test(NAME paint-icon-states-native COMMAND paint-forms-native-tests --icon-states)
  add_test(NAME paint-icon-ribbon-native COMMAND paint-forms-native-tests --icon-ribbon)
  add_test(NAME paint-icon-shared-palette-native COMMAND paint-forms-native-tests --icon-shared-palette)
  add_test(NAME paint-icon-batch-native COMMAND paint-forms-native-tests --icon-batch)
  add_test(NAME paint-icon-stamp-native COMMAND paint-forms-native-tests --icon-stamp)
  add_test(NAME paint-asset-browser-native COMMAND paint-forms-native-tests --asset-browser)
  add_test(NAME paint-stamp-placement-native COMMAND paint-forms-native-tests --stamp-placement)
  add_test(NAME paint-opacity-native COMMAND paint-forms-native-tests --opacity)
  add_test(NAME paint-sticky-native COMMAND paint-forms-native-tests --sticky)
  add_test(NAME paint-sticky-compact-native COMMAND paint-forms-native-tests --sticky-compact)
  add_test(NAME paint-corners-native COMMAND paint-forms-native-tests --corners)
  add_test(NAME paint-corners-compact-native COMMAND paint-forms-native-tests --corners-compact)
  add_test(NAME paint-geometry-styles-native COMMAND paint-forms-native-tests --geometry-styles)
  add_test(NAME paint-geometry-styles-compact-native COMMAND paint-forms-native-tests --geometry-styles-compact)
  add_test(NAME paint-markers-native COMMAND paint-forms-native-tests --markers)
  add_test(NAME paint-markers-compact-native COMMAND paint-forms-native-tests --markers-compact)
  add_test(NAME paint-opacity-compact-native COMMAND paint-forms-native-tests --opacity-compact)
endif()
# Explicit offline authoring target; not part of application builds or installation.
if(EXISTS "${PROJECT_SOURCE_DIR}/astra/ribbon-export.cpp")
  add_executable(paint-export-ribbon-icons EXCLUDE_FROM_ALL astra/ribbon-export.cpp)
  target_include_directories(paint-export-ribbon-icons PRIVATE src scripts)
  target_link_libraries(paint-export-ribbon-icons PRIVATE paint_core)
endif()

# External translations are optional at runtime. English lives in the binary.
file(GLOB paint_languages CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/languages/*.json")
if(APPLE)
  set_source_files_properties(${paint_languages} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/languages")
  target_sources(plan-paint PRIVATE ${paint_languages})
else()
  add_custom_target(paint-language-packs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:plan-paint>/languages"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/languages" "$<TARGET_FILE_DIR:plan-paint>/languages"
    VERBATIM)
  add_dependencies(plan-paint paint-language-packs)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/languages/" DESTINATION languages FILES_MATCHING PATTERN "*.json")
endif()

if(Python3_Interpreter_FOUND)
  if(RAINSTAR_TOOLKIT_SOURCE_DIR)
    set(paint_package_toolkit "${RAINSTAR_TOOLKIT_SOURCE_DIR}")
  else()
    get_filename_component(paint_package_toolkit "${GUIForms_FONT_DIR}/../../.." ABSOLUTE)
  endif()
  if(WIN32)
    get_filename_component(paint_compiler_runtime "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(paint_package_script windows)
    set(paint_package_arguments --runtime-dir "${paint_compiler_runtime}")
  elseif(APPLE)
    set(paint_package_script macos)
    set(paint_package_arguments --runtime "$ENV{GUI_FORMS_LLVM_RUNTIME}")
  else()
    set(paint_package_script linux)
  endif()
  add_custom_target(paint-package
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/package-${paint_package_script}.py"
      --build "${CMAKE_BINARY_DIR}" --version "${PAINT_RELEASE_VERSION}" --gui-forms-sdk "${paint_package_toolkit}" ${paint_package_arguments}
    DEPENDS plan-paint
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL)
endif()
