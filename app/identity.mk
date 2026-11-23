# The application's name and title ID, in one place: app/Makefile (the .3dsx, the SMDH, APP_NAME in the code) and
# the top Makefile (the CIA through makerom and app/meta/app.rsf) both include this file.
#
# Title ID 000400000FA5A000: unique ID 0xFA5A0, the hex digit F followed by the first four hex digits of the
# SHA-256 of "3DStockTheme"; no app in Universal-DB uses it and it is no retail unique ID in that list (README).
# Changing the name later does not change the title ID, so an installed copy is replaced, not doubled.
APP_TITLE         := 3DStockTheme
APP_DESCRIPTION   := Install themes into the HOME Menu theme pack
APP_AUTHOR        := console-tools
APP_UNIQUE_ID     := 0xFA5A0
APP_PRODUCT_CODE  := CTR-P-STKT
APP_VERSION_MAJOR := 1
APP_VERSION_MINOR := 0
APP_VERSION_MICRO := 0
# The pre-release mark after a hyphen (semantic versioning: 1.0.0-beta.2 sorts before 1.0.0), empty for a release. The
# application shows it with the numbers; the CIA's version field takes numbers only.
APP_VERSION_PRE   := beta.2
