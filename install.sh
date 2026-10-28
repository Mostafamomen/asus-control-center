#!/bin/bash
# ASUS Control Center — install the two system files that enable M2
# hardware controls. Requires your password ONCE (sudo). Reversible with
# "uninstall" argument. Everything else stays user-level.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

install_helper() {
  echo "-> installing /usr/local/bin/acc-helper (whitelist-only helper)"
  sudo install -m 0755 "$DIR/data/acc-helper" /usr/local/bin/acc-helper
  echo "-> installing polkit policy"
  sudo install -m 0644 "$DIR/data/io.github.asuscontrolcenter.AsusControlCenter.policy" \
       /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy

  echo "-> enabling asusd (ASUS firmware control service)"
  if systemctl is-active --quiet asusd 2>/dev/null; then
    echo "   asusd is already running"
  else
    sudo systemctl start asusd 2>/dev/null && echo "   asusd started" || \
      echo "   WARNING: could not start asusd (is asusd installed?)"
  fi

  echo "-> enabling asus-shutdown (needed for queued GPU MUX changes)"
  sudo systemctl enable --now asus-shutdown.service 2>/dev/null && \
    echo "   asus-shutdown enabled" || \
    echo "   WARNING: could not enable asus-shutdown.service"

  echo "OK — controls are now available. A password prompt may appear the"
  echo "   first time you change a control in each login session."
}

uninstall_helper() {
  sudo rm -f /usr/local/bin/acc-helper \
       /usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy
  echo "OK — helper and policy removed."
  echo "   (asusd and asus-shutdown services left untouched)"
}

case "${1:-install}" in
  install)   install_helper ;;
  uninstall) uninstall_helper ;;
  *) echo "usage: $(basename "$0") [install|uninstall]"; exit 1 ;;
esac
