cmake_minimum_required(VERSION 3.22)

project(DrumBreaker VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # macOS: build for both Intel and Apple Silicon, deploy back to 10.13
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
else()
    unset(CMAKE_OSX_ARCHITECTURES CACHE)
    unset(CMAKE_OSX_DEPLOYMENT_TARGET CACHE)
endif()

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(WIN32)
    FetchContent_Declare(WebView2SDK
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1901.177
        URL_HASH SHA256=464360D5EEDCAE0E95B0A3FE13C74B496BC9B542980FF8ECEBD9FF7EA37CA39E
        DOWNLOAD_NAME WebView2.zip
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/Microsoft.Web.WebView2")
    FetchContent_MakeAvailable(WebView2SDK)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_BINARY_DIR}/_deps")
    set(DRUMBREAKER_WEBVIEW_OPTIONS NEEDS_WEBVIEW2 TRUE)
endif()

# Copying into the system VST3 folder needs elevated rights on Windows, so it is opt-in.
option(DRUMBREAKER_INSTALL_AFTER_BUILD "Copy the built plugin into the system plugin folders" OFF)

# Allow an already-present JUCE checkout to be used instead of downloading.
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DrumBreaker
    ${DRUMBREAKER_WEBVIEW_OPTIONS}
    COMPANY_NAME                "DrumBreaker Audio"
    BUNDLE_ID                   "com.drumbreakeraudio.drumbreaker"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     ${DRUMBREAKER_INSTALL_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE    Dbkr
    PLUGIN_CODE                 Dbk1
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Drum Breaker")

juce_generate_juce_header(DrumBreaker)

juce_add_binary_data(DrumBreakerIntroData
    HEADER_NAME DrumBreakerIntroData.h
    NAMESPACE DrumBreakerIntroData
    SOURCES Assets/LogoIntro.html Assets/LogoIntro.mp4)

target_sources(DrumBreaker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Licensing/LicensePolicy.cpp
    Source/Licensing/LicenseClient.cpp
    Source/Licensing/LicenseStore.cpp
    Source/Analysis/SampleAnalyzer.cpp
    Source/Audio/BreakEngine.cpp
    Source/Audio/OfflineExporter.cpp
    Source/Audio/ToneStack.cpp
    Source/Audio/FilterPresets.cpp
    Source/UI/EmeraldLookAndFeel.cpp
    Source/UI/WaveformEditor.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/XYPad.cpp
    Source/UI/ActivationPanel.cpp
    Source/UI/LogoIntro.cpp
    Source/UI/HintBroadcaster.cpp)

target_include_directories(DrumBreaker PRIVATE Source)

target_compile_definitions(DrumBreaker PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=$<BOOL:${WIN32}>
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(DrumBreaker PRIVATE
    DrumBreakerIntroData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

option(DRUMBREAKER_BUILD_TESTS "Build the offline analyser test harness" OFF)

function(drumbreaker_link_credential_store target)
    if(WIN32)
        target_link_libraries(${target} PRIVATE Advapi32)
    elseif(APPLE)
        target_link_libraries(${target} PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
endfunction()

drumbreaker_link_credential_store(DrumBreaker)
if(WIN32)
    target_link_libraries(DrumBreaker PUBLIC juce::juce_webview2)
endif()

if(DRUMBREAKER_BUILD_TESTS)
    juce_add_console_app(AnalyzerTest PRODUCT_NAME "AnalyzerTest")
    target_sources(AnalyzerTest PRIVATE Tests/AnalyzerTest.cpp Source/Analysis/SampleAnalyzer.cpp Source/Audio/ToneStack.cpp Source/Audio/BreakEngine.cpp Source/Audio/OfflineExporter.cpp Source/Licensing/LicensePolicy.cpp Source/Licensing/LicenseClient.cpp Source/Licensing/LicenseStore.cpp)
    target_include_directories(AnalyzerTest PRIVATE Source)
    target_compile_definitions(AnalyzerTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(AnalyzerTest PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_cryptography
        juce::juce_recommended_config_flags)
    enable_testing()
    drumbreaker_link_credential_store(AnalyzerTest)
    add_test(NAME EqualizerResponse COMMAND AnalyzerTest --eq-self-test)
    add_test(NAME TempoDetection COMMAND AnalyzerTest --tempo-self-test)
    add_test(NAME ProcessedWavExport COMMAND AnalyzerTest --export-self-test)
    add_test(NAME LoopPlayback COMMAND AnalyzerTest --loop-self-test)
    add_test(NAME LicensePolicy COMMAND AnalyzerTest --license-policy-self-test)
    add_test(NAME LicensePersistence COMMAND AnalyzerTest --license-persistence-self-test)

    juce_add_console_app(SessionStateTest PRODUCT_NAME "SessionStateTest")
    target_sources(SessionStateTest PRIVATE Tests/SessionStateTest.cpp)
    target_include_directories(SessionStateTest PRIVATE Source)
    target_compile_definitions(SessionStateTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SessionStateTest PRIVATE
        DrumBreaker
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_cryptography
        juce::juce_recommended_config_flags)
    add_test(NAME ProjectSessionState COMMAND SessionStateTest)
    add_test(NAME LogoIntroUI COMMAND SessionStateTest --logo-intro-self-test)
endif()
