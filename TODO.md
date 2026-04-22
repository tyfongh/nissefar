# Codex Migration TODO

## Goal

Replace the current Ollama-based LLM integration with the ChatGPT Codex OAuth flow used by OpenCode, using:

- `chatgpt_model = gpt-5.4` for all LLM use cases
- a separate device-code login CLI for initial authentication
- bot-side automatic token refresh using `~/.config/nissefar/chatgpt.json`
- startup failure if ChatGPT auth is missing or invalid

## Constraints

- Device-code auth only
- No localhost callback flow
- One model for all modes
- Preserve current bot behaviors where possible:
  - normal text replies
  - diff summaries
  - image descriptions
  - tool calling
  - image-assisted chat
- Remove `ollama-hpp` fully by the end
- Persist OAuth auth in:
  - `$HOME/.config/nissefar/chatgpt.json`
- Auth file permissions must be `0600`

## Reference Behavior To Mirror

Source reference from OpenCode `dev` branch:

- `packages/opencode/src/plugin/codex.ts`
- `packages/opencode/src/auth/index.ts`
- `packages/opencode/src/provider/provider.ts`
- `packages/opencode/src/session/llm.ts`

Important external endpoints:

- `POST https://auth.openai.com/api/accounts/deviceauth/usercode`
- `POST https://auth.openai.com/api/accounts/deviceauth/token`
- `POST https://auth.openai.com/oauth/token`
- `POST https://chatgpt.com/backend-api/codex/responses`

Important persisted auth fields:

- `type`
- `refresh`
- `access`
- `expires`
- `accountId` optional

Important runtime headers:

- `Authorization: Bearer <access>`
- `ChatGPT-Account-Id: <accountId>` when present

## Phase 1: Inventory And Decoupling

### 1.1 Audit current Ollama coupling
- [x] Confirm all `ollama-hpp` usages in repo
- [x] Confirm all `ollama::json` usages outside `LlmService`
- [x] Confirm all `ollama::images` usages in callers
- [x] Confirm all config and docs references to Ollama

### 1.2 Introduce repo-owned JSON abstraction
- [x] Add a local JSON header using `nlohmann::json`
- [x] Replace `ollama::json` in:
  - [x] `src/DiscordEventService.cpp`
  - [x] `src/AnalyticsQuery.cpp`
- [x] Update any helper APIs that currently accept/return `ollama::json`

### 1.3 Introduce repo-owned LLM types
- [x] Define provider-neutral image type
- [x] Define provider-neutral message type
- [x] Define provider-neutral tool-call representation
- [x] Define provider-neutral response representation
- [x] Remove provider SDK types from `LlmService` public API if practical
- [x] If not practical in one pass, reduce exposure incrementally

## Phase 2: Config Migration

### 2.1 Update config model
- [x] Add `chatgpt_model`
- [x] Default/target value: `gpt-5.4`
- [x] Remove or stop requiring:
  - [x] `text_model`
  - [x] `comparison_model`
  - [x] `vision_model`
  - [x] `image_description_model`
  - [x] `ollama_server_url`
- [x] Preserve:
  - [x] `system_prompt`
  - [x] `diff_system_prompt`
  - [x] `image_description_system_prompt`

### 2.2 Update config validation
- [x] Make `chatgpt_model` required
- [x] Remove Ollama URL validation
- [x] Ensure startup clearly fails on invalid config

### 2.3 Update startup logging
- [x] Remove `Ollama server url` log line from `src/Nissefar.cpp`
- [x] Add ChatGPT/Codex relevant startup logging if useful
- [x] Keep logging free of secrets/tokens

### 2.4 Update documentation
- [x] Update setup docs (`CONFIGURATION.md`; `CLAUDE.md` was removed)
- [x] Update any config examples
- [x] Document `chatgpt_model = gpt-5.4`
- [x] Document `chatgpt.json` requirement

## Phase 3: Auth Storage

### 3.1 Define auth file schema
- [x] Create local auth types matching intended file structure
- [x] Required fields:
  - [x] `type = "oauth"`
  - [x] `refresh`
  - [x] `access`
  - [x] `expires`
- [x] Optional fields:
  - [x] `accountId`

### 3.2 Implement auth file path logic
- [x] Resolve `$HOME/.config/nissefar/chatgpt.json`
- [x] Handle missing `$HOME`
- [x] Handle missing config directory
- [x] Ensure file writes use `0600`

