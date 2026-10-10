# Self-contained speech assets. Build-time downloads only; runtime is offline.
option(MOJI_NOOK_BUNDLE_SPEECH "Bundle offline Japanese pronunciation" ON)
if(MOJI_NOOK_BUNDLE_SPEECH AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(MOJI_NOOK_SPEECH_BUNDLE "${CMAKE_BINARY_DIR}/speech")
  file(SHA256 "${CMAKE_SOURCE_DIR}/scripts/speech-assets.json" speech_manifest_sha)
  file(SHA256 "${CMAKE_SOURCE_DIR}/scripts/setup_speech.py" speech_setup_sha)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/speech-assets.json" "${CMAKE_SOURCE_DIR}/scripts/setup_speech.py")
  set(speech_expected_stamp "${speech_manifest_sha}\n${speech_setup_sha}")
  set(speech_bundle_valid TRUE)
  if(EXISTS "${MOJI_NOOK_SPEECH_BUNDLE}/.complete")
    file(READ "${MOJI_NOOK_SPEECH_BUNDLE}/.complete" speech_actual_stamp)
    string(STRIP "${speech_actual_stamp}" speech_actual_stamp)
    if(NOT speech_actual_stamp STREQUAL speech_expected_stamp)
      set(speech_bundle_valid FALSE)
    endif()
  else()
    set(speech_bundle_valid FALSE)
  endif()
  foreach(speech_file bin/open_jtalk lib/libHTSEngine.so.1
          dictionary/sys.dic dictionary/unk.dic dictionary/char.bin
          dictionary/matrix.bin
          voices/mei.htsvoice voices/takumi.htsvoice voices/tohoku.htsvoice
          licenses/mei.txt licenses/takumi.txt licenses/tohoku.txt
          licenses/open-jtalk.txt licenses/libhtsengine1.txt
          licenses/open-jtalk-mecab-naist-jdic.txt licenses/ATTRIBUTION.txt
          licenses/speech-assets.json)
    if(NOT EXISTS "${MOJI_NOOK_SPEECH_BUNDLE}/${speech_file}")
      set(speech_bundle_valid FALSE)
    endif()
  endforeach()
  if(NOT speech_bundle_valid)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/setup_speech.py" "${MOJI_NOOK_SPEECH_BUNDLE}"
                    RESULT_VARIABLE speech_result)
    if(NOT speech_result EQUAL 0)
      message(FATAL_ERROR "Could not prepare the local Japanese speech bundle")
    endif()
  endif()
  # Quality has its own install rule; stale assets must not leak into Fast-only packages.
  install(DIRECTORY "${MOJI_NOOK_SPEECH_BUNDLE}/" DESTINATION ${MOJI_NOOK_SPEECH_INSTALL_DIR}
          USE_SOURCE_PERMISSIONS PATTERN "quality" EXCLUDE)
elseif(MOJI_NOOK_BUNDLE_SPEECH AND WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  # Windows has no Open JTalk build; the Quality setup provides the dictionary.
  set(MOJI_NOOK_SPEECH_BUNDLE "${CMAKE_BINARY_DIR}/speech")
else()
  message(STATUS "Bundled Japanese pronunciation is available for Linux x86_64 builds")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/QualitySpeechBundle.cmake)
add_library(moji_nook_speech src/speech.cpp)
target_include_directories(moji_nook_speech PUBLIC src)
target_link_libraries(moji_nook_speech PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)
option(MOJI_NOOK_BUILD_TREE_SPEECH "Let binaries fall back to speech assets in this build tree" ON)
if(MOJI_NOOK_SPEECH_BUNDLE AND MOJI_NOOK_BUILD_TREE_SPEECH)
  target_compile_definitions(moji_nook_speech PRIVATE MOJI_NOOK_SPEECH_DIR="${MOJI_NOOK_SPEECH_BUNDLE}")
endif()
if(TARGET moji_nook_quality_worker)
  add_dependencies(moji_nook_speech moji_nook_quality_worker)
  if(MOJI_NOOK_BUILD_TREE_SPEECH)
    target_compile_definitions(moji_nook_speech PRIVATE MOJI_NOOK_QUALITY_WORKER="$<TARGET_FILE:moji_nook_quality_worker>")
  endif()
endif()
