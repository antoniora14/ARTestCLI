cmake_minimum_required(VERSION 3.24)
project(ARTestDev VERSION 0.1.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ARTestDev is supported only on Windows x64")
endif()
if(NOT MSVC OR MSVC_VERSION GREATER_EQUAL 1950)
  message(FATAL_ERROR "Qt 6.8.3 msvc2022_64 requires the selected MSVC 2022 v143 toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Widgets Concurrent Test)

add_library(ARTestDevPrivate STATIC Inspection.cpp ProcessAdapter.cpp Readiness.cpp Authoring.cpp SdkLocation.cpp TreeProcess.cpp PreparationService.cpp NativeService.cpp)
qt_add_resources(ARTestDevPrivate authoring PREFIX "/authoring" BASE "resources"
  FILES resources/driver.py.in resources/command.py.in)
target_link_libraries(ARTestDevPrivate PUBLIC Qt6::Core Qt6::Concurrent)
target_include_directories(ARTestDevPrivate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ARTestDevPrivate PRIVATE /W4 /utf-8)

add_executable(ARTestDevNative NativeMain.cpp NativeOutputs.cpp NativeRetention.cpp)
target_link_libraries(ARTestDevNative PRIVATE ARTestDevPrivate bcrypt)
target_compile_options(ARTestDevNative PRIVATE /W4 /utf-8)

add_executable(ARTestDev WIN32 Main.cpp AuthoringWidget.cpp)
add_dependencies(ARTestDev ARTestDevNative)
target_link_libraries(ARTestDev PRIVATE ARTestDevPrivate Qt6::Widgets Qt6::Concurrent)
target_compile_options(ARTestDev PRIVATE /W4 /utf-8)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ARTestDev)
set(ARTESTDEV_STAGING_ROOT "${CMAKE_CURRENT_BINARY_DIR}/development staging" CACHE PATH "Private development staging parent (not a runtime setting)")
set(ARTESTDEV_WHEELHOUSE "" CACHE PATH "Explicit verified offline wheel inputs for development staging")
if(NOT IS_DIRECTORY "${ARTESTDEV_WHEELHOUSE}")
  message(FATAL_ERROR "Set ARTESTDEV_WHEELHOUSE to existing verified local wheel inputs; no downloads are performed")
endif()
find_program(ARTESTDEV_PWSH NAMES pwsh REQUIRED)
set_property(TARGET ARTestDev PROPERTY VS_DEBUGGER_COMMAND "${ARTESTDEV_STAGING_ROOT}/$<CONFIG>/ARTestDev.exe")
set_property(TARGET ARTestDev PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

get_filename_component(ARTESTDEV_QT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
set(ARTESTDEV_WINDEPLOYQT "${ARTESTDEV_QT_ROOT}/bin/windeployqt.exe")
if(NOT EXISTS "${ARTESTDEV_WINDEPLOYQT}")
  message(FATAL_ERROR "Qt windeployqt.exe was not found at ${ARTESTDEV_WINDEPLOYQT}")
endif()
add_custom_command(TARGET ARTestDev POST_BUILD
  COMMAND "${ARTESTDEV_WINDEPLOYQT}"
    "$<IF:$<CONFIG:Debug>,--debug,--release>"
    --no-translations --no-compiler-runtime --no-system-d3d-compiler
    --no-system-dxc-compiler --no-opengl-sw "$<TARGET_FILE:ARTestDev>"
  COMMENT "Copy the Qt runtime for local ARTestDev debugging"
  VERBATIM)

add_custom_target(ARTestDevStaging ALL
  COMMAND "${ARTESTDEV_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/StageDevelopment.ps1"
    -Executable "$<TARGET_FILE:ARTestDev>" -QtRoot "${ARTESTDEV_QT_ROOT}"
    -Destination "${ARTESTDEV_STAGING_ROOT}/$<CONFIG>" -Configuration "$<CONFIG>" -Wheelhouse "${ARTESTDEV_WHEELHOUSE}"
  DEPENDS ARTestDev
  VERBATIM)
# Building the startup project alone must also refresh the development copy.
add_custom_command(TARGET ARTestDev POST_BUILD
  COMMAND "${ARTESTDEV_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/StageDevelopment.ps1"
    -Executable "$<TARGET_FILE:ARTestDev>" -QtRoot "${ARTESTDEV_QT_ROOT}"
    -Destination "${ARTESTDEV_STAGING_ROOT}/$<CONFIG>" -Configuration "$<CONFIG>" -Wheelhouse "${ARTESTDEV_WHEELHOUSE}"
  VERBATIM)

set(ARTESTDEV_EVIDENCE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/dev014-evidence/regression" CACHE PATH "Current delivery test evidence")
include(CTest)
if(BUILD_TESTING)
  add_executable(ARTestDevNativeTests tests/NativeTests.cpp NativeOutputs.cpp NativeRetention.cpp)
  target_compile_definitions(ARTestDevNativeTests PRIVATE ARTESTDEV_TESTING)
  add_executable(ARTestDevNativeProcessProbe ../../tests/TestSupport/Fakes/ARTestDevNativeProcessProbe.cpp)
  target_compile_options(ARTestDevNativeProcessProbe PRIVATE /W4 /utf-8)
  add_executable(ARTestDevPublicationProbe ../../tests/TestSupport/Fakes/ARTestDevPublicationProbe.cpp NativeOutputs.cpp NativeRetention.cpp)
  target_compile_definitions(ARTestDevPublicationProbe PRIVATE ARTESTDEV_TESTING)
  target_link_libraries(ARTestDevPublicationProbe PRIVATE ARTestDevPrivate bcrypt)
  target_compile_options(ARTestDevPublicationProbe PRIVATE /W4 /utf-8)
  target_link_libraries(ARTestDevNativeTests PRIVATE ARTestDevPrivate Qt6::Test advapi32 bcrypt)
  target_compile_options(ARTestDevNativeTests PRIVATE /W4 /utf-8)
  add_test(NAME ARTestDevNativeTests COMMAND ARTestDevNativeTests)
  set_tests_properties(ARTestDevNativeTests PROPERTIES TIMEOUT 3600 ENVIRONMENT "ARTESTDEV_TEST_STAGING=${ARTESTDEV_STAGING_ROOT}/$<CONFIG>;QTEST_FUNCTION_TIMEOUT=1800000")
  file(MAKE_DIRECTORY "${ARTESTDEV_EVIDENCE_ROOT}")
  add_executable(ARTestDevPreparationTests tests/PreparationTests.cpp)
  target_link_libraries(ARTestDevPreparationTests PRIVATE ARTestDevPrivate Qt6::Test advapi32)
  target_compile_definitions(ARTestDevPreparationTests PRIVATE ARTESTDEV_TESTING)
  target_compile_options(ARTestDevPreparationTests PRIVATE /W4 /utf-8)
  add_test(NAME ARTestDevPreparationTests COMMAND ARTestDevPreparationTests -o "${ARTESTDEV_EVIDENCE_ROOT}/preparation-$<CONFIG>.txt,txt")
  set_tests_properties(ARTestDevPreparationTests PROPERTIES TIMEOUT 900 ENVIRONMENT "ARTESTDEV_TEST_STAGING=${ARTESTDEV_STAGING_ROOT}/$<CONFIG>")
  add_executable(ARTestDevTests tests/Tests.cpp)
  target_link_libraries(ARTestDevTests PRIVATE ARTestDevPrivate Qt6::Test)
  target_compile_options(ARTestDevTests PRIVATE /W4)
  add_test(NAME ARTestDevTests COMMAND ARTestDevTests)
  add_executable(ARTestDevAuthoringTests tests/AuthoringTests.cpp)
  target_link_libraries(ARTestDevAuthoringTests PRIVATE ARTestDevPrivate Qt6::Test advapi32)
  target_compile_options(ARTestDevAuthoringTests PRIVATE /W4 /utf-8)
  add_test(NAME ARTestDevAuthoringTests COMMAND ARTestDevAuthoringTests -o "${ARTESTDEV_EVIDENCE_ROOT}/authoring-$<CONFIG>.txt,txt")
  add_test(NAME ARTestDevNativeOnlyTests COMMAND ARTestDevAuthoringTests
    variants:cpp/driver-command variants:cpp/driver-only variants:cpp/command-only
    -o "${ARTESTDEV_EVIDENCE_ROOT}/native-only-$<CONFIG>.txt,txt")
  set_tests_properties(ARTestDevNativeOnlyTests PROPERTIES ENVIRONMENT "ARTESTDEV_TEST_PYTHON=" TIMEOUT 180)
  add_custom_command(TARGET ARTestDevAuthoringTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:ARTestDevAuthoringTests>" VERBATIM)
  add_executable(ARTestDevEditorProbe ../../tests/TestSupport/Fakes/ARTestDevEditorProbe.cpp)
  target_compile_options(ARTestDevEditorProbe PRIVATE /W4)
  add_executable(ARTestDevUiTests tests/UiTests.cpp AuthoringWidget.cpp)
  add_executable(ARTestDevIncompatiblePython ../../tests/TestSupport/Fakes/ARTestDevIncompatiblePython.cpp)
  target_compile_options(ARTestDevIncompatiblePython PRIVATE /W4)
  add_dependencies(ARTestDevUiTests ARTestDevIncompatiblePython)
  target_link_libraries(ARTestDevUiTests PRIVATE ARTestDevPrivate Qt6::Widgets Qt6::Concurrent Qt6::Test)
  target_compile_options(ARTestDevUiTests PRIVATE /W4 /utf-8)
  target_compile_definitions(ARTestDevUiTests PRIVATE ARTESTDEV_TESTING)
  add_test(NAME ARTestDevUiTests COMMAND ARTestDevUiTests -o "${ARTESTDEV_EVIDENCE_ROOT}/ui-$<CONFIG>.txt,txt")
  set_tests_properties(ARTestDevTests ARTestDevAuthoringTests ARTestDevUiTests PROPERTIES TIMEOUT 180)
  set_property(TEST ARTestDevAuthoringTests ARTestDevNativeOnlyTests ARTestDevUiTests APPEND PROPERTY
    ENVIRONMENT "ARTESTDEV_TEST_STAGING=${ARTESTDEV_STAGING_ROOT}/$<CONFIG>")
  add_test(NAME ARTestDevAssemblyTests COMMAND "${ARTESTDEV_PWSH}" -NoProfile
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/StageDevelopmentTests.ps1"
    -Executable "$<TARGET_FILE:ARTestDev>" -QtRoot "${ARTESTDEV_QT_ROOT}"
    -EvidenceRoot "${ARTESTDEV_EVIDENCE_ROOT}" -Configuration "$<CONFIG>" -Wheelhouse "${ARTESTDEV_WHEELHOUSE}")
  set_tests_properties(ARTestDevAssemblyTests PROPERTIES TIMEOUT 180)
endif()
