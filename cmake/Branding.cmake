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
set(PRODUCT_VERSION       "0.1.0")

# File name of the built program: "<name>.app" on macOS, "<exe>" elsewhere.
# Prefix of user-facing environment variables, e.g. CUTTHULHU_MODULE_PATH.
string(TOUPPER "${PRODUCT_EXE_NAME}" PRODUCT_ENV_PREFIX)
string(MAKE_C_IDENTIFIER "${PRODUCT_ENV_PREFIX}" PRODUCT_ENV_PREFIX)

if(APPLE)
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_NAME}")
else()
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_EXE_NAME}")
endif()
