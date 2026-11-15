# Single source of truth for every user-visible name.
#
# Change the values here (or pass -DPRODUCT_NAME=... on the command line) and
# the window titles, About box, executable name, bundle identifier, installer
# names, settings location and environment variable names all follow.
# "lumenis" stays as the internal code name (targets, namespaces, module ABI).

set(PRODUCT_NAME          "Cutthulhu"                     CACHE STRING "User-visible product name")
set(PRODUCT_EXE_NAME      "cutthulhu"                     CACHE STRING "Executable / package file name")
set(PRODUCT_ORGANIZATION  "Open Compose"                  CACHE STRING "Organization name (settings, About box)")
set(PRODUCT_DOMAIN        "opencompose.org"               CACHE STRING "Organization domain (settings)")
set(PRODUCT_BUNDLE_ID     "org.opencompose.cutthulhu"     CACHE STRING "macOS bundle identifier / Linux app id")
set(PRODUCT_DESCRIPTION   "Professional non-linear video editor" CACHE STRING "One-line description")
set(PRODUCT_HOMEPAGE      "https://github.com/open-compose/lumenis" CACHE STRING "Project homepage")
# The version is YEAR.N, the year and that year's release number counting from 1 (2026.1, then 2026.2 ... and
# 2027.1 the next year; the owner's decision, 2026-11-15). The release workflow works out the next one from the
# releases there are and passes it as -DPRODUCT_VERSION=..., so nobody edits it here: this default is what
# development builds report, "release 0" of the year, older than any release (docs/spec/versioning.md).
set(PRODUCT_VERSION       "2026.0"                        CACHE STRING "YEAR.N; the release workflow sets it")
if(NOT PRODUCT_VERSION MATCHES "^[0-9][0-9][0-9][0-9]\\.[0-9]+$")
    message(FATAL_ERROR "PRODUCT_VERSION is ${PRODUCT_VERSION}: it must be YEAR.N, as 2026.1 (docs/spec/versioning.md)")
endif()

# File name of the built program: "<name>.app" on macOS, "<exe>" elsewhere.
# Prefix of user-facing environment variables, e.g. CUTTHULHU_MODULE_PATH.
string(TOUPPER "${PRODUCT_EXE_NAME}" PRODUCT_ENV_PREFIX)
string(MAKE_C_IDENTIFIER "${PRODUCT_ENV_PREFIX}" PRODUCT_ENV_PREFIX)

if(APPLE)
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_NAME}")
else()
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_EXE_NAME}")
endif()
