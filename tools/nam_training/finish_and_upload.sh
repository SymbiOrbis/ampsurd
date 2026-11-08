#!/bin/bash
# AMPSURD - when training is done: upload the .nam files to Google Drive, then terminate this RunPod pod.
#
# Start (once, after the trainings are running; it keeps running when the browser is closed):
#     nohup bash finish_and_upload.sh CH1 > autostop.log 2>&1 &
# Watch:  tail -f autostop.log      (Ctrl+C only stops watching, not the script)
#
# 1. waits until no train_batch.py is running any more
# 2. uploads nams/*.nam and the summaries to Google Drive, folder "AMPSURD NAMs/<name>" (rclone remote "gdrive")
# 3. checks that every .nam arrived
# 4. only then terminates the pod (no more charges); if anything failed it stays on, so nothing is lost
#    (terminating needs a RunPod API key in /root/.rpkey:  echo "KEY" > /root/.rpkey && chmod 600 /root/.rpkey)

NAME="${1:-batch}"
DEST="gdrive:AMPSURD NAMs/$NAME"
cd /workspace || exit 1

echo "$(date '+%H:%M') waiting for the trainings to finish ..."
while pgrep -f train_batch.py > /dev/null; do sleep 60; done
echo "$(date '+%H:%M') trainings finished"

ls nams/*.nam > /dev/null 2>&1 || { echo "no .nam files found - NOT terminating"; exit 1; }
COUNT=$(ls nams/*.nam | wc -l)
echo "uploading $COUNT .nam files to Google Drive: $DEST"

rclone copy nams "$DEST" --include "*.nam" --include "summary*.txt" --retries 5 \
    || { echo "UPLOAD FAILED - the pod stays on, download the files by hand"; exit 1; }

ARRIVED=$(rclone lsf "$DEST" --include "*.nam" | wc -l)
if [ "$ARRIVED" -lt "$COUNT" ]; then
    echo "only $ARRIVED of $COUNT files on Google Drive - NOT terminating"
    exit 1
fi
echo "$(date '+%H:%M') all $COUNT files are on Google Drive - terminating the pod"
sleep 30
# RunPod REST API with the key saved in /root/.rpkey (the older runpodctl may not accept new keys)
if [ -s /root/.rpkey ]; then
    CODE=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE -H "Authorization: Bearer $(cat /root/.rpkey)" \
        "https://rest.runpod.io/v1/pods/$RUNPOD_POD_ID")
    echo "terminate answered $CODE"
    case "$CODE" in 2*) exit 0;; esac
    CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Authorization: Bearer $(cat /root/.rpkey)" \
        "https://rest.runpod.io/v1/pods/$RUNPOD_POD_ID/stop")
    echo "stop answered $CODE"
    case "$CODE" in 2*) exit 0;; esac
fi
runpodctl remove pod "$RUNPOD_POD_ID" || runpodctl stop pod "$RUNPOD_POD_ID" \
    || echo "COULD NOT SWITCH THE POD OFF - terminate it by hand in RunPod (the files are on Google Drive)"
