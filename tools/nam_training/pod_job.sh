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
# everything it finished. Finished .nam files and a status line (status/status_<pod>.txt) go up every 4 minutes.

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
# what each training is doing right now (read by train_amp.ps1 on the PC): "CH1_CLEAN_G08 epoch 37/400 | ..."
status() {
    # per training: what it trains, its epoch, and how long it still needs (from its own epoch speed so far:
    # the rest of this recording + the recordings still waiting in its share); the machine needs the longest one
    local line="" f name ep pos now t0 e0 rate left worst=0 k
    now=$(date +%s)
    for f in train_part*.txt; do
        [ -f "$f" ] || continue
        k=${f#train_part}; k=${k%.txt}
        name=$(grep -o 'training [^ ]* (' "$f" | tail -1 | sed 's/training //; s/ (//')
        pos=$(grep -o '^\[[0-9]*/[0-9]*\] training' "$f" | tail -1 | tr -d '[]' | awk -F'[/ ]' '{print $1, $2}')
        ep=$(tr '\r' '\n' < "$f" | grep -o 'Epoch [0-9]*/[0-9]*' | tail -1 | awk -F'[ /]' '{print $2+1}')
        [ -n "$name" ] || continue
        ep=${ep:-0}
        # speed: epochs since this part's first measurement (kept in .speed_<k>, restarted with each recording)
        if [ ! -f ".speed_$k" ] || [ "$(cut -d' ' -f1 ".speed_$k")" != "$name" ] || [ "$ep" -lt "$(cut -d' ' -f3 ".speed_$k")" ]; then
            echo "$name $now $ep" > ".speed_$k"
        fi
        read -r _ t0 e0 < ".speed_$k"
        left=""
        if [ "$ep" -gt "$e0" ] && [ "$now" -gt "$t0" ]; then
            rate=$(awk -v d="$(( ep - e0 ))" -v t="$(( now - t0 ))" 'BEGIN{print d/t}')   # epochs per second
            set -- $pos
            left=$(awk -v r="$rate" -v e="$ep" -v E="$EPOCHS" -v i="${1:-1}" -v n="${2:-1}" \
                'BEGIN{printf "%d", ((E - e) + (n - i) * E) / r}')
            [ "$left" -gt "$worst" ] && worst=$left
        fi
        line="$line${line:+ | }$name epoch $ep/$EPOCHS"
    done
    [ -n "$line" ] || line="${1:-starting}"
    [ "$worst" -gt 0 ] && line="$line - $(( worst / 3600 )) h $(( worst % 3600 / 60 )) min left"
    echo "$line" > "status_${RUNPOD_POD_ID:-pod}.txt"   # (no time: the machine runs on UTC, the PC adds its own)
    echo "$worst" > "eta_${RUNPOD_POD_ID:-pod}.txt"
    rclone copyto "status_${RUNPOD_POD_ID:-pod}.txt" "$OUT/status/status_${RUNPOD_POD_ID:-pod}.txt" > /dev/null 2>&1
    rclone copyto "eta_${RUNPOD_POD_ID:-pod}.txt" "$OUT/status/eta_${RUNPOD_POD_ID:-pod}.txt" > /dev/null 2>&1
}

# safety limit: whatever happens, after MAX_HOURS upload what is there and switch off
# (the training stops, the normal end below uploads and switches off; if that hangs, this does it 15 minutes later)
( sleep "$(( ${MAX_HOURS:-8} * 3600 ))"; say "SAFETY LIMIT of ${MAX_HOURS:-8} h reached"; pkill -9 -f train_batch.py
  sleep 900; upload; switch_off ) &
LIMIT_PID=$!

status "installing"
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
( sleep 60; while true; do status; upload; sleep 240; done ) &
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
status "finished - switching off"
for f in train_part*.txt; do tail -n 40 "$f"; done
cat nams/summary*.txt 2> /dev/null
say "$(rclone lsf "$OUT" --include "*.nam" | wc -l) .nam files are on Google Drive in AMPSURD training/$AMP/NAMs"
kill "$LIMIT_PID" 2> /dev/null
switch_off
