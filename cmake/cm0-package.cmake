#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
# Packaging and install layout for CardputerZero Debian builds.

include(GNUInstallDirs)

# APP_DISPLAY_NAME, APP_PACKAGE_NAME and APP_PACKAGE_DESCRIPTION come from the identity block in CMakeLists.txt.
set(APP_DEBIAN_REVISION "1" CACHE STRING "Debian package revision (the N in <version>-N)")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
# The package's Maintainer comes from the LOKICTL_MAINTAINER environment
# variable ("Name <e-mail>"), read again on every configure, so it never
# has to be written into the repository; scripts/build-deb.sh passes it on.
# Without it the neutral default below is used. The store wants the
# publisher's GitHub e-mail or noreply address here, so set it for a store
# build. (Older build folders cached a value; it is dropped.)
unset(APP_MAINTAINER CACHE)
set(APP_MAINTAINER "$ENV{LOKICTL_MAINTAINER}")
if(APP_MAINTAINER STREQUAL "")
    set(APP_MAINTAINER "${APP_DISPLAY_NAME} contributors <maintainer@${APP_PACKAGE_NAME}.invalid>")
endif()
# The launcher starts LokiCTL when you open it, so the package installs no
# systemd unit: nothing of LokiCTL runs in the background or at boot. (This
# drops the template's old switch for one from older build folders.)
unset(APP_INSTALL_SYSTEMD_SERVICE CACHE)

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The firewall helper and the capture helper (device builds only). They are
# installed for root alone; postinst gives them to the netdev group and each
# its one capability (see templates/postinst.in).
if(TARGET ${APP_HELPER_NAME})
    install(TARGETS ${APP_HELPER_NAME}
        RUNTIME DESTINATION ${APP_HELPER_INSTALL_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
endif()
if(TARGET ${APP_CAPTURE_NAME})
    install(TARGETS ${APP_CAPTURE_NAME}
        RUNTIME DESTINATION ${APP_HELPER_INSTALL_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/${APP_CONFIG_NAME}.conf"
    DESTINATION "/etc"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
    PATTERN ".DS_Store" EXCLUDE
)
# Only the app icons: the simulator's device frames (cp0-*.png) and the
# icon drawing (lokictl.svg) stay out of the package.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
    FILES_MATCHING
    PATTERN "${APP_ICON_NAME}*.png"
)
# The web dashboard's page, style, scripts and icon, which Remote view
# serves (the fonts come from the fonts folder above).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/web/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/web"
    PATTERN ".DS_Store" EXCLUDE
)
# The list of known security holes and outdated versions the Ports page
# checks against (made by scripts/vulndb/build.py).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/data"
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
# The parts LVGL brings with it that the build compiles in: the TLSF
# allocator, printf, the Montserrat fonts with their Font Awesome symbols
# and the JPEG decoder.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/third-party/lvgl-bundled.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/third-party/vulnerability-data.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
# Preact, the web dashboard's page library (assets/web/preact.module.js).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/third-party/preact-license.txt"
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
# for the hotspot's devices) and nft (sharing the link). The firewall
# helper runs nft too, and iw (adding Share's interface), and postinst
# gives it its capability with setcap (libcap2-bin). None of them is a
# library, so they are named by hand on both routes.
set(APP_DEBIAN_TOOL_DEPENDS "")
if(APP_USE_LIBNM)
    set(APP_DEBIAN_TOOL_DEPENDS "dnsmasq-base, nftables")
endif()
if(TARGET ${APP_HELPER_NAME})
    if(APP_DEBIAN_TOOL_DEPENDS)
        string(APPEND APP_DEBIAN_TOOL_DEPENDS ", libcap2-bin, iw")
    else()
        set(APP_DEBIAN_TOOL_DEPENDS "nftables, libcap2-bin, iw")
    endif()
endif()
# The speed test loads libcurl with dlopen() (dpkg-shlibdeps cannot see
# that) and checks the server's certificate against the system's
# certificates.
if(APP_USE_CURL)
    if(APP_DEBIAN_TOOL_DEPENDS)
        string(APPEND APP_DEBIAN_TOOL_DEPENDS ", libcurl4t64, ca-certificates")
    else()
        set(APP_DEBIAN_TOOL_DEPENDS "libcurl4t64, ca-certificates")
    endif()
endif()
# The LAN speed test runs iperf3 as a child process, with --json-stream
# (iperf3 3.17 or later). Debian's package asks whether to start its own
# server at boot; its answer defaults to no, and LokiCTL never changes it.
if(APP_DEBIAN_TOOL_DEPENDS)
    string(APPEND APP_DEBIAN_TOOL_DEPENDS ", iperf3 (>= 3.17)")
else()
    set(APP_DEBIAN_TOOL_DEPENDS "iperf3 (>= 3.17)")
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
        string(APPEND APP_DEBIAN_DEPENDS ", libnm0, libglib2.0-0t64")
    endif()
    if(APP_DEBIAN_TOOL_DEPENDS)
        string(APPEND APP_DEBIAN_DEPENDS ", ${APP_DEBIAN_TOOL_DEPENDS}")
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
# With the helpers: postinst gives each its capability, prerm removes the
# firewall table on removal. Strict permissions want them executable.
if(TARGET ${APP_HELPER_NAME})
    foreach(script IN ITEMS postinst prerm)
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/templates/${script}.in"
            "${APP_GENERATED_DIR}/${script}"
            @ONLY
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
        list(APPEND CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${APP_GENERATED_DIR}/${script}")
    endforeach()
endif()

include(CPack)
