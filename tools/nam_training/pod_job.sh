#!/bin/bash
# AMPSURD - what a rented RunPod machine does on its own (started by train_amp.ps1 on the owner's PC).
#
# Environment (set by train_amp.ps1 when it creates the machine):
#   AMP          the amp's folder name on Google Drive ("AMPSURD training/<AMP>")
#   PART_FROM, PART_TO, PARTS   this machine trains parts PART_FROM..PART_TO of PARTS (one training per part, side by side)
#   EPOCHS       training passes (400)
#   MAX_HOURS    safety limit: after this the machine uploads what it has and switches itself off
#   RCLONE_CONFIG_GDRIVE_TYPE / _SCOPE / _TOKEN   Google Drive access (rclone reads its remote "gdrive" from these)
#   AMPSURD_RUNPOD_KEY   RunPod API key, only used to switch this machine off at the end
#   RUNPOD_POD_ID        set by RunPod
#
# Google Drive layout:
#   AMPSURD training/_input/T3K-sweep-v3.wav       the input (NAM standard v3)
#   AMPSURD training/<AMP>/recordings/*.wav         the amp recordings
#   AMPSURD training/<AMP>/NAMs/                    the results (.nam, summaries, a log per machine)
#
# The machine switches itself off at the end in every case (done, failed, or the safety limit) - after uploading
# everything it finished. Finished .nam files are also uploaded every 10 minutes while it trains.

set -u
RAW="https://raw.githubusercontent.com/SymbiOrbis/ampsurd/main/tools/nam_training"
ROOT="gdrive:AMPSURD training"
OUT="$ROOT/$AMP/NAMs"
W=/workspace/job
mkdir -p "$W/recordings" "$W/nams"
cd "$W" || exit 1
LOG="$W/log_${RUNPOD_POD_ID:-pod}.txt"
exec > >(tee -a "$LOG") 2>&1
say() { echo "$(date '+%H:%M:%S') $*"; }

switch_off() {
    say "switching this machine off"
    rclone copy "$LOG" "$OUT/logs" > /dev/null 2>&1
    if [ -n "${AMPSURD_RUNPOD_KEY:-}" ] && [ -n "${RUNPOD_POD_ID:-}" ]; then
        for i in 1 2 3; do
            CODE=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE -H "Authorization: Bearer $AMPSURD_RUNPOD_KEY" \
                "https://rest.runpod.io/v1/pods/$RUNPOD_POD_ID")
            case "$CODE" in 2*) exit 0;; esac
            sleep 20
        done
        curl -s -o /dev/null -X POST -H "Authorization: Bearer $AMPSURD_RUNPOD_KEY" "https://rest.runpod.io/v1/pods/$RUNPOD_POD_ID/stop"
    fi
    exit 0
}
upload() { rclone copy nams "$OUT" --include "*.nam" --include "summary*.txt" --ignore-existing --retries 5 > /dev/null 2>&1; }

# safety limit: whatever happens, after MAX_HOURS upload what is there and switch off
# (the training stops, the normal end below uploads and switches off; if that hangs, this does it 15 minutes later)
( sleep "$(( ${MAX_HOURS:-8} * 3600 ))"; say "SAFETY LIMIT of ${MAX_HOURS:-8} h reached"; pkill -9 -f train_batch.py
  sleep 900; upload; switch_off ) &
LIMIT_PID=$!

say "AMPSURD training machine: amp '$AMP', parts $PART_FROM-$PART_TO of $PARTS, $EPOCHS epochs, limit ${MAX_HOURS:-8} h"

# --- tools ---
say "installing the trainer and rclone ..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq > /dev/null 2>&1
apt-get install -y -qq python3-tk unzip curl > /dev/null 2>&1
curl -fsSL https://rclone.org/install.sh | bash > /dev/null 2>&1
pip install -q neural-amp-modeler==0.13.0 > /dev/null 2>&1
curl -fsSL "$RAW/train_batch.py" -o train_batch.py
command -v rclone > /dev/null && python -c "import nam" 2> /dev/null || { say "INSTALL FAILED"; switch_off; }

# --- files ---
say "downloading this machine's recordings from Google Drive ..."
rclone copy "$ROOT/_input/T3K-sweep-v3.wav" . || { say "no input file on Google Drive (AMPSURD training/_input)"; switch_off; }
rclone lsf "$ROOT/$AMP/recordings" --include "*.wav" | sort > all.txt
N=$(wc -l < all.txt)
[ "$N" -gt 0 ] || { say "no recordings on Google Drive (AMPSURD training/$AMP/recordings)"; switch_off; }
# the same split as train_batch.py --part p --parts PARTS (every PARTS-th file, starting with file p)
: > mine.txt
for p in $(seq "$PART_FROM" "$PART_TO"); do awk -v p="$p" -v n="$PARTS" '(NR - p) % n == 0 && NR >= p' all.txt >> mine.txt; done
rclone copy "$ROOT/$AMP/recordings" recordings --files-from mine.txt
# already finished ones (an earlier run) are not trained again
rclone copy "$OUT" nams --include "*.nam" > /dev/null 2>&1
say "$(wc -l < mine.txt) of $N recordings are this machine's; $(ls nams/*.nam 2> /dev/null | wc -l) .nam already finished"

# --- training: one per part, side by side; finished .nam files go up every 10 minutes ---
( while true; do sleep 600; upload; done ) &
UP_PID=$!
# this machine holds only its own recordings: split them between its trainings (1..L)
L=$(( PART_TO - PART_FROM + 1 ))
PIDS=()
for k in $(seq 1 "$L"); do
    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs "$EPOCHS" \
        --part "$k" --parts "$L" > "train_part$k.txt" 2>&1 &
    PIDS+=($!)
done
wait "${PIDS[@]}"
kill "$UP_PID" 2> /dev/null

# --- results ---
upload
for f in train_part*.txt; do tail -n 40 "$f"; done
cat nams/summary*.txt 2> /dev/null
say "$(rclone lsf "$OUT" --include "*.nam" | wc -l) .nam files are on Google Drive in AMPSURD training/$AMP/NAMs"
kill "$LIMIT_PID" 2> /dev/null
switch_off