### 3.3 Implement auth store
- [x] Read auth file
- [x] Parse and validate JSON
- [x] Write auth file atomically if possible
- [x] Fail clearly on malformed file
- [x] Avoid logging secrets

### 3.4 Startup enforcement
- [x] Make bot startup fail if auth file is missing
- [x] Make bot startup fail if auth file is malformed
- [x] Make bot startup fail if required fields are missing

## Phase 4: Device-Code Login CLI

### 4.1 Add separate executable
- [x] Create a new CLI target in `CMakeLists.txt`
- [x] Pick executable name
- [x] Keep it fully separate from bot startup path

### 4.2 Implement device-code start request
- [x] `POST /api/accounts/deviceauth/usercode`
- [x] Send required `client_id`
- [x] Parse:
  - [x] `device_auth_id`
  - [x] `user_code`
  - [x] `interval`

### 4.3 Implement user-facing CLI flow
- [x] Print verification URL:
  - [x] `https://auth.openai.com/codex/device`
- [x] Print user code clearly
- [x] Print polling progress
- [x] Print success/failure states

### 4.4 Implement device polling
- [x] Poll `POST /api/accounts/deviceauth/token`
- [x] Respect returned interval if available
- [x] Handle pending/slow authorization states
- [x] Handle timeout/cancellation cleanly

### 4.5 Implement token exchange
- [x] Exchange authorization code at `POST /oauth/token`
- [x] Send required fields:
  - [x] `grant_type=authorization_code`
  - [x] `code`
  - [x] `redirect_uri=https://auth.openai.com/deviceauth/callback`
  - [x] `client_id`
  - [x] `code_verifier`
- [x] Parse:
  - [x] `access_token`
  - [x] `refresh_token`
  - [x] `expires_in`
  - [x] `id_token` if present

### 4.6 Extract account ID
- [x] Parse JWT claims from `id_token`
- [x] Fallback to `access_token` claims if needed
- [x] Extract `chatgpt_account_id` or equivalent claim path
- [x] Store `accountId` when available

### 4.7 Persist auth
- [x] Write final `chatgpt.json`
- [x] Confirm `0600`
- [x] Avoid printing secrets after completion

## Phase 5: Bot-Side Token Refresh

### 5.1 Implement token manager
- [x] Load auth on demand
- [x] Determine whether token is expired
- [x] Refresh lazily before requests
- [x] Persist updated token values immediately

### 5.2 Implement refresh request
- [x] `POST https://auth.openai.com/oauth/token`
- [x] Send:
  - [x] `grant_type=refresh_token`
  - [x] `refresh_token`
  - [x] `client_id`
- [x] Parse new:
  - [x] `access_token`
  - [x] `refresh_token`
  - [x] `expires_in`
  - [x] `id_token` if present

### 5.3 Refresh robustness
- [x] Handle refresh failures clearly
- [x] Surface a user-safe failure message from bot runtime
- [x] Avoid token-refresh storms under concurrent requests
- [x] Add locking around auth refresh/update if needed

### 5.4 Preserve/update account ID
- [x] Recompute `accountId` from refreshed tokens when possible
- [x] Keep previous `accountId` if refresh does not provide a new one

## Phase 6: HTTP / Codex Client

### 6.1 Choose/standardize HTTP client
- [ ] Determine available HTTP stack to use in C++
- [ ] Support:
  - [ ] JSON POST
  - [ ] headers
  - [ ] TLS HTTPS
  - [ ] timeouts
- [ ] Reuse one implementation for auth + model requests

### 6.2 Implement Codex request client
- [ ] Add request builder for `https://chatgpt.com/backend-api/codex/responses`
- [ ] Add bearer auth header
- [ ] Add `ChatGPT-Account-Id` header when present
- [ ] Ensure request/response logging avoids secrets

### 6.3 Implement response parsing
- [ ] Parse normal text output
- [ ] Parse tool-call output
- [ ] Parse multimodal/image responses if shape differs
- [ ] Handle API errors and malformed payloads cleanly

## Phase 7: Plain LLM Generation

### 7.1 Port `generate_text()`
- [ ] Reimplement normal text reply path
- [ ] Reimplement diff path
- [ ] Reimplement image-description path
- [ ] Use `config.chatgpt_model` for all modes

