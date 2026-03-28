#!/bin/bash
set -e

TAMA_COUNT=${TAMA_COUNT:-9}
SAVE_INTERVAL=${SAVE_INTERVAL:-3600}
STATE_DIR=${STATE_DIR:-/app/state}
RUNTIME_ROMS=/tmp/roms

mkdir -p "$RUNTIME_ROMS" "$STATE_DIR"

# Restore saved state if available, otherwise stamp out fresh EEPROMs
bases=(/app/roms/*.eep)
for i in $(seq 1 "$TAMA_COUNT"); do
  saved="$STATE_DIR/tama-$i.eep"
  runtime="$RUNTIME_ROMS/tama-$i.eep"
  if [ -f "$saved" ]; then
    cp "$saved" "$runtime"
    echo "Restored tama-$i from saved state"
  else
    base="${bases[$((RANDOM % ${#bases[@]}))]}"
    cp "$base" "$runtime"
    echo "Created fresh tama-$i"
  fi
done

# Periodically save runtime EEPROM state to persistent storage
save_state() {
  while true; do
    sleep "$SAVE_INTERVAL"
    for f in "$RUNTIME_ROMS"/*.eep; do
      cp "$f" "$STATE_DIR/$(basename "$f")"
    done
    echo "State saved to $STATE_DIR"
  done
}
save_state &

# Start an emulator for each ROM, staggered 1s apart so each instance gets a
# unique srand(time(NULL)) seed and different periodic-action timing.
delay=0
for f in "$RUNTIME_ROMS"/*.eep; do
  (sleep "$delay" && cd /app/emu && exec ./tamaemu -e "$f" >/dev/null 2>&1) &
  delay=$((delay + 1))
done

# Save state on shutdown before exiting
cleanup() {
  echo "Saving state before exit..."
  for f in "$RUNTIME_ROMS"/*.eep; do
    cp "$f" "$STATE_DIR/$(basename "$f")"
  done
  echo "State saved. Exiting."
  kill 0
}
trap cleanup SIGTERM SIGINT

# Start the Go web server (keeps container alive)
/app/webserver -static /var/www/html -listen :80 &
wait
