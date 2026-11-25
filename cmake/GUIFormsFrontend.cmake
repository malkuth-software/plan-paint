set(RAINSTAR_TOOLKIT_SOURCE_DIR "" CACHE PATH "Pinned standalone GUI.Forms source")
if(RAINSTAR_TOOLKIT_SOURCE_DIR)
  set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GUI_FORMS_ENABLE_HARFBUZZ_TEXT ON CACHE BOOL "" FORCE)
  # The Win32 host renders through the private CPU Skia adapter since cp22.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GUI_FORMS_ENABLE_LINUX_HOST ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${RAINSTAR_TOOLKIT_SOURCE_DIR}" toolkit EXCLUDE_FROM_ALL)
  set(GUIForms_FONT_DIR "${RAINSTAR_TOOLKIT_SOURCE_DIR}/assets/fonts")
else()
# Keep the explicitly selected SDK ahead of ambient /usr/local headers. Mixing
# installed GUI.Forms headers from another SDK with these libraries breaks ABI.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
find_package(GUIForms 1.0 CONFIG REQUIRED COMPONENTS Application Threading)
set_property(TARGET GUIForms::Application PROPERTY IMPORTED_NO_SYSTEM TRUE)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES GUIForms::Application)
# Plan Paint is written against the cp22 checkpoint's standard (the
# Application-first lifecycle, property cells and the canonical ribbon) and
# cp29's: flat control headers and the target-phase mouse raisers that
# replaced Control::on_pointer.
check_cxx_source_compiles("#include <gui_forms/application.hpp>
#include <gui_forms/controls/ribbon.hpp>
#include <gui_forms/basic_controls.hpp>
class Probe final : public gui_forms::Control {
public:
  using gui_forms::Control::Control;
protected:
  void on_mouse_down(gui_forms::MouseEventArgs& arguments) override { gui_forms::Control::on_mouse_down(arguments); }
};
int main() {
  gui_forms::ApplicationConfig config{};
  static_cast<void>(config);
  gui_forms::Label label(gui_forms::StableId(\"probe\"));
  label.text = \"probe\";
  return label.text.read().empty() ? 1 : 0;
}" PLAN_PAINT_GUI_FORMS_STANDARD)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT PLAN_PAINT_GUI_FORMS_STANDARD)
  message(FATAL_ERROR "Plan Paint requires the GUI.Forms SDK pinned in third_party/gui-forms.lock.json (package version 0.6: checkpoint cp29 or later).")
endif()
endif()

# paint_core's CONV uses stxlib::conv (stxlib::algorithms); stx_conv is found
# only for the opt-in PLAN_PAINT_CONV_COMPARE comparison. An SDK's GUIFormsConfig finds both in this
# scope; a pinned in-tree toolkit defines them as global targets; a toolkit
# that installs stx at configure time leaves them in <build>/stx-prefix (or
# the GUI_FORMS_STX_PREFIX it was given), where they are found here.
if(GUI_FORMS_STX_PREFIX)
  set(paint_stx_prefix "${GUI_FORMS_STX_PREFIX}")
else()
  set(paint_stx_prefix "${CMAKE_BINARY_DIR}/stx-prefix")
endif()
if(PLAN_PAINT_CONV_COMPARE AND NOT TARGET stx_conv::stx_conv)
  find_package(stx_conv CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${paint_stx_prefix}")
endif()
if(NOT TARGET stxlib::algorithms)
  find_package(stxlib CONFIG REQUIRED NO_DEFAULT_PATH PATHS "${paint_stx_prefix}")
endif()
target_link_libraries(paint_core PUBLIC stxlib::algorithms)
# The curve tools flatten through GUI.Drawing's GraphicsPath (src/curve.cpp),
# which the static Application archive carries; the frontend-free build keeps
# its own flattening.
target_link_libraries(paint_core PRIVATE GUIForms::Application)
target_compile_definitions(paint_core PRIVATE RAINSTAR_GUI_DRAWING=1)
add_library(paint_forms STATIC src/forms/canvas_controls.cpp src/forms/gradient_controls.cpp src/forms/recovery.cpp src/forms/file_change.cpp src/forms/interface_theme.cpp src/cursors/tool_cursors.cpp src/forms/editor.cpp src/forms/pattern_canvas.cpp src/forms/display.cpp src/forms/ribbon.cpp src/forms/ribbon_tabs.cpp src/forms/identity.cpp src/forms/open_link.cpp src/forms/ribbon_galleries.cpp src/forms/ribbon_popups.cpp src/forms/dialog.cpp src/forms/carpet_dialog.cpp src/forms/dither_dialog.cpp src/forms/icon_validation_dialog.cpp src/forms/icon_sheet_dialog.cpp src/forms/toolbar_strip_dialog.cpp src/forms/icon_export_dialog.cpp src/forms/asset_browser_dialog.cpp src/forms/asset_reveal.cpp src/forms/alpha_dialog.cpp src/forms/icon_effects_dialog.cpp src/forms/sticky_note_ribbon.cpp src/forms/numeric_transform_dialog.cpp src/forms/icon_palette_dialog.cpp src/forms/icon_states_dialog.cpp src/forms/icon_variant_export.cpp src/forms/icon_ribbon.cpp src/forms/icon_index_paint.cpp src/forms/icon_generation_dialog.cpp src/forms/icon_batch_dialog.cpp src/forms/icon_batch_stamp.cpp src/forms/icon_batch_palette.cpp src/forms/text.cpp src/forms/content_layout.cpp src/forms/warp.cpp src/forms/atlas.cpp src/forms/selection.cpp src/forms/help.cpp src/forms/surface.cpp src/forms/guide.cpp src/forms/spirograph.cpp src/forms/model_tools.cpp)
target_link_libraries(paint_forms PUBLIC paint_core GUIForms::Application)
target_include_directories(paint_forms PUBLIC src)
target_compile_definitions(paint_forms PUBLIC RAINSTAR_VERSION="${PAINT_RELEASE_VERSION}")
# The static dialogs' content as form documents (forms/), generated at build
# time into Panel subclasses that EditorDialog adopts.
gui_forms_add_form(paint_forms forms/about.form CLASS AboutForm NAMESPACE paint::forms DERIVED)
gui_forms_add_form(paint_forms forms/english_help.form CLASS EnglishHelpForm NAMESPACE paint::forms DERIVED)
gui_forms_add_form(paint_forms forms/tool_size.form CLASS ToolSizeForm NAMESPACE paint::forms DERIVED)
add_executable(plan-paint MACOSX_BUNDLE src/forms/main.cpp)
target_link_libraries(plan-paint PRIVATE paint_forms)
# All the program's C++ allocations come from STXlib's allocator, as in
# GUI.Forms' own programs; a sanitizer build keeps the toolchain's operator new.
gui_forms_use_library_allocator(plan-paint)
# Plan Paint's theme package (colours, fonts and the bubble ribbon buttons),
# embedded and compiled once by Application::create.
gui_forms_add_theme(plan-paint themes/plan-paint FUNCTION plan_paint_theme NAMESPACE paint)
set_target_properties(plan-paint PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Plan Paint"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.malkuthsoft.planpaint"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PAINT_RELEASE_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PAINT_RELEASE_VERSION}")
# The interface faces are compiled into GUI.Forms (font pack stage 4):
# GUIForms_FONT_DIR holds only their licences and attribution, which Plan
# Paint carries in licenses/fonts. The canvas text tool's faces are Plan
# Paint's own content faces (the interface/content split), shipped in fonts.
file(GLOB toolkit_font_notices CONFIGURE_DEPENDS "${GUIForms_FONT_DIR}/OFL-*.txt"
     "${GUIForms_FONT_DIR}/LICENSE-*.txt" "${GUIForms_FONT_DIR}/ATTRIBUTION.md")
file(GLOB cairo_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/fonts/cairo-unicode/*")
file(GLOB poster_files CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/fonts/poster/*")
set(canvas_fonts ${cairo_files} ${poster_files})
if(APPLE)
  enable_language(OBJCXX)
  target_sources(paint_forms PRIVATE src/platform_mac.mm src/file_reveal_mac.mm)
  target_link_libraries(paint_forms PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
  target_compile_definitions(paint_forms PRIVATE RAINSTAR_FORMS_NATIVE_PRINT=1)
  set_source_files_properties(${canvas_fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
  set_source_files_properties(${toolkit_font_notices} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/fonts")
  target_sources(plan-paint PRIVATE ${canvas_fonts} ${toolkit_font_notices} assets/app-icon.icns)
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
  if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_link_options(plan-paint PRIVATE "LINKER:/ENTRY:mainCRTStartup")
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
# The sticky-note contract runs with Plan Paint's theme, as the application does.
gui_forms_add_theme(paint-forms-tests themes/plan-paint FUNCTION plan_paint_theme NAMESPACE paint)
add_test(NAME paint-forms COMMAND paint-forms-tests --general)
add_test(NAME paint-icon-production-forms COMMAND paint-forms-tests --icon-production)
install(TARGETS plan-paint BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(NOT APPLE)
  install(FILES ${toolkit_font_notices} DESTINATION licenses/fonts)
endif()
if(APPLE OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(paint-forms-native-tests MACOSX_BUNDLE tests/forms_native.cpp)
  target_link_libraries(paint-forms-native-tests PRIVATE paint_forms)
  # The native exercise runs Application-first with Plan Paint's package, as main.cpp.
  gui_forms_add_theme(paint-forms-native-tests themes/plan-paint FUNCTION plan_paint_theme NAMESPACE paint)
  if(APPLE)
    target_sources(paint-forms-native-tests PRIVATE ${canvas_fonts})
    set_target_properties(paint-forms-native-tests PROPERTIES
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.malkuthsoft.planpaint.forms.validation")
  endif()
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Follow the executable for custom output directories and multi-config builds.
    # One producer owns these shared resources even under parallel builds.
    set(forms_runtime_directory "$<TARGET_FILE_DIR:plan-paint>")
    set(forms_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/forms-runtime-$<CONFIG>.stamp")
    set(runtime_font_commands)
    foreach(font IN LISTS canvas_fonts)
      list(APPEND runtime_font_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "${font}" "${forms_runtime_directory}/fonts")
    endforeach()
    add_custom_command(OUTPUT "${forms_runtime_stamp}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${forms_runtime_directory}/fonts"
      ${runtime_font_commands}
      COMMAND ${CMAKE_COMMAND} -E touch "${forms_runtime_stamp}"
      DEPENDS ${canvas_fonts}
      VERBATIM)
    add_custom_target(paint-forms-runtime DEPENDS "${forms_runtime_stamp}")
    # One module (GUI.Forms 66f82629b): GUI.Forms and its threading library
    # link statically into each executable, so no library is copied here.
    foreach(forms_target IN ITEMS plan-paint paint-forms-native-tests paint-forms-tests)
      add_dependencies(${forms_target} paint-forms-runtime)
    endforeach()
  endif()
  add_test(NAME paint-forms-native COMMAND paint-forms-native-tests)
  add_test(NAME paint-clipboard-native COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/native_clipboard.py" "$<TARGET_FILE:paint-forms-native-tests>")
  set_tests_properties(paint-clipboard-native PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 90)
  add_test(NAME paint-healing-native COMMAND paint-forms-native-tests --healing)
  add_test(NAME paint-dialog-window-native COMMAND paint-forms-native-tests --dialog-window)
  # The owner's hovered selected Pencil tab (2026-11-17): fails while the
  # tab shows the menu-item hot plate; prints the engine's counters and
  # writes the ribbon captures (PAINT_TAB_HOVER_DIR, PAINT_TOOL_TAB_WIDTH).
  add_test(NAME paint-tab-hover-native COMMAND paint-forms-native-tests --tab-hover)
  add_test(NAME paint-tab-hover-narrow-native COMMAND paint-forms-native-tests --tab-hover)
  set_tests_properties(paint-tab-hover-narrow-native PROPERTIES ENVIRONMENT "PAINT_TOOL_TAB_WIDTH=100")
  set_tests_properties(paint-tab-hover-native paint-tab-hover-narrow-native PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
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
    # The packages read GUI.Forms' notices (and on Windows its fonts) from an
    # installed SDK: since the text stack moved to STXlib the notices exist only
    # as the install collects them. scripts/native-build.sh package installs the
    # embedded toolkit here before building paint-package.
    set(paint_package_toolkit "${CMAKE_BINARY_DIR}/gui-forms-sdk")
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
