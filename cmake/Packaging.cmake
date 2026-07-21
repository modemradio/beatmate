# Packaging.cmake - CPack configuration for installer generation

set(CPACK_PACKAGE_NAME "BeatMateV11")
set(CPACK_PACKAGE_VENDOR "BeatMate")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BeatMate V11 - Professional DJ Application")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "BeatMateV11")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/docs/LICENSE.txt")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "BeatMate V11 Professional")

    # Multi-user / multi-PC deployment policy:
    #   - The installer offers a choice: per-machine (Program Files, requires
    #     admin, shared by all Windows users of the PC) OR per-user
    #     (%LOCALAPPDATA%\Programs, no admin required, private to the user
    #     running the setup).
    #   - User data (DB, playlists, settings, recordings, stems) ALWAYS lives
    #     under %APPDATA%\BeatMate\ per Windows user — already handled in
    #     Application.cpp via userApplicationDataDirectory. Safe on any setup.
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    # Let the installer prompt "Install for all users" vs "Install for me only"
    # (NSIS MultiUser plugin semantics via CPack options).
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "BeatMateV11.exe")

    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\BeatMate V11.lnk' '$INSTDIR\\\\bin\\\\BeatMateV11.exe'\n  CreateShortCut '$DESKTOP\\\\BeatMate V11.lnk' '$INSTDIR\\\\bin\\\\BeatMateV11.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU\\\\BeatMate V11.lnk'\n  Delete '$DESKTOP\\\\BeatMate V11.lnk'"
    )

    # Component labels so the install wizard is clearer.
    set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)
    set(CPACK_PACKAGE_FILE_NAME "BeatMateV11-${PROJECT_VERSION}-win64")
endif()

include(CPack)
