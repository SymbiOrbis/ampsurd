#!/bin/bash
# AMPSURD - what a rented RunPod machine does on its own (started by train_amp.ps1 on the owner's PC).
#
# Environment (set by train_amp.ps1 when it creates the machine):
#   AMP          the amp's folder name on Google Drive ("AMPSURD training/<AMP>")
#   SLOTS        trainings side by side on this machine (2). Each takes the next untrained recording from Google
#                Drive, trains it, and takes the next one - until none is left (all machines share one queue)
#   EPOCHS       training passes (400)
#   MAX_HOURS    safety limit: after this the machine uploads what it has and switches itself off
#   RCLONE_CONFIG_GDRIVE_TYPE / _SCOPE / _TOKEN   Google Drive access (rclone reads its remote "gdrive" from these)
#   AMPSURD_RUNPOD_KEY   RunPod API key, only used to switch this machine off at the end
#   RUNPOD_POD_ID        set by RunPod
#
# Google Drive layout:
#   AMPSURD training/_input/T3K-sweep-v3.wav       the input (NAM standard v3)
#   AMPSURD training/<AMP>/recordings/*.wav         the amp recordings
#   AMPSURD training/<AMP>/NAMs/                    the results (.nam, summaries/, a log per machine)
#   AMPSURD training/<AMP>/claims/<rec>__<pod>_<k>  "taken" marks: who trains which recording (emptied by the PC
#                                                   before each run, so a run never waits for an old machine)
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
                "https://api.runpod.io/v2/pods/$RUNPOD_POD_ID")
            case "$CODE" in 2*) exit 0;; esac
            sleep 20
        done
        curl -s -o /dev/null -X POST -H "Authorization: Bearer $AMPSURD_RUNPOD_KEY" -H "Content-Type: application/json" \
            -d '{"action":"stop"}' "https://api.runpod.io/v2/pods/$RUNPOD_POD_ID/action"
    fi
    exit 0
}
# only the finished files at the top of nams/ (not the trainer's checkpoints in nams/_work_*)
upload() { rclone copy nams "$OUT" --include "/*.nam" --include "/summaries/*.txt" --ignore-existing --retries 5 > /dev/null 2>&1; }
# what each training is doing right now (read by train_amp.ps1 on the PC): "CH1_CLEAN_G08 epoch 37/400 | ..."
status() {
    # per training: what it trains, its epoch, and how long it still needs (from its own epoch speed so far:
    # the rest of this recording + the recordings still waiting in its share); the machine needs the longest one
    local line="" f name ep pos now t0 e0 rate left worst=0 k per=0 one
    now=$(date +%s)
    for f in train_part*.txt; do
        [ -f "$f" ] || continue
        k=${f#train_part}; k=${k%.txt}
        # (names may contain spaces, e.g. "Boss SD-1 DRIVE": the line is "[i/n] training <name> (<E> epochs) ...")
        name=$(grep -oE 'training .* \([0-9]+ epochs' "$f" | tail -1 | sed -E 's/^training //; s/ \([0-9]+ epochs$//')
        pos=$(grep -o '^\[[0-9]*/[0-9]*\] training' "$f" | tail -1 | tr -d '[]' | awk -F'[/ ]' '{print $1, $2}')
        # epoch: from the progress bar ("Epoch 145: ..." or "Epoch 145/400"; counted from 0), or else from the newest
        # checkpoint the trainer saved (".../checkpoint_best_epoch=0145_...nam") - whichever is further
        ep=$(tr '\r' '\n' < "$f" | grep -oE 'Epoch [0-9]+' | tail -1 | awk '{print $2+1}')
        # (the newest lightning_logs/version_N is the recording being trained now)
        vd=$(ls -d nams/_work_w${k}/lightning_logs/version_* 2> /dev/null | sort -V | tail -1)
        ck=""
        [ -n "$vd" ] && ck=$(ls "$vd/checkpoints/" 2> /dev/null | grep -oE 'epoch=[0-9]+' | cut -d= -f2 | sort -n | tail -1)
        [ -n "$ck" ] && ck=$(( 10#$ck + 1 )) && [ "$ck" -gt "${ep:-0}" ] && ep=$ck
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
            one=$(awk -v r="$rate" -v E="$EPOCHS" 'BEGIN{printf "%d", E / r}')
            [ "$one" -gt "$per" ] && per=$one
        fi
        line="$line${line:+ | }$name epoch $ep/$EPOCHS"
    done
    [ -n "$line" ] || line="${1:-starting}"
    [ "$worst" -gt 0 ] && line="$line - $(( worst / 3600 )) h $(( worst % 3600 / 60 )) min left"
    echo "$line" > "status_${RUNPOD_POD_ID:-pod}.txt"   # (no time: the machine runs on UTC, the PC adds its own)
    echo "$worst $per" > "eta_${RUNPOD_POD_ID:-pod}.txt"   # seconds left on the current recordings, seconds per recording
    rclone copyto "status_${RUNPOD_POD_ID:-pod}.txt" "$OUT/status/status_${RUNPOD_POD_ID:-pod}.txt" > /dev/null 2>&1
    rclone copyto "eta_${RUNPOD_POD_ID:-pod}.txt" "$OUT/status/eta_${RUNPOD_POD_ID:-pod}.txt" > /dev/null 2>&1
}

# safety limit: whatever happens, after MAX_HOURS upload what is there and switch off
# (the training stops, the normal end below uploads and switches off; if that hangs, this does it 15 minutes later)
( sleep "$(( ${MAX_HOURS:-8} * 3600 ))"; say "SAFETY LIMIT of ${MAX_HOURS:-8} h reached"; pkill -9 -f train_batch.py
  sleep 900; upload; switch_off ) &
LIMIT_PID=$!

status "installing"
SLOTS=${SLOTS:-2}
CL="$ROOT/$AMP/claims"
POD=${RUNPOD_POD_ID:-pod}
say "AMPSURD training machine: amp '$AMP', $SLOTS trainings side by side from the shared queue, $EPOCHS epochs, limit ${MAX_HOURS:-8} h"

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
say "getting the list of recordings from Google Drive ..."
rclone copy "$ROOT/_input/T3K-sweep-v3.wav" . || { say "no input file on Google Drive (AMPSURD training/_input)"; switch_off; }
rclone lsf "$ROOT/$AMP/recordings" --files-only --include "/*.wav" | sort > all.txt
N=$(wc -l < all.txt)
[ "$N" -gt 0 ] || { say "no recordings on Google Drive (AMPSURD training/$AMP/recordings)"; switch_off; }
mkdir -p nams/summaries

# --- the queue: take the next recording that is neither finished nor taken by another training ---
# A training marks a recording as taken (claims/<rec>__<pod>_<k>), waits 20 s and checks the marks again: if
# another one marked it earlier (or at the same moment with a smaller name), it gives it up and looks further.
# On this machine the trainings take turns (flock), so they never pick at the same time.
claim_next() {
    local k=$1 rec base me first
    exec 9> "$W/.claim.lock"; flock 9
    rclone lsf "$OUT" --files-only --include "/*.nam" 2> /dev/null | sed 's/\.nam$//' > done.txt
    rclone lsf "$CL" --files-only 2> /dev/null > claims.txt
    while read -r rec; do
        base=${rec%.wav}
        grep -qxF "$base" done.txt && continue
        awk -v b="${base}__" 'index($0, b) == 1 { f = 1 } END { exit !f }' claims.txt && continue
        me="${base}__${POD}_$k"
        : > ".claim_$k"; rclone copyto ".claim_$k" "$CL/$me" > /dev/null 2>&1 || continue
        sleep 20
        first=$(rclone lsf "$CL" --files-only --format "tp" --separator "|" 2> /dev/null \
            | awk -F'|' -v b="${base}__" 'index($2, b) == 1' | sort | head -1 | cut -d'|' -f2)
        if [ "$first" = "$me" ]; then flock -u 9; echo "$rec"; return 0; fi
        rclone deletefile "$CL/$me" > /dev/null 2>&1
        rclone lsf "$CL" --files-only 2> /dev/null > claims.txt
    done < all.txt
    flock -u 9
    return 1
}

worker() {
    local k=$1 rec base
    sleep $(( (k - 1) * 30 + RANDOM % 20 ))
    while rec=$(claim_next "$k"); do
        base=${rec%.wav}
        say "training $k takes $rec"
        rm -rf "rec_$k"; mkdir -p "rec_$k"
        rclone copyto "$ROOT/$AMP/recordings/$rec" "rec_$k/$rec" || { say "could not download $rec"; continue; }
        python train_batch.py --input T3K-sweep-v3.wav --recordings "rec_$k" --dest nams --epochs "$EPOCHS" \
            --tag "_w$k" >> "train_part$k.txt" 2>&1
        [ -f "nams/summary_w$k.txt" ] && mv "nams/summary_w$k.txt" "nams/summaries/summary_$base.txt"
        upload
    done
    say "training $k: nothing left in the queue"
}

# --- training: SLOTS side by side, each working through the queue; finished .nam files go up at once ---
( sleep 60; while true; do status; upload; sleep 240; done ) &
UP_PID=$!
PIDS=()
for k in $(seq 1 "$SLOTS"); do worker "$k" & PIDS+=($!); done
wait "${PIDS[@]}"
kill "$UP_PID" 2> /dev/null

# --- results ---
upload
status "finished - switching off"
for f in train_part*.txt; do tail -n 40 "$f"; done
cat nams/summaries/*.txt 2> /dev/null
say "$(rclone lsf "$OUT" --files-only --include "/*.nam" | wc -l) .nam files are on Google Drive in AMPSURD training/$AMP/NAMs"
kill "$LIMIT_PID" 2> /dev/null
switch_off
