# ==============================================================================
#
#  AB Reference
#
#  A mastering A/B tool: switch between what your chain just produced and a
#  commercial reference loaded from disk, level-matched, so the comparison is
#  about tone rather than about loudness.
#
#  Three build products come out of this file. AU and VST3 are the deliverables.
#  Standalone is not shipped - it exists because developing a plugin through a
#  DAW means fighting the AU component cache on every rebuild, and the fastest
#  way to hear whether a crossfade clicks is an app you can just launch.
#
#  MeterCheck is a console target that links the same meter sources and runs
#  them against signals whose loudness is known from the standard. A LUFS meter
#  that is quietly wrong is worse than no LUFS meter at all, because every
#  judgement made downstream of it inherits the error silently.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(ABReference VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon only.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.5" CACHE STRING "" FORCE)

# JUCE lives outside the project tree rather than as a submodule, because it is
# shared with every other plugin on this machine and pinning a copy per project
# would mean three copies of a 500 MB framework.
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE framework")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Pass -DJUCE_PATH=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------

juce_add_plugin(ABReference
    COMPANY_NAME                "T'Zorr"
    BUNDLE_ID                   TZorr.ABReference
    # Not shown anywhere, but it is an abbreviation of the company name, so
    # changing the company name changes this too - which gives the AU and
    # the VST3 new unique ids, and a host that had already scanned an older
    # build sees this as a different plugin.
    PLUGIN_MANUFACTURER_CODE    Tzor
    PLUGIN_CODE                 Abrf
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "AB Reference"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE     # the A/B switch is a keyboard shortcut
    COPY_PLUGIN_AFTER_BUILD     FALSE    # Scripts/install.sh does this, and flushes the AU cache with it
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# The DSP and file-handling sources are listed separately so the verification
# target can link exactly the same objects the plugin runs. Testing a
# reimplementation of the meter would test nothing.
set(ABREFERENCE_CORE_SOURCES
    Source/LoudnessMeter.cpp
    Source/TruePeakMeter.cpp
    Source/ReferenceLoader.cpp
    Source/ReferencePlayer.cpp
    Source/ReferenceBus.cpp
    Source/ABEngine.cpp)

target_sources(ABReference PRIVATE
    ${ABREFERENCE_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReferenceSlotButton.cpp
    Source/WaveformDisplay.cpp)

target_compile_definitions(ABReference PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    # Standalone development builds should not grab an input device: this plugin
    # is fed by the host, and an open microphone during a listening test is a
    # feedback loop waiting to happen.
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(ABReference PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# MeterCheck - the verification harness
# ------------------------------------------------------------------------------

juce_add_console_app(MeterCheck PRODUCT_NAME "MeterCheck")

target_sources(MeterCheck PRIVATE
    Verification/MeterCheck.cpp
    Source/LoudnessMeter.cpp
    Source/TruePeakMeter.cpp
    Source/ABEngine.cpp
    Source/ReferencePlayer.cpp
    Source/ReferenceBus.cpp
    Source/ReferenceLoader.cpp)

target_include_directories(MeterCheck PRIVATE Source)

target_compile_definitions(MeterCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0
    # The end-to-end load test has to let the loader's async callback reach the
    # message thread, which in a console app means pumping the loop by hand.
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(MeterCheck PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_events
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# EditorShot - renders the panel to a PNG without needing a screen
# ------------------------------------------------------------------------------

juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")

target_sources(EditorShot PRIVATE Verification/EditorShot.cpp)

target_compile_definitions(EditorShot PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1)

# Linking the plugin's own shared-code library rather than recompiling its
# sources: that library already carries the JucePlugin_* definitions that
# juce_add_plugin generates, and a second copy compiled without them would be a
# different plugin wearing the same name.
target_link_libraries(EditorShot PRIVATE
    ABReference
    juce::juce_audio_formats
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_include_directories(EditorShot PRIVATE Source)

# Linking plugin shared code into a console app means both sides have an opinion
# about JUCE_STANDALONE_APPLICATION, and the console app's is the one that should
# win. Suppressing a warning is usually the wrong answer; here the redefinition
# is the intended arrangement and the warning has nothing else to tell us.
target_compile_options(EditorShot PRIVATE -Wno-macro-redefined)
