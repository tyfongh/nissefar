# Configuration

Runtime config is loaded from `~/.config/nissefar/config.ini`.

## Required Keys

```ini
[General]
discord_token = <discord bot token>
google_api_key = <google api key>
chatgpt_model = gpt-5.4
system_prompt = <main system prompt>
diff_system_prompt = <diff summary system prompt>
image_description_system_prompt = <image description system prompt>
max_history = 40

[Database]
db_connection_string = <postgres connection string>
```

## Optional Keys

```ini
[General]
context_size = 40000
num_predict = 4000
rate_limit_count = 3
rate_limit_window_seconds = 300
video_summary_script_path = scripts/summarize_video.sh
youtube_summary_bot_id =
youtube_summary_channel_id =
owner_id =
allowed_channels = botspam
youtube_skip_channel_names =
```

## Notes

- `chatgpt_model` is the only model setting. Use `gpt-5.4` for now.
- The old `text_model`, `comparison_model`, `vision_model`, `image_description_model`, and `ollama_server_url` settings are no longer used.
- ChatGPT OAuth credentials are expected at `~/.config/nissefar/chatgpt.json`.
- The auth file is part of the migration target and should be written with permissions `0600`.
- Bot startup now fails if `chatgpt.json` is missing, malformed, or missing required auth fields.

## chatgpt.json Schema

```json
{
  "type": "oauth",
  "refresh": "<refresh token>",
  "access": "<access token>",
  "expires": 1735689600,
  "accountId": "optional"
}
```

## Device Login CLI

Use `nissefar-chatgpt-auth` to perform the initial ChatGPT device login.

The CLI will:

- print the verification URL `https://auth.openai.com/codex/device`
- print the device code you need to enter
- poll for authorization completion
- exchange the authorization code for OAuth tokens
- extract `accountId` from JWT claims when present
- write `~/.config/nissefar/chatgpt.json` with permissions `0600`

The CLI does not print tokens to stdout.
