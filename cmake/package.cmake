#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

if(UNIX
   AND NOT APPLE
   AND NOT ANDROID
   AND NOT QNX
)
  set(CPACK_PACKAGE_NAME "vlink")
  set(CPACK_PACKAGE_VERSION "${VLINK_VERSION}")
  set(CPACK_PACKAGE_VENDOR "Thun Lu")
  set(CPACK_PACKAGE_CONTACT "Thun Lu <thun.lu@zohomail.cn>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
  set(CPACK_PACKAGE_DESCRIPTION "High-performance middleware for autonomous driving and embodied AI")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/thun-res/vlink")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_STRIP_FILES ON)
  set(CPACK_THREADS 0)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" VLINK_PKG_OS)
  set(CPACK_PACKAGE_FILE_NAME "vlink-${VLINK_VERSION}-${VLINK_PKG_OS}-${CMAKE_SYSTEM_PROCESSOR}")
  if(NOT CPACK_GENERATOR)
    if(EXISTS "/etc/debian_version")
      set(CPACK_GENERATOR "DEB;TGZ")
    elseif(
      EXISTS "/etc/redhat-release"
      OR EXISTS "/etc/fedora-release"
      OR EXISTS "/etc/anolis-release"
    )
      set(CPACK_GENERATOR "RPM;TGZ")
    else()
      set(CPACK_GENERATOR "TGZ")
    endif()
  endif()
  # deb
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Thun Lu <thun.lu@zohomail.cn>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
  set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
  set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  set(CPACK_RPM_PACKAGE_AUTOPROV ON)
  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/usr;/usr/bin;/usr/lib;/usr/lib64;/usr/include;/usr/share")
  include(CPack)
endif()
