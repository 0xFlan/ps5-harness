# Single source of truth for the package identity.
#
# Nine characters: four uppercase letters, five digits. PS5H00001 is a
# homebrew-range ID chosen for this project; it is not a Sony title ID and does
# not collide with the PPSA/CUSA retail ranges. Change it here and everything
# derived from it (param.sfo, param.json, the payload log, the package folder)
# follows.

TITLE_ID   := PHAR00001
TITLE      := PS5 Harness
CONTENT_ID := UP9000-$(TITLE_ID)_00-PS5HARNESS000001
APP_VER    := 01.00
