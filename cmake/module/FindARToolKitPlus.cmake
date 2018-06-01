# - Find ARToolKitPlus library
# Once done this will define
#
#  ARToolKitPlus_FOUND - This defines if we found ARToolKitPlus
#  ARToolKitPlus_INCLUDE_DIR - ARToolKitPlus include directory
#  ARToolKitPlus_LIBS - ARToolKitPlus libraries
#  ARToolKitPlus_DEFINITIONS - Compiler switches required for ARToolKitPlus


# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_LIBRARY() calls
#FIND_PACKAGE(PkgConfig)
#PKG_SEARCH_MODULE(PC_LIBARToolKitPlus libARToolKitPlus)

SET(ARToolKitPlus_DEFINITIONS ${PC_ARToolKitPlus_CFLAGS_OTHER})

FIND_PATH(
    ARToolKitPlus_INCLUDE_DIR
    NAMES "ARToolKitPlus.h"
    PATH_SUFFIXES "ARToolKitPlus"
)

FIND_LIBRARY(ARToolKitPlus_LIBS NAMES ARToolKitPlus
   HINTS
   ${PC_ARToolKitPlus_LIBDIR}
   ${PC_ARToolKitPlus_LIBRARY_DIRS}
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ARToolKitPlus DEFAULT_MSG ARToolKitPlus_LIBS ARToolKitPlus_INCLUDE_DIR)