### 7.2 Map prompts correctly
- [ ] `TextReply` uses `system_prompt`
- [ ] `Diff` uses `diff_system_prompt`
- [ ] `ImageDescription` uses `image_description_system_prompt`
- [ ] Put selected system prompt into `instructions`

### 7.3 Preserve output limits and error handling
- [ ] Keep length cap behavior
- [ ] Keep bot-safe fallback strings
- [ ] Preserve useful logs without leaking auth data

## Phase 8: Tool Calling

### 8.1 Replace Ollama tool wiring
- [ ] Remove `include/OllamaToolCalling.h` usage from `LlmService`
- [ ] Rebuild tool schema generation for Codex responses format
- [ ] Preserve existing `ToolDefinition` interface if possible

### 8.2 Port iterative tool loop
- [ ] Send available tools with request
- [ ] Parse tool calls from response
- [ ] Execute current tool executor callback
- [ ] Append tool results into follow-up request
- [ ] Repeat until final answer or iteration cap

### 8.3 Preserve current safeguards
- [ ] Duplicate tool-call blocking
- [ ] Max iteration count
- [ ] Logging of tool name/args/result size
- [ ] Analytics forced final-response path
- [ ] Fallback without tools when tool mode fails

### 8.4 Caller compatibility
- [ ] Keep `DiscordEventService` tool executor contract unchanged if possible
- [ ] Update call sites only where necessary

## Phase 9: Image Support

### 9.1 Keep attachment acquisition
- [ ] Preserve current attachment download flow
- [ ] Preserve allowed image content types
- [ ] Keep base64 conversion if needed by target API

### 9.2 Map images into Codex request format
- [ ] Implement image input structure for normal multimodal chat
- [ ] Implement image input structure for image description mode
- [ ] Verify multiple-image handling if needed

### 9.3 Validate behavior
- [ ] Normal reply with image attachment
- [ ] Image description response
- [ ] Graceful behavior when images are unsupported or malformed

## Phase 10: Remove Ollama Dependency

### 10.1 Remove remaining code usages
- [ ] Remove `#include <ollama.hpp>` everywhere
- [ ] Remove Ollama request/response/image types
- [ ] Remove `OllamaToolCalling.h`
- [ ] Remove any stale comments or logs referring to Ollama

### 10.2 Remove build dependency
- [ ] Remove `FetchContent_Declare(ollama_hpp ...)`
- [ ] Remove `FetchContent_MakeAvailable(ollama_hpp)`
- [ ] Remove include directories for `ollama_hpp`
- [ ] Confirm nothing else depends on it

## Phase 11: Build / Validation

### 11.1 Compile/test validation
- [ ] Full configure/build succeeds
- [ ] Existing tests still compile and pass
- [ ] Fix any fallout from JSON/type changes

### 11.2 Auth validation
- [ ] Device login CLI succeeds end-to-end
- [ ] `chatgpt.json` written correctly
- [ ] Refresh path works after token expiry simulation if feasible

### 11.3 Bot runtime validation
- [ ] Plain text reply works
- [ ] Diff generation works
- [ ] Tool calling works
- [ ] Image-assisted reply works
- [ ] Image description works
- [ ] Failure modes are clear and non-destructive

## File Touch List

### Existing files likely to change
- [ ] `CMakeLists.txt`
- [ ] `include/Config.h`
- [ ] `src/Config.cpp`
- [ ] `include/LlmService.h`
- [ ] `src/LlmService.cpp`
- [ ] `src/Nissefar.cpp`
- [ ] `src/DiscordEventService.cpp`
- [ ] `src/AnalyticsQuery.cpp`
- [ ] `CLAUDE.md`

### Files likely to be deleted
- [ ] `include/OllamaToolCalling.h`

### New files likely to be added
- [ ] local JSON alias header
- [ ] ChatGPT auth store header/source
- [ ] ChatGPT token manager header/source
- [ ] Codex HTTP client header/source
- [ ] device login CLI source
- [ ] any request/response model headers needed for provider-neutral LLM types

## Notes

- Do not log tokens, refresh tokens, raw auth file contents, or full authorization headers.
- Preserve unrelated worktree changes if present.
- Prefer staging the migration so the repo remains buildable after each major step.
- Image support is a required migration item, not optional cleanup.
- `ChatGPT-Account-Id` handling is important and should not be skipped.
