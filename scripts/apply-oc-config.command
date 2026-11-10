#!/bin/bash
# Install ~/m8/config.plist (and ~/m8/RembrandtGfx, if present) onto the OpenCore ESP (disk0s1),
# turn off hibernation. Backups: config-prev.plist, RembrandtGfx.prev. Double-click or run in Terminal.
set -e
NEW="$HOME/m8/config.plist"; KEXT="$HOME/m8/RembrandtGfx"
[ -f "$NEW" ] || { echo "missing $NEW"; read -p "Enter to close"; exit 1; }
plutil -lint "$NEW"
sudo -v
sudo diskutil mount disk0s1 >/dev/null
MP=$(diskutil info disk0s1 | awk -F": *" "/Mount Point/{print \$2}")
sudo cp "$MP/EFI/OC/config.plist" "$MP/EFI/OC/config-prev.plist"
sudo cp "$NEW" "$MP/EFI/OC/config.plist"
if [ -f "$KEXT" ]; then
  B="$MP/EFI/OC/Kexts/RembrandtGfx.kext/Contents/MacOS"
  sudo cp "$B/RembrandtGfx" "$MP/EFI/OC/RembrandtGfx.prev"
  sudo cp "$KEXT" "$B/RembrandtGfx"
  echo "RembrandtGfx updated (backup: EFI/OC/RembrandtGfx.prev)"
  mv "$KEXT" "$KEXT.installed"
fi
sync
sudo diskutil unmount force disk0s1 >/dev/null
sudo pmset -a hibernatemode 0 standby 0 autopoweroff 0
echo "Done: OpenCore config updated (backup: config-prev.plist), hibernation off."
read -p "Reboot now? [y/N] " a
[ "$a" = y ] && sudo shutdown -r now
