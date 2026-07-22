# Overlay triplet: same name/settings as vcpkg's stock x64-windows, plus a
# pin to VS 2022 Community's stable MSVC toolset. Without this, vcpkg
# auto-selects the newest VS instance on the machine — the VS 18 BuildTools
# preview (MSVC 14.51), whose cl.exe crashed with an access violation
# (0xC0000005) while building qtbase's qmake. Activated via
# VCPKG_OVERLAY_TRIPLETS in CMakePresets.json.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Pin the VS instance only (no VCPKG_PLATFORM_TOOLSET_VERSION): vcpkg's
# toolset-prefix matching rejected an explicit "14.44" even though that
# toolset is installed, and Community's default toolset is already the
# stable 14.44 — pinning the instance is sufficient to exclude the
# crashing 14.51 preview in VS 18 BuildTools.
# NOTE: must be backslashes — vcpkg compares this against vswhere's
# reported installation path textually, and forward slashes never match.
set(VCPKG_VISUAL_STUDIO_PATH "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community")
