#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
# Packaging and install layout for CardputerZero Debian builds.

include(GNUInstallDirs)

# APP_DISPLAY_NAME, APP_PACKAGE_NAME and APP_PACKAGE_DESCRIPTION come from the identity block in CMakeLists.txt.
set(APP_DEBIAN_REVISION "1" CACHE STRING "Debian package revision (the N in <version>-N)")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
# Placeholder on purpose: set a real maintainer name and e-mail before
# publishing; the store rejects placeholder values.
set(APP_MAINTAINER "TODO Your Name <todo@example.invalid>" CACHE STRING "Debian package maintainer")
set(APP_INSTALL_SYSTEMD_SERVICE ON CACHE BOOL "Install a systemd service file for embedded deployments")

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/app.service.in"
        "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        @ONLY
    )
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/${APP_CONFIG_NAME}.conf"
    DESTINATION "/etc"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/audio"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
    PATTERN ".DS_Store" EXCLUDE
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
    FILES_MATCHING
    PATTERN "${APP_ICON_NAME}*.png"
)

# Router and the other modes change NetworkManager's settings, which apps
# started by the launcher may not do on their own: this polkit rule grants
# the netdev group the few actions they need (see the file).
if(APP_USE_LIBNM)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/polkit/60-${APP_PACKAGE_NAME}.rules"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/polkit-1/rules.d"
    )
endif()

install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications"
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
    )
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "third-party-assets-license.txt"
)
# LVGL is linked into the program, with the QR code encoder it brings for
# the Hotspot page's join code.
install(FILES "${LVGL_SOURCE_DIR}/LICENCE.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "lvgl-license.txt"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/third-party/qrcodegen-license.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
# Keep CPack's scratch files in the build tree, wherever cpack is started from.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CPACK_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "${APP_MAINTAINER}")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${APP_PACKAGE_NAME}_${PROJECT_VERSION}-${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_DEBIAN_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# Dependencies: a native build on Debian (the device itself, or the container
# used by scripts/build-deb.sh) has the target's package database, so
# dpkg-shlibdeps reads the real dependencies and minimum versions from the
# finished program. A cross build has no package database for the target, and
# the desktop build's program is not an arm64 Linux binary, so both keep the
# hand-written list below.
#
# Router uses NetworkManager's shared mode, which runs dnsmasq (addresses
# for the hotspot's devices) and nft (sharing the link). Neither is a
# library, so they are named by hand on both routes.
set(APP_DEBIAN_TOOL_DEPENDS "")
if(APP_USE_LIBNM)
    set(APP_DEBIAN_TOOL_DEPENDS "dnsmasq-base, nftables")
endif()
if(NOT USE_DESKTOP AND NOT CMAKE_CROSSCOMPILING)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # CPack adds the libraries dpkg-shlibdeps finds to this list.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${APP_DEBIAN_TOOL_DEPENDS}")
else()
    set(APP_DEBIAN_DEPENDS "libc6, libstdc++6, libgcc-s1, libfreetype6, libpng16-16, libjpeg62-turbo, zlib1g")
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        string(APPEND APP_DEBIAN_DEPENDS ", libfmt10")
    endif()
    if(APP_USE_LIBNM)
        string(APPEND APP_DEBIAN_DEPENDS ", libnm0, libglib2.0-0t64, ${APP_DEBIAN_TOOL_DEPENDS}")
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${APP_DEBIAN_DEPENDS}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
endif()
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/conffiles.in"
    "${APP_GENERATED_DIR}/conffiles"
    @ONLY
)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${APP_GENERATED_DIR}/conffiles")

include(CPack)
