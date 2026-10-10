# VOICEVOX native CPU inference. Downloading and Python are build-time only.
set(quality_speech_supported FALSE)
if(MOJI_NOOK_BUNDLE_SPEECH AND MOJI_NOOK_SPEECH_BUNDLE
   AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(quality_speech_supported TRUE)
endif()
option(MOJI_NOOK_BUNDLE_QUALITY_SPEECH "Bundle offline quality Japanese pronunciation" ${quality_speech_supported})
if(MOJI_NOOK_BUNDLE_QUALITY_SPEECH AND quality_speech_supported)
  set(MOJI_NOOK_QUALITY_SPEECH_BUNDLE "${MOJI_NOOK_SPEECH_BUNDLE}/quality")
  if(WIN32)
    set(quality_manifest "quality-assets-windows.json")
    set(quality_setup_arguments --windows)
    set(quality_platform_files lib/voicevox_core.dll lib/voicevox_core.lib lib/voicevox_onnxruntime.dll
        licenses/open-jtalk-dictionary/COPYING ../dictionary/sys.dic ../dictionary/unk.dic
        ../dictionary/char.bin ../dictionary/matrix.bin)
  else()
    set(quality_manifest "quality-assets.json")
    set(quality_setup_arguments)
    set(quality_platform_files lib/libvoicevox_core.so lib/libvoicevox_onnxruntime.so.1.17.3)
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/scripts/${quality_manifest}" quality_manifest_sha)
  file(SHA256 "${CMAKE_SOURCE_DIR}/scripts/setup_quality_speech.py" quality_setup_sha)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/${quality_manifest}" "${CMAKE_SOURCE_DIR}/scripts/setup_quality_speech.py")
  set(quality_expected_stamp "${quality_manifest_sha}\n${quality_setup_sha}")
  set(quality_bundle_valid TRUE)
  if(EXISTS "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/.complete")
    file(READ "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/.complete" quality_actual_stamp)
    string(STRIP "${quality_actual_stamp}" quality_actual_stamp)
    if(NOT quality_actual_stamp STREQUAL quality_expected_stamp)
      set(quality_bundle_valid FALSE)
    endif()
  else()
    set(quality_bundle_valid FALSE)
  endif()
  foreach(quality_file ${quality_platform_files}
          models/4.vvm models/9.vvm models/21.vvm include/voicevox_core.h
          licenses/voicevox-core/LICENSE licenses/voicevox-core/README.txt licenses/voicevox-core/VERSION
          licenses/voicevox-onnxruntime/TERMS.txt licenses/voicevox-onnxruntime/third-party-notices.html
          licenses/voicevox-onnxruntime/VERSION_NUMBER licenses/voicevox-onnxruntime/GIT_COMMIT_ID
          licenses/voicevox-vvm/TERMS.txt licenses/voicevox-vvm/README.txt
          licenses/ATTRIBUTION.txt licenses/${quality_manifest})
    if(NOT EXISTS "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/${quality_file}")
      set(quality_bundle_valid FALSE)
    endif()
  endforeach()
  if(NOT quality_bundle_valid)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/setup_quality_speech.py"
                            "${MOJI_NOOK_SPEECH_BUNDLE}" ${quality_setup_arguments}
                    RESULT_VARIABLE quality_result)
    if(NOT quality_result EQUAL 0)
      message(FATAL_ERROR "Could not prepare the offline quality Japanese speech bundle")
    endif()
  endif()
  add_library(moji_nook_voicevox_core SHARED IMPORTED)
  set_target_properties(moji_nook_voicevox_core PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/include")
  if(WIN32)
    set_target_properties(moji_nook_voicevox_core PROPERTIES
      IMPORTED_LOCATION "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/lib/voicevox_core.dll"
      IMPORTED_IMPLIB "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/lib/voicevox_core.lib")
  else()
    set_target_properties(moji_nook_voicevox_core PROPERTIES
      IMPORTED_LOCATION "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/lib/libvoicevox_core.so"
      IMPORTED_NO_SONAME TRUE)
  endif()
  add_executable(moji_nook_quality_worker src/quality_worker.cpp)
  set_target_properties(moji_nook_quality_worker PROPERTIES OUTPUT_NAME "moji-nook-quality-worker")
  target_link_libraries(moji_nook_quality_worker PRIVATE Qt6::Core moji_nook_voicevox_core)
  if(NOT WIN32)
    set_target_properties(moji_nook_quality_worker PROPERTIES
      INSTALL_RPATH "$ORIGIN/../share/moji-nook/speech/quality/lib")
  endif()
  install(TARGETS moji_nook_quality_worker RUNTIME DESTINATION ${MOJI_NOOK_BIN_INSTALL_DIR})
  install(DIRECTORY "${MOJI_NOOK_QUALITY_SPEECH_BUNDLE}/" DESTINATION ${MOJI_NOOK_SPEECH_INSTALL_DIR}/quality
          USE_SOURCE_PERMISSIONS PATTERN "include" EXCLUDE PATTERN "*.lib" EXCLUDE)
  if(WIN32)
    install(DIRECTORY "${MOJI_NOOK_SPEECH_BUNDLE}/dictionary" DESTINATION ${MOJI_NOOK_SPEECH_INSTALL_DIR})
  endif()
elseif(MOJI_NOOK_BUNDLE_QUALITY_SPEECH)
  message(STATUS "Quality Japanese pronunciation is bundled only for Linux and Windows x86_64")
endif()
