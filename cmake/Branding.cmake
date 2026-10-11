# Single source of truth for every user-visible name.
#
# The product name is still to be decided. Change the values here (or pass
# -DPRODUCT_NAME=... on the command line) and the window titles, About box,
# executable name, bundle identifier, installer names and settings location
# all follow.

set(PRODUCT_NAME          "Lumenis"                       CACHE STRING "User-visible product name")
set(PRODUCT_EXE_NAME      "lumenis"                       CACHE STRING "Executable / package file name")
set(PRODUCT_ORGANIZATION  "Open Compose"                  CACHE STRING "Organization name (settings, About box)")
set(PRODUCT_DOMAIN        "opencompose.org"               CACHE STRING "Organization domain (settings)")
set(PRODUCT_BUNDLE_ID     "org.opencompose.lumenis"       CACHE STRING "macOS bundle identifier / Linux app id")
set(PRODUCT_DESCRIPTION   "Professional non-linear video editor" CACHE STRING "One-line description")
set(PRODUCT_HOMEPAGE      "https://github.com/open-compose/lumenis" CACHE STRING "Project homepage")
set(PRODUCT_VERSION       "0.1.0")

# File name of the built program: "<name>.app" on macOS, "<exe>" elsewhere.
if(APPLE)
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_NAME}")
else()
    set(PRODUCT_OUTPUT_NAME "${PRODUCT_EXE_NAME}")
endif()
