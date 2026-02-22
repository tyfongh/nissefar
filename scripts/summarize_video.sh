#!/usr/bin/env bash
set -euo pipefail

URL="${1:-}"
WORK_DIR="$(mktemp -d /tmp/video_summarize_XXXXXX)"
AUDIO_DIR="$WORK_DIR/audio"
TRANSCRIPT_BASE="$WORK_DIR/transcript"
TRANSCRIPT_FILE="${TRANSCRIPT_BASE}.txt"

WHISPER_IMAGE="ghcr.io/ggml-org/whisper.cpp:main-cuda"
MODEL_DIR="/mnt/disker/whisper"
MODEL_PATH="/models/ggml-small.bin"

OLLAMA_URL="http://localhost:11434/v1/chat/completions"
OLLAMA_MODEL="glm-5:cloud"

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

error() {
    echo "ERROR: $1"
    exit 1
}

[ -z "$URL" ] && error "No URL provided"

command -v yt-dlp >/dev/null 2>&1 || error "yt-dlp not installed"
command -v ffmpeg >/dev/null 2>&1 || error "ffmpeg not installed"
command -v curl >/dev/null 2>&1 || error "curl not installed"
command -v jq >/dev/null 2>&1 || error "jq not installed"
command -v docker >/dev/null 2>&1 || error "docker not installed"

mkdir -p "$AUDIO_DIR"

if ! yt-dlp \
    --no-playlist \
    --match-filter "duration <= 3600" \
    -f bestaudio \
    -o "$AUDIO_DIR/audio.%(ext)s" \
    "$URL" >/dev/null 2>&1; then

    error "Download failed"
fi

shopt -s nullglob
audio_files=("$AUDIO_DIR"/audio.*)
shopt -u nullglob
[ ${#audio_files[@]} -eq 0 ] && error "Downloaded audio not found"
AUDIO_FILE="${audio_files[0]}"

# Transcode if not WAV
EXT="${AUDIO_FILE##*.}"
if [ "$EXT" != "wav" ]; then
    WAV_FILE="$AUDIO_DIR/audio.wav"
    if ! ffmpeg -y -i "$AUDIO_FILE" -ar 16000 -ac 1 "$WAV_FILE" >/dev/null 2>&1; then
        error "Audio transcoding failed"
    fi
    AUDIO_FILE="$WAV_FILE"
fi

# Run whisper inside Docker (GPU enabled)

if ! docker run --rm --gpus all \
    -e NVIDIA_DRIVER_CAPABILITIES=compute,utility \
    -e LD_LIBRARY_PATH=/usr/local/nvidia/lib64:/usr/local/nvidia/lib:/usr/local/cuda/lib64 \
    -v "$MODEL_DIR":/models \
    -v "$WORK_DIR":/work \
    "$WHISPER_IMAGE" \
    "whisper-cli -m /models/ggml-small.bin -f /work/audio/audio.wav -otxt -of /work/transcript" \
    >"$WORK_DIR/whisper.log" 2>&1; then

    echo "ERROR: Transcription failed"
    exit 1
fi


[ ! -f "$TRANSCRIPT_FILE" ] && error "Transcript missing"

TRANSCRIPT=$(cat "$TRANSCRIPT_FILE")
[ -z "$TRANSCRIPT" ] && error "Transcript empty"

# Summarize via Ollama OpenAI-compatible API
RESPONSE=$(curl -sS "$OLLAMA_URL" \
    -H "Content-Type: application/json" \
    --fail-with-body \
    --connect-timeout 15 \
    --max-time 90 \
    -d "$(jq -n \
        --arg model "$OLLAMA_MODEL" \
        --arg content "$TRANSCRIPT" \
        '{
            model: $model,
            messages: [
                {role: "system", content: "Produce a concise summary."},
                {role: "user", content: $content}
            ],
            temperature: 0.2
        }')"
) || error "Ollama API request failed"

SUMMARY=$(echo "$RESPONSE" | jq -r '.choices[0].message.content // empty')

[ -z "$SUMMARY" ] && error "Summarization failed"

echo "$SUMMARY"
