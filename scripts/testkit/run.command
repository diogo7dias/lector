#!/bin/bash
# Lector test kit — flash a device over USB and record everything it says.
#
# Double-click this file. It flashes the firmware in this folder, then keeps
# recording the device's serial output until you press Return. The log lands in
# ~/Downloads and is the file to send back.
set -u

cd "$(dirname "$0")" || exit 1
KIT_DIR="$(pwd)"
source "$KIT_DIR/kit.env"

LOG_DIR="$HOME/Downloads"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="$LOG_DIR/lector-test-${KIT_VERSION_SLUG}-${STAMP}.log"
CAPTURE_LIMIT_SECONDS=900

say() { printf '%s\n' "$*"; }
log_line() { printf '%s\n' "$*" >> "$LOG"; }

say "Lector test kit ${KIT_VERSION} (${KIT_COMMIT})"
say "Log: $LOG"
say ""

# --- pick the flashing tool ------------------------------------------------
ARCH="$(uname -m)"
ESPTOOL="$KIT_DIR/tools/esptool-macos-arm64"
if [ "$ARCH" != "arm64" ] || [ ! -x "$ESPTOOL" ]; then
  # The bundled binary is Apple Silicon only. On an Intel Mac fall back to any
  # esptool already installed rather than failing outright.
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL="$(command -v esptool)"
  elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL="$(command -v esptool.py)"
  else
    say "No esptool for this Mac (arch: $ARCH)."
    say "Install one with:  python3 -m pip install --user esptool"
    say "Then double-click this file again."
    read -r -p "Press Return to close. " _
    exit 1
  fi
fi
chmod +x "$KIT_DIR/tools/esptool-macos-arm64" 2>/dev/null

# --- pick the serial port --------------------------------------------------
# macOS exposes a USB serial device twice (tty.* and cu.*); cu.* is the one that
# does not wait for carrier detect, which is what a reader board needs.
PORTS=()
while IFS= read -r p; do PORTS+=("$p"); done < <(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null)

if [ "${#PORTS[@]}" -eq 0 ]; then
  say "No device found. Plug the reader in with a data cable (not charge-only), then run this again."
  read -r -p "Press Return to close. " _
  exit 1
elif [ "${#PORTS[@]}" -eq 1 ]; then
  PORT="${PORTS[0]}"
else
  say "More than one device is plugged in:"
  i=1
  for p in "${PORTS[@]}"; do say "  $i) $p"; i=$((i + 1)); done
  read -r -p "Which one? [1] " choice
  choice="${choice:-1}"
  PORT="${PORTS[$((choice - 1))]}"
fi
say "Device: $PORT"

# --- header ----------------------------------------------------------------
{
  echo "=== LECTOR TEST LOG ==="
  echo "kit version:   $KIT_VERSION"
  echo "kit commit:    $KIT_COMMIT"
  echo "kit built:     $KIT_BUILT"
  echo "flashed at:    $(date '+%Y-%m-%d %H:%M:%S %z')"
  echo "mac:           $(sw_vers -productName 2>/dev/null) $(sw_vers -productVersion 2>/dev/null) ($ARCH)"
  echo "port:          $PORT"
  echo "esptool:       $ESPTOOL"
  echo
  echo "=== FLASH ==="
} > "$LOG"

say ""
# Flash mode and frequency come from the image header, which PlatformIO wrote
# for this board. Hardcoding dio would silently downgrade a qio board (the
# X4 Pro is qio_opi) and skew every timing this kit exists to measure.
say "Flashing. Do not unplug."

# --connect-attempts: the X4 Pro speaks over USB-Serial/JTAG, so esptool resets it
# over RTS and the reset does not always take on the first try — "No serial data
# received" with the board sitting there perfectly healthy. Letting esptool retry
# the handshake itself costs nothing and clears it most times.
flash_once() {
  "$ESPTOOL" --chip "$KIT_CHIP" --port "$1" --baud "$KIT_BAUD" --connect-attempts 5 \
    write-flash -z --flash-mode keep --flash-freq keep --flash-size "$KIT_FLASH_SIZE" \
    0x0 firmware/bootloader.bin \
    0x8000 firmware/partitions.bin \
    0xe000 firmware/boot_app0.bin \
    0x10000 firmware/firmware.bin 2>&1 | tee -a "$LOG"
  return "${PIPESTATUS[0]}"
}

# esptool v5 renamed the subcommands; the old spelling is kept for an Intel Mac
# running an older pip install.
flash_once_legacy() {
  "$ESPTOOL" --chip "$KIT_CHIP" --port "$1" --baud "$KIT_BAUD" \
    write_flash -z --flash_mode keep --flash_freq keep --flash_size "$KIT_FLASH_SIZE" \
    0x0 firmware/bootloader.bin \
    0x8000 firmware/partitions.bin \
    0xe000 firmware/boot_app0.bin \
    0x10000 firmware/firmware.bin 2>&1 | tee -a "$LOG"
  return "${PIPESTATUS[0]}"
}

FLASH_RC=1
for round in 1 2 3; do
  # The port can be renamed between rounds when the board re-enumerates, so it is
  # looked up again each time rather than trusted from the first scan.
  found="$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null | head -1)"
  [ -n "$found" ] && PORT="$found"
  [ "$round" -gt 1 ] && say "Attempt $round of 3 on $PORT."
  flash_once "$PORT"
  FLASH_RC=$?
  [ "$FLASH_RC" -eq 0 ] && break
  flash_once_legacy "$PORT"
  FLASH_RC=$?
  [ "$FLASH_RC" -eq 0 ] && break
  [ "$round" -lt 3 ] && sleep 3
