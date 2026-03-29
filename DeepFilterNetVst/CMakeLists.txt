cmake_minimum_required(VERSION 3.22)

project(DeepFilterNetVst VERSION 0.1.0 LANGUAGES C CXX RC)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/JUCE" CACHE PATH "Path to the JUCE repository")
set(FST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/FST" CACHE PATH "Path to the FST repository")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${FST_DIR}/fst/aeffectx.h")
  message(FATAL_ERROR "FST headers not found at ${FST_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

set(EMBEDDED_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/EmbeddedAssets")
set(DF_DLL "${EMBEDDED_ASSETS_DIR}/df.dll")
set(MODEL_FILE "${EMBEDDED_ASSETS_DIR}/DeepFilterNet3_onnx.tar.gz")

if(NOT EXISTS "${DF_DLL}")
  message(FATAL_ERROR "df.dll not found at ${DF_DLL}")
endif()

if(NOT EXISTS "${MODEL_FILE}")
  message(FATAL_ERROR "Model file not found at ${MODEL_FILE}")
endif()

set(VST2_SDK_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/juce_vst2_sdk")
set(VST2_SDK_COMPAT_HEADERS "${VST2_SDK_COMPAT_DIR}/pluginterfaces/vst2.x")
file(MAKE_DIRECTORY "${VST2_SDK_COMPAT_HEADERS}")
configure_file("${FST_DIR}/fst/aeffect.h" "${VST2_SDK_COMPAT_HEADERS}/aeffect.h" COPYONLY)
configure_file("${FST_DIR}/fst/aeffectx.h" "${VST2_SDK_COMPAT_HEADERS}/aeffectx.h" COPYONLY)
configure_file("${FST_DIR}/fst/fst.h" "${VST2_SDK_COMPAT_HEADERS}/fst.h" COPYONLY)

file(TO_CMAKE_PATH "${DF_DLL}" DF_DLL_RC)
file(TO_CMAKE_PATH "${MODEL_FILE}" MODEL_FILE_RC)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/EmbeddedResources.rc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedResources.rc"
  @ONLY
)

add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

juce_set_vst2_sdk_path("${VST2_SDK_COMPAT_DIR}")

juce_add_plugin(DeepFilterNetVst
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
    PluginEditor.cpp
    PluginEditor.h
    PluginProcessor.cpp
    PluginProcessor.h
    ResourceIds.h
    RuntimeAssets.cpp
    RuntimeAssets.h
)

target_include_directories(DeepFilterNetVst
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(TARGET DeepFilterNetVst_VST)
  target_sources(DeepFilterNetVst_VST
    PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedResources.rc"
  )

  target_include_directories(DeepFilterNetVst_VST
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

if(TARGET DeepFilterNetVst_VST3)
  target_sources(DeepFilterNetVst_VST3
    PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedResources.rc"
  )

  target_include_directories(DeepFilterNetVst_VST3
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

target_compile_definitions(DeepFilterNetVst
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(DeepFilterNetVst
  PRIVATE
    juce::juce_audio_utils
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
