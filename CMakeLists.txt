cmake_minimum_required(VERSION 3.22)
project(UnterholzUH20 VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

add_subdirectory(JUCE)
add_subdirectory(clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(UnterholzUH20
    COMPANY_NAME "Unterholz"
    COMPANY_WEBSITE "https://github.com/ClayShentrup/unterholz-uh20"
    BUNDLE_ID com.unterholz.uh20
    PLUGIN_MANUFACTURER_CODE Untz
    PLUGIN_CODE Uh20
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Unterholz UH-20"
    DESCRIPTION "JFET guitar amp: one voice from studio clean to singing lead"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The standalone app needs audio input to process your guitar.")

target_sources(UnterholzUH20 PRIVATE source/PluginProcessor.cpp source/PluginEditor.cpp)
target_compile_definitions(UnterholzUH20 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(UnterholzUH20
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET UnterholzUH20
    CLAP_ID "com.unterholz.uh20"
    CLAP_FEATURES audio-effect distortion mono)

# tools
juce_add_console_app(uh20_screenshot PRODUCT_NAME "uh20_screenshot")
target_sources(uh20_screenshot PRIVATE tools/screenshot.cpp source/PluginProcessor.cpp source/PluginEditor.cpp)
target_compile_definitions(uh20_screenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 UH20_NO_PLUGIN_ENTRY=1)
target_link_libraries(uh20_screenshot PRIVATE juce::juce_audio_utils juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)

add_executable(uh20_cli tools/uh20_cli.cpp)

# tests: tests/uh20_tests.cpp (run with ctest, or uh20_tests --update to rewrite the goldens on purpose)
enable_testing()
add_executable(uh20_tests tests/uh20_tests.cpp)
add_test(NAME uh20_tests COMMAND uh20_tests)