done

log_line ""
log_line "flash exit code: $FLASH_RC"

if [ "$FLASH_RC" -ne 0 ]; then
  say ""
  say "Flashing failed after three attempts. The log has the reason: $LOG"
  say "If it says \"No serial data received\": unplug the reader, plug it back in,"
  say "and run the command again straight away."
  read -r -p "Press Return to close. " _
  exit 1
fi

# --- capture ---------------------------------------------------------------
log_line ""
log_line "=== SERIAL ==="

say ""
say "Flashed. Recording the device now, across resets and sleeps."
say "Run your checks on the reader. Press Return here when you are done."
say "(recording stops on its own after $((CAPTURE_LIMIT_SECONDS / 60)) minutes)"
say ""

# The board is an ESP32-C3 speaking over USB-Serial/JTAG, so its serial port is
# created by the firmware itself: every reset, sleep, and silent restart makes
# /dev/cu.* disappear and come back a second or two later. A single `cat` catches
# the first boot and then quietly stops at EOF, which is how the first kit lost
# everything after 512 ms. So: reopen, in a loop, until the person says stop.
# The board is an ESP32-C3 on USB-Serial/JTAG: the serial port is created by the
# firmware, so it disappears and comes back on every reset, sleep, and restart.
# Worse, when the device re-enumerates, the old file handle can stay open without
# ever returning EOF, so a plain `cat` sits on a dead port forever. That is how
# the first two kits recorded only the first 512 ms. So the port is watched, and
# the reader is restarted whenever the device node changes or goes quiet.
port_id() {
  # Device and inode numbers: they change when macOS recreates the node.
  stat -f '%d:%i' "$PORT" 2>/dev/null || echo "gone"
}

find_port() {
  ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null | head -1
}

start_reader() {
  stty -f "$PORT" "$KIT_BAUD" raw -echo -hupcl 2>/dev/null
  cat "$PORT" >> "$LOG" 2>/dev/null &
  READER_PID=$!
  READER_ID="$(port_id)"
}

stop_reader() {
  [ -n "${READER_PID:-}" ] && kill "$READER_PID" 2>/dev/null
  READER_PID=""
}

capture_loop() {
  local last_size=0
  local quiet=0
  start_reader
  while :; do
    sleep 1
    local now_id
    now_id="$(port_id)"
    local size
    size="$(wc -c < "$LOG" 2>/dev/null | tr -d ' ')"

    if [ "$size" != "$last_size" ]; then
      last_size="$size"
      quiet=0
    else
      quiet=$((quiet + 1))
    fi

    # Reattach when the node changed (device reset) or nothing has arrived for
    # 5 s (the handle is stale, or the device slept and came back). Five, not twenty:
    # on the X4 Pro the handle goes stale far more often than the node changes, and at
    # twenty seconds whole minutes of a test session were lost between reattaches.
    if [ "$now_id" != "$READER_ID" ] || [ "$quiet" -ge 5 ]; then
      stop_reader
      local found
      found="$(find_port)"
      if [ -n "$found" ]; then
        [ "$found" != "$PORT" ] && printf '\n--- port is now %s ---\n' "$found" >> "$LOG"
        PORT="$found"
        printf '\n--- reattached %s ---\n' "$(date '+%H:%M:%S')" >> "$LOG"
        start_reader
      fi
      quiet=0
      last_size="$(wc -c < "$LOG" 2>/dev/null | tr -d ' ')"
    fi
  done
}

# esptool resets the board over RTS, which makes the port vanish and come back.
# Wait for that before recording, or the first handle is dead on arrival.
sleep 2
for _ in $(seq 1 40); do
  [ -e "$PORT" ] && break
  found="$(find_port)"
  [ -n "$found" ] && PORT="$found" && break
  sleep 0.5
done

capture_loop &
CAT_PID=$!

# Stop on its own if the person walks away, so the log always closes cleanly.
( sleep "$CAPTURE_LIMIT_SECONDS"; kill "$CAT_PID" 2>/dev/null ) &
TIMER_PID=$!

read -r _
pkill -P "$CAT_PID" 2>/dev/null
kill "$CAT_PID" 2>/dev/null
kill "$TIMER_PID" 2>/dev/null
wait "$CAT_PID" 2>/dev/null

log_line ""
log_line "=== END $(date '+%Y-%m-%d %H:%M:%S %z') ==="

LINES="$(wc -l < "$LOG" | tr -d ' ')"
say ""
say "Done. $LINES lines written to:"
say "  $LOG"
say ""
say "Send that file back."

# The kit has done its job and the log lives in ~/Downloads, not in here. Clean
# up so the only thing left behind is the log. Guarded: only a directory under
# $HOME that still holds this kit's own kit.env is ever removed.
case "$KIT_DIR" in
  "$HOME"/*)
    if [ -f "$KIT_DIR/kit.env" ] && [ -f "$KIT_DIR/run.command" ]; then
      cd "$HOME" || exit 0
      rm -rf "$KIT_DIR"
      say "Kit folder removed."
    fi
    ;;
esac

read -r -p "Press Return to close. " _
