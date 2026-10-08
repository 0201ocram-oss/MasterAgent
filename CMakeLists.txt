cmake_minimum_required(VERSION 3.22)

project(MasterAgent VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# i sorgenti contengono testo italiano con accenti (UTF-8)
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(MASTERAGENT_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Sorgenti condivise tra plugin e test (analisi + confronto)
# ---------------------------------------------------------------------------
set(MASTERAGENT_CORE_SOURCES
    Source/Analysis/AnalysisEngine.cpp
    Source/Analysis/LoudnessMeter.cpp
    Source/Analysis/TruePeakMeter.cpp
    Source/Analysis/DynamicsAnalyzer.cpp
    Source/Analysis/SpectrumAnalyzer.cpp
    Source/Analysis/TechnicalChecks.cpp
    Source/Analysis/StreamingPreview.cpp
    Source/Analysis/ReferenceAnalyzer.cpp
    Source/Analysis/SnapshotIO.cpp
    Source/Compare/TargetProfile.cpp
    Source/Compare/Comparator.cpp
    Source/Compare/EqSuggestion.cpp
    Source/Compare/VersionCompare.cpp
    Source/Compare/AlbumCheck.cpp
    Source/Compare/ReportExporter.cpp)

set(MASTERAGENT_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

juce_add_binary_data(MasterAgentProfiles
    HEADER_NAME MasterAgentProfiles.h
    NAMESPACE ProfileData
    SOURCES
        Resources/profiles/streaming_generic.json
        Resources/profiles/spotify.json
        Resources/profiles/apple_music.json
        Resources/profiles/youtube.json
        Resources/profiles/pop.json
        Resources/profiles/rock.json
        Resources/profiles/hiphop_trap.json
        Resources/profiles/edm_club.json
        Resources/profiles/techno_house.json)

# traduzioni dell'interfaccia (testo sorgente: italiano)
juce_add_binary_data(MasterAgentLang
    HEADER_NAME MasterAgentLang.h
    NAMESPACE LangData
    SOURCES
        Resources/lang/en.txt)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(MasterAgent
    COMPANY_NAME "MarcoMusumeci"
    PLUGIN_MANUFACTURER_CODE Mmus
    PLUGIN_CODE MaAg
    FORMATS VST3 Standalone
    PRODUCT_NAME "MasterAgent"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Analyzer Fx)

target_sources(MasterAgent PRIVATE
    ${MASTERAGENT_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Theme.cpp
    Source/UI/HelpTexts.cpp
    Source/UI/HeaderBar.cpp
    Source/UI/LoudnessPanel.cpp
    Source/UI/SpectrumPanel.cpp
    Source/UI/StereoPanel.cpp
    Source/UI/ReportPanel.cpp
    Source/UI/AlbumView.cpp)

target_include_directories(MasterAgent PRIVATE Source)


target_compile_definitions(MasterAgent PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(MasterAgent
    PRIVATE
        MasterAgentProfiles
        MasterAgentLang
        ${MASTERAGENT_JUCE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------
if(MASTERAGENT_BUILD_TESTS)
    add_subdirectory(external/Catch2)

    juce_add_console_app(MasterAgentTests PRODUCT_NAME "MasterAgentTests")

    target_sources(MasterAgentTests PRIVATE
        ${MASTERAGENT_CORE_SOURCES}
        Tests/TestSignals.cpp
        Tests/LoudnessTests.cpp
        Tests/TruePeakTests.cpp
        Tests/SpectrumStereoTests.cpp
        Tests/DynamicsTechnicalTests.cpp
        Tests/CompareTests.cpp
        Tests/LiveAnalysisTests.cpp
        Tests/EqSuggestionTests.cpp
        Tests/MixAnalysisTests.cpp
        Tests/ReportScoreTests.cpp
        Tests/SnapshotTests.cpp
        Tests/AlbumTests.cpp)

    target_include_directories(MasterAgentTests PRIVATE Source Tests)

    target_compile_definitions(MasterAgentTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(MasterAgentTests PRIVATE
        MasterAgentProfiles
        juce::juce_audio_formats
        juce::juce_dsp
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME MasterAgentTests COMMAND MasterAgentTests)
endif()

# ---------------------------------------------------------------------------
# Strumento di sviluppo: render della dashboard in PNG con dati sintetici
# ---------------------------------------------------------------------------
if(MASTERAGENT_BUILD_TESTS)
    juce_add_console_app(MasterAgentPreview PRODUCT_NAME "MasterAgentPreview")

    target_sources(MasterAgentPreview PRIVATE
        ${MASTERAGENT_CORE_SOURCES}
        Source/UI/Theme.cpp
        Source/UI/HelpTexts.cpp
        Source/UI/HeaderBar.cpp
        Source/UI/LoudnessPanel.cpp
        Source/UI/SpectrumPanel.cpp
        Source/UI/StereoPanel.cpp
        Source/UI/ReportPanel.cpp
        Source/UI/AlbumView.cpp
        tools/Preview.cpp)

    target_include_directories(MasterAgentPreview PRIVATE Source)
    target_compile_definitions(MasterAgentPreview PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(MasterAgentPreview PRIVATE
        MasterAgentProfiles
        MasterAgentLang
        juce::juce_gui_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Strumento di verifica: carica il VST3 compilato in un host e controlla trasparenza, CPU, render offline
# ---------------------------------------------------------------------------
if(MASTERAGENT_BUILD_TESTS)
    juce_add_console_app(MasterAgentHostTest PRODUCT_NAME "MasterAgentHostTest")
    target_sources(MasterAgentHostTest PRIVATE tools/HostTest.cpp)
    target_compile_definitions(MasterAgentHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MasterAgentHostTest PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Strumento di calibrazione: analizza file audio e confronta con i profili
# ---------------------------------------------------------------------------
if(MASTERAGENT_BUILD_TESTS)
    juce_add_console_app(MasterAgentAnalyze PRODUCT_NAME "MasterAgentAnalyze")
    target_sources(MasterAgentAnalyze PRIVATE ${MASTERAGENT_CORE_SOURCES} tools/Analyze.cpp)
    target_include_directories(MasterAgentAnalyze PRIVATE Source)
    target_compile_definitions(MasterAgentAnalyze PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(MasterAgentAnalyze PRIVATE
        MasterAgentProfiles
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
