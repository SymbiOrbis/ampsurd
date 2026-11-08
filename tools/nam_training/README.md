# Training NAM captures on a rented GPU (RunPod)

## The one command (on the owner's PC)

    powershell -ExecutionPolicy Bypass -File .\train_amp.ps1 -Amp "Mesa Dual" -Recordings "C:\...\Mesa Dual\recordings"

Uploads the recordings to Google Drive (AMPSURD training\<amp>\recordings), rents 3 machines (cheapest suitable
free GPU, 3 trainings each), shows every 5 minutes what each machine trains and how long it still needs, and
downloads the .nam files to <recordings>\..\NAMs when all machines are off. Each machine (pod_job.sh) switches
itself off when done, on errors, and after 8 hours at the latest; finished .nam files go to Google Drive every
4 minutes. Running it again for the same amp trains only what is missing; another amp can run at the same time
in a second window. First run: asks for the RunPod API key (kept encrypted for the Windows user) and connects
rclone (C:\rclone.exe) to Google Drive (drive.file scope: only rclone's own files).
The first time also: -InputFile <path to T3K-sweep-v3.wav> (uploaded once to AMPSURD training\_input).
Options: -Machines -PerMachine -Epochs -MaxHours -NoWait -Collect.

Tested end to end 2026-11-08 (2 recordings, 20 epochs, 1 machine): trained, uploaded, switched itself off.

## By hand (Jupyter), as on 2026-11-08

Owner's process since 2026-11-08 (TONE3000 free training cap reached; local CPU training too slow).
Same trainer as TONE3000 / the NAM window: neural-amp-modeler 0.13 (A2 = SlimmableContainer, ECO works),
400 epochs, automatic latency. The .nam files carry loudness and gain, so the AMPSURD amp builder's checks work.
The TONE3000 sweep (T3K-sweep-v3.wav) is the NAM standard v3 input (the trainer recognises its hash).

No keys or passwords in this folder: the RunPod API key and the Google Drive code are pasted on the rented machine.

## Once per machine (Jupyter on a RunPod "PyTorch 2.8" pod)
    pip install neural-amp-modeler==0.13.0
    apt-get update && apt-get install -y python3-tk unzip && curl https://rclone.org/install.sh | bash
    python setup_gdrive.py                      # paste the code from: rclone authorize "drive" "eyJzY29wZSI6ImRyaXZlLmZpbGUifQ"
    echo "KEY" > /root/.rpkey && chmod 600 /root/.rpkey   # RunPod API key (All), for switching the pod off

## Per batch
Upload T3K-sweep-v3.wav, these scripts, and the recordings into `recordings/`
(48 kHz WAV, exactly as long as the input; names like CH1_CLEAN_G08_gain_0800.wav - the .nam gets the same name).

    python train_batch.py --input T3K-sweep-v3.wav --recordings recordings --dest nams --epochs 400 --part 1 --parts 3
    (the same with --part 2 and --part 3 in two more terminals)
    nohup bash finish_and_upload.sh CH1 > autostop.log 2>&1 &

`finish_and_upload.sh` waits for the trainings, uploads the .nam files and summaries to Google Drive
"AMPSURD NAMs/<name>", checks they all arrived, then terminates the pod (REST API, key in /root/.rpkey).

## Measured (RTX PRO 4000, 12 vCPU, $0.58/h)
One training alone: ~19 it/s, ~3.3 s per epoch, ~22 min per capture (400 epochs).
Three at once: ~8.5 it/s each (~1.3x more in total) - the machine is limited by its CPU, not the GPU.
Bigger GPUs do not help; more machines do (--parts 9 across 3 machines x 3 terminals).

## Notes
- Silent recordings (7:00 with GAIN fully down) are skipped - AMPSURD makes the silence itself.
- Stop a training with `pkill -9 -f train_batch.py` (Ctrl+C does not reach it in the browser terminal; a plain
  pkill makes the trainer save a half-trained model - delete that .nam).
- Planned: one command on the owner's PC that starts the machines and does all of this (Google Drive in / out,
  8-hour safety limit, cheapest suitable GPU).
