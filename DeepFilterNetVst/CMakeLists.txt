cmake_minimum_required(VERSION 3.22)

# 插件版本固定由源码文件维护；如需升级版本，请直接修改这里。
set(DEEPFILTERNET_PLUGIN_VERSION "2026.3.31.2")

if(NOT DEEPFILTERNET_PLUGIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "DEEPFILTERNET_PLUGIN_VERSION must use the numeric format YYYY.M.D.N, e.g. 2026.3.29.1")
endif()

project(DeepFilterNetVst VERSION ${DEEPFILTERNET_PLUGIN_VERSION} LANGUAGES C CXX RC)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/JUCE" CACHE PATH "Path to the JUCE repository")
set(FST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/FST" CACHE PATH "Path to the FST repository")
set(DEEPFILTERNET_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/DeepFilterNet" CACHE PATH "Path to the DeepFilterNet upstream repository")
set(CORROSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/Corrosion" CACHE PATH "Path to the Corrosion repository")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${FST_DIR}/fst/aeffectx.h")
  message(FATAL_ERROR "FST headers not found at ${FST_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${DEEPFILTERNET_UPSTREAM_DIR}/libDF/Cargo.toml")
  message(FATAL_ERROR "DeepFilterNet upstream not found at ${DEEPFILTERNET_UPSTREAM_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${CORROSION_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Corrosion not found at ${CORROSION_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

set(VST2_SDK_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/juce_vst2_sdk")
set(VST2_SDK_COMPAT_HEADERS "${VST2_SDK_COMPAT_DIR}/pluginterfaces/vst2.x")
file(MAKE_DIRECTORY "${VST2_SDK_COMPAT_HEADERS}")
configure_file("${FST_DIR}/fst/aeffect.h" "${VST2_SDK_COMPAT_HEADERS}/aeffect.h" COPYONLY)
configure_file("${FST_DIR}/fst/aeffectx.h" "${VST2_SDK_COMPAT_HEADERS}/aeffectx.h" COPYONLY)
configure_file("${FST_DIR}/fst/fst.h" "${VST2_SDK_COMPAT_HEADERS}/fst.h" COPYONLY)

set(DEEPFILTER_RUNTIME_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../rust/deepfilter_runtime_bridge/Cargo.toml")

set(CORROSION_BUILD_TESTS OFF CACHE BOOL "Disable Corrosion self tests" FORCE)
add_subdirectory("${CORROSION_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/Corrosion")
corrosion_import_crate(
  MANIFEST_PATH "${DEEPFILTER_RUNTIME_MANIFEST}"
  LOCKED
)

if(WIN32)
  # Corrosion 0.6.1 在当前依赖链上未把 bcrypt 传播到最终链接步骤，
  # 会导致 getrandom 依赖的 BCryptGenRandom 在插件链接阶段缺符号。
  target_link_libraries(deepfilter_runtime_bridge INTERFACE advapi32 bcrypt)
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

juce_set_vst2_sdk_path("${VST2_SDK_COMPAT_DIR}")

juce_add_plugin(DeepFilterNetVst
  VERSION "${PROJECT_VERSION}"
  COMPANY_NAME "DeepFilterNet"
  BUNDLE_ID "local.deepfilternet.plugin"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  PLUGIN_MANUFACTURER_CODE Dfnt
  PLUGIN_CODE Dfvt
  FORMATS VST VST3
  PRODUCT_NAME "DeepFilterNet"
  VST2_CATEGORY kPlugCategEffect
  VST3_CATEGORIES Fx
)

juce_generate_juce_header(DeepFilterNetVst)

target_sources(DeepFilterNetVst
  PRIVATE
    DenoiseEngine.cpp
    DenoiseEngine.h
    DeepFilterRuntimeBridge.h
    PluginEditor.cpp
    PluginEditor.h
    PluginProcessor.cpp
    PluginProcessor.h
)

target_include_directories(DeepFilterNetVst
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(DeepFilterNetVst
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
)

target_compile_options(DeepFilterNetVst
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(DeepFilterNetVst
  PRIVATE
    juce::juce_audio_utils
    deepfilter_runtime_bridge
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

set_target_properties(DeepFilterNetVst PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
