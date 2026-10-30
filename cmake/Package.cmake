# What the release downloads contain (PRISM_RELEASE only): `cmake --install` into a folder that is
# then zipped (packaging/macos/package.sh, packaging/windows/package.ps1).
#
#   macOS:   Prism.app, prism
#   Windows: prism_gui.exe, prism.exe
#   both:    LICENSE, THIRD_PARTY_NOTICES.txt, README.txt,
#            licenses/LibRaw/LICENSE.CDDL and COPYRIGHT
#
# LibRaw's two files are installed from the source tree the build compiled, byte for byte.

install(TARGETS prism RUNTIME DESTINATION .)
if(PRISM_BUILD_GUI)
  install(TARGETS prism_gui RUNTIME DESTINATION . BUNDLE DESTINATION .)
endif()
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PRISM_THIRD_PARTY_NOTICES} DESTINATION .)
install(FILES ${libraw_SOURCE_DIR}/LICENSE.CDDL ${libraw_SOURCE_DIR}/COPYRIGHT
        DESTINATION licenses/LibRaw)

string(LENGTH "Prism ${PROJECT_VERSION}" prism_length)
string(REPEAT "=" ${prism_length} PRISM_README_UNDERLINE)
if(APPLE)
  set(PRISM_README_PLATFORM "For macOS ${CMAKE_OSX_DEPLOYMENT_TARGET} or later")
  set(PRISM_README_FILES
"  Prism.app                 the app: drag it to Applications
  prism                     the command-line tool")
  set(PRISM_README_FIRST_RUN
"Prism isn't signed with an Apple Developer ID, so macOS blocks it the first time.
On macOS 14 and earlier, right-click (or Control-click) Prism.app, choose Open, then
click Open again. On macOS 15 and later, try to open it, then go to System Settings >
Privacy & Security, scroll down to the message about Prism and click Open Anyway.
After that it opens normally.")
  set(PRISM_README_CLI "./prism")
elseif(WIN32)
  set(PRISM_README_PLATFORM "For Windows 10 and 11, 64-bit")
  set(PRISM_README_FILES
"  prism_gui.exe             the app
  prism.exe                 the command-line tool")
  set(PRISM_README_FIRST_RUN
"Extract the whole zip first. Prism isn't signed, so Microsoft Defender SmartScreen may
say \"Windows protected your PC\" the first time: click More info, then Run anyway.
If Smart App Control is on (Windows 11), it blocks unsigned apps and there is no way
to allow just Prism.")
  set(PRISM_README_CLI "prism.exe")
else()
  set(PRISM_README_PLATFORM "Built for ${CMAKE_SYSTEM_NAME}")
  set(PRISM_README_FILES
"  prism_gui                 the app
  prism                     the command-line tool")
  set(PRISM_README_FIRST_RUN "Run prism_gui.")
  set(PRISM_README_CLI "./prism")
endif()
configure_file(${PROJECT_SOURCE_DIR}/packaging/README.txt.in ${PROJECT_BINARY_DIR}/README.txt @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/README.txt DESTINATION .)

# For the release workflow and the packaging scripts: the versions and the LibRaw tarball, as
# KEY=value lines (they go straight into $GITHUB_ENV).
file(WRITE ${PROJECT_BINARY_DIR}/release.env
"PRISM_VERSION=${PROJECT_VERSION}
LIBRAW_VERSION=${PRISM_LIBRAW_VERSION}
LIBRAW_SHA256=${PRISM_LIBRAW_SHA256}
LIBRAW_TARBALL=${PRISM_LIBRAW_TARBALL}
")
