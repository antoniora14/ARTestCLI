set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# The bundled vcpkg may not recognize MSBuild's v145 label. Pin the VS instance;
# its vcvars selects the installed compiler (recorded by vcpkg in the build log).
set(VCPKG_ENV_PASSTHROUGH ARTEST_VISUAL_STUDIO_PATH)
if(NOT DEFINED ENV{ARTEST_VISUAL_STUDIO_PATH})
    message(FATAL_ERROR "Use restore-process-dependencies.ps1 to select Visual Studio explicitly.")
endif()
set(VCPKG_VISUAL_STUDIO_PATH "$ENV{ARTEST_VISUAL_STUDIO_PATH}")
