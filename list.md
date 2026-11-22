# Free models in OpenCode — task-ranked shortlist

_Generated 22 Nov 2026 from the live `/models` catalog._

## How this list was built

- **Free** = the model's OpenCode catalog entry has **$0 input and $0 output**.
- The live catalog has **781 connected models**; **59 entries** meet that test.
- Those 59 collapse to **39 unique model families + 5 OpenRouter routers**.
- **25 entries** carry an explicit `:free` / `-free` tag. The rest are $0 trial or
  free-tier endpoints (mostly NVIDIA).
- **Free does not mean unlimited.** NVIDIA trial, OpenRouter free and the OpenCode
  free gateway all rate-limit. See “Provider reliability” below.
- You asked for the “OpenWorld test”. I could not find a benchmark by that name in
  OpenCode, OpenRouter or Artificial Analysis, so this uses your chosen fallback:
  a **practical OpenCode assessment**. It is anchored on the 17 Nov Artificial
  Analysis numbers already in this conversation plus catalog metadata and prior
  OpenCode usage. No new model calls were made for this file.

**Tiers:** `S` best free pick · `A` strong · `B` usable with caveats · `C` niche/weak ·
`D` avoid for agent work · `X` not a chat/agent model.

---

## Quick picks

| Need | Use | Why |
|---|---|---|
| Default coding / agent | **DeepSeek V4.1 Flash** (`opencode/deepseek-v4.1-flash-free`) | Best quality-per-cost; proven implementer |
| Second coding model | **Big Pickle** (`opencode/big-pickle`) | Prior “strong implementer”; good tool use |
| Best NVIDIA quality | **GLM-5.3 Flash** | Strong coding; slower and rate-limited |
| Fast subagent | **Nemotron 3.5 Lightning** | Small active params, 262k context |
| Vision | **Nemotron 3.5 Super VL** | Dedicated VL model, 1M context, video |
| Long context | **Step 5 Preview** / **Hy4 Flash** / **Nemotron 3 Ultra** | 1M-class context |
| Reasoning backup | **MiniMax M3.1 Flash** | AA 29.1 and fast |
| Avoid for parallel agents | NVIDIA-only models | 5 RPM cap in the local rate-guard; account-level 429s |

---

## Full free catalog (deduplicated)

Each model appears once. “Also available as” shows the duplicate provider entries
that were collapsed.

| Model | Canonical ID | Also available as | Context | Inputs | Tools | Role |
|---|---|---|---|---:|---|---|
| DeepSeek V4.1 Flash | `opencode/deepseek-v4.1-flash-free` | `nvidia/deepseek-ai/deepseek-v4.1-flash` | 200k (1M NVIDIA) | image, text | yes | agent / coding |
| GLM-5.3 Flash | `nvidia/z-ai/glm-5.3-flash` | — | 1M | image, text | yes | agent / coding |
| MiniMax M3.1 Flash | `nvidia/minimaxai/minimax-m3.1-flash` | — | 262k | image, text, video | yes | agent / multimodal |
| Step 5 Preview | `nvidia/stepfun-ai/step-5-preview` | — | 1.05M | image, text | yes | agent / planner |
| Big Pickle | `opencode/big-pickle` | — | 200k | text | yes | agent / coding |
| Hy4 Flash | `opencode/hy4-flash-free` | — | 1.05M | image, text | yes | agent / long-context |
| Beam 1.0 | `opencode/beam-free` | `vercel/reflection/beam`, `vercel/reflection/beam-free` | 1.05M | text | yes | agent / general |
| Nemotron 3 Ultra 550B | `nvidia/nvidia/nemotron-3-ultra-550b-a55b` | `openrouter/nvidia/nemotron-3-ultra-550b-a55b:free` | 1M | text | yes | agent / planner |
| Nemotron 3.5 Super VL | `opencode/nemotron-3.5-super-vl-free` | `nvidia/nvidia/nemotron-3.5-super-vl-120b-a12b`, `openrouter/nvidia/nemotron-3.5-super-vl:free` | 1.05M | image, text, video | yes | agent / vision |
| Nemotron 3.5 Lightning | `opencode/nemotron-3.5-lightning-free` | `nvidia/nvidia/nemotron-3.5-lightning-30b-a3b`, `openrouter/nvidia/nemotron-3.5-lightning:free`, `vercel/nvidia/nemotron-3.5-lightning-free` | 262k (1M OR/Vercel) | text | yes | agent / fast |
| Nemotron 3 Nano Omni | `nvidia/nvidia/nemotron-3-nano-omni-30b-a3b-reasoning` | `openrouter/nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free` | 256k | audio, image, text, video | yes | agent / multimodal |
| GPT OSS 20B | `nvidia/openai/gpt-oss-20b` | — | 131k | text | yes | agent / coding |
| Laguna XS 2.1 | `nvidia/poolside/laguna-xs-2.1` | — | 262k | text | yes | agent / coding |
| Muse Glimmer 30B | `nvidia/meta/muse-glimmer-30b` | — | 131k | image, text | yes | agent / general |
| Gemma 4 31B | `google/gemma-4-31b-it` | `nvidia/google/gemma-4-31b-it`, `openrouter/google/gemma-4-31b-it:free` | 262k | image, text, video | yes | general / vision |
| Gemma 4 26B A4B | `google/gemma-4-26b-a4b-it` | `openrouter/google/gemma-4-26b-a4b-it:free` | 262k | image, text, video | yes | general / vision |
| DiffusionGemma 26B A4B | `nvidia/google/diffusiongemma-26b-a4b-it` | — | 250k | image, text, video | yes | vision |
| GLM-4.7-Flash | `zai/glm-4.7-flash` | — | 200k | text | yes | general |
| GLM-4.6V-Flash | `zai/glm-4.6v-flash` | — | 128k | image, text, video | yes | vision |
| GLM-4.5-Flash | `zai/glm-4.5-flash` | — | 131k | text | yes | general |
| LongCat 2.5 | `opencode/longcat-2.5-free` | — | 1M | image, text | yes | general / long-context |
| Ling 3.2 Flash | `openrouter/inclusionai/ling-3.2-flash:free` | — | 1.05M | image, text | yes | general / long-context |
| ERNIE 5.5 Flash Preview | `openrouter/baidu/ernie-5.5-flash-preview:free` | — | 1.02M | text | yes | general / long-context |
| Particle-1 | `openrouter/particle-ai/particle-1:free` | — | 1.05M | image, text | yes | general / long-context |
| Flint 40B | `opencode/flint-40b-free` | `openrouter/recursive/flint-v1.0:free`, `vercel/recursive/flint-v1.0` | 262k | text | yes | coding / general |
| MiMo V2.6 Flash | `opencode/mimo-v2.6-flash-free` | — | 200k | audio, image, text, video | yes | multimodal / fast |
| Iceman | `opencode/iceman-free` | — | 262k | text | yes | general |
| Tidepool Alpha | `opencode/tidepool-alpha-free` | — | 1.05M | text | yes | general / long-context |
| Ampersand v0.1 | `vercel/humansand/ampersand-v0.1` | — | 1M | text | yes | general |
| Glyph Cluster | `vercel/stealth/glyph-cluster` | — | 256k | text | yes | general |
| LFM3 1.7B | `openrouter/liquid/lfm-3-1.7b:free` | — | 262k | text | yes | tiny |
| ALLaM-2-7b | `groq/allam-2-7b` | — | 4k | text | no | not agent-capable |
| Llama 3.2 11B Vision | `nvidia/meta/llama-3.2-11b-vision-instruct` | — | 128k | image, text | yes | vision |
| Llama 3.2 90B Vision | `nvidia/meta/llama-3.2-90b-vision-instruct` | — | 128k | image, text | yes | vision |
| Paligemma | `nvidia/google/google-paligemma` | — | 128k | image, text | no | vision (no tools) |
| Llama Guard 4 12B | `nvidia/meta/llama-guard-4-12b` | — | 163k | image, text | no | guard |
| Nemotron Safety Guard 8B | `nvidia/nvidia/llama-3.1-nemotron-safety-guard-8b-v3` | — | 128k | text | no | guard |
| Nemotron 3.5 Content Safety | `nvidia/nvidia/nemotron-3.5-content-safety` | — | 128k | image, text | no | guard |
| Nemotron Voicechat | `nvidia/nvidia/nemotron-voicechat` | — | 128k | audio, text | yes | audio chat |

**Excluded from the model count:** five $0 OpenRouter routers —
`openrouter/openrouter/auto`, `openrouter/openrouter/free`, `openrouter/openrouter/fusion`,
`openrouter/openrouter/pareto-code`, `openrouter/openrouter/bodybuilder`.
They route to other models rather than being a model you can benchmark directly.

---

## 1. Reasoning

Ranked by depth of reasoning, long-context planning and reliability — not by speed.

| # | Model | Tier | Why |
|---:|---|---|---|
| 1 | **DeepSeek V4.1 Flash** | S | AA 32.6 at Max; reasoning variants; best free reasoning model |
| 2 | **GLM-5.3 Flash** | S | AA 27.8; 1M context; strong planner, slower TTFA |
| 3 | **MiniMax M3.1 Flash** | A | AA 29.1; faster than GLM; 262k context |
| 4 | **Step 5 Preview** | A | 1M context; preview model; strong planner candidate, less proven |
| 5 | **Hy4 Flash** | A | 1M context; high OpenRouter usage; still new |
| 6 | **Beam 1.0** | A− | Large 501B A23B generalist; 1M context |
| 7 | **Big Pickle** | A− | Strong implementer; 200k context limits very deep planning |
| 8 | **Nemotron 3 Ultra 550B** | B+ | AA only 11.8, but 1M context and fastest TTFA; use as a long-context planner, not for hard reasoning |
| 9 | **LongCat 2.5** | B+ | 1M context; generalist; limited public evidence |
| 10 | **ERNIE 5.5 Flash Preview** | B+ | 1M context; free-preview usage is strong, quality unproven |

**Next tier:** Ling 3.2 Flash, Nemotron 3.5 Super VL, Nemotron 3 Nano Omni,
Gemma 4 31B, Particle-1, Tidepool Alpha, Iceman, Ampersand, Glyph Cluster.

---

## 2. Coding

Ranked for code generation, repo reading, tool use and edit loops.

| # | Model | Tier | Why |
|---:|---|---|---|
| 1 | **DeepSeek V4.1 Flash** | S | Best free coding model; proven in this repo’s implementation work |
| 2 | **Big Pickle** | S | Prior “strong implementer”; good multi-step edits |
| 3 | **GLM-5.3 Flash** | S | Coding-focused; 1M context; slower and NVIDIA-only |
| 4 | **MiniMax M3.1 Flash** | A | 141 tok/s; solid agentic coding; 262k |
| 5 | **Step 5 Preview** | A | 1M context; strong usage signal; preview risk |
| 6 | **Hy4 Flash** | A | 1M context; high adoption; less proven in this repo |
| 7 | **Laguna XS 2.1** | A− | Poolside coding specialist; 262k; NVIDIA-only |
| 8 | **GPT OSS 20B** | B+ | Cheap, fast, good for small edits; weaker on large refactors |
| 9 | **Beam 1.0** | B+ | Large generalist; not code-specialised |
| 10 | **Flint 40B** | B | Coding-oriented; limited evidence; 262k |

**Next tier:** LongCat 2.5, Ling 3.2 Flash, ERNIE 5.5 Flash, Nemotron 3.5 Lightning,
Gemma 4 31B, Nemotron 3 Ultra.

---

## 3. Multi-agent workload

Ranked for tool calling, instruction adherence, parallel subagents, context and
rate-limit resilience.

| # | Model | Tier | Why |
|---:|---|---|---|
| 1 | **DeepSeek V4.1 Flash** | S | Best quality + OpenCode-gateway availability; default orchestrator |
| 2 | **Big Pickle** | S | Strong implementer; 200k context; good tool use |
| 3 | **Hy4 Flash** | A | 1M context; good for parallel workers; new but widely used |
| 4 | **GLM-5.3 Flash** | A | Strong, but NVIDIA-only and 5 RPM — keep to one agent at a time |
| 5 | **MiniMax M3.1 Flash** | A | Fast, 262k; same NVIDIA rate-limit caveat |
| 6 | **Step 5 Preview** | A | 1M context; NVIDIA rate-limit caveat |
| 7 | **Nemotron 3.5 Lightning** | B+ | Excellent cheap subagent; lower reasoning ceiling |
| 8 | **Beam 1.0** | B+ | 1M context; large; tool-reliability less proven |
| 9 | **LongCat 2.5** | B+ | 1M context; limited evidence |
| 10 | **Nemotron 3.5 Super VL** | B+ | Fast, vision-capable subagent; 1M |

**Next tier:** Ling 3.2 Flash, ERNIE 5.5 Flash, Flint 40B, Gemma 4 31B,
Nemotron 3 Nano Omni, GPT OSS 20B.

> **Multi-agent rule:** prefer `opencode/*` models for parallel work. NVIDIA-only
> models share one account-level quota; the local rate-guard spaces them 10s apart
> at 5 RPM, which defeats parallelism.

---

## 4. Files access and file edits

Ranked for agentic read → edit → verify loops, diff accuracy and tool discipline.

| # | Model | Tier | Why |
|---:|---|---|---|
| 1 | **DeepSeek V4.1 Flash** | S | Best free edit loop; proven on this repo |
| 2 | **Big Pickle** | S | Strong implementer; reliable multi-file edits |
| 3 | **GLM-5.3 Flash** | A | Strong coding; 1M context; NVIDIA quota |
| 4 | **MiniMax M3.1 Flash** | A | Fast edit loops; 262k |
| 5 | **Step 5 Preview** | A | 1M context; good for big diffs |
| 6 | **Hy4 Flash** | A | 1M context; good for large files |
| 7 | **Laguna XS 2.1** | A− | Coding specialist; 262k |
| 8 | **GPT OSS 20B** | B+ | Fine for small files; weaker on large refactors |
| 9 | **Beam 1.0** | B+ | Large generalist; not code-specialised |
| 10 | **Flint 40B** | B | Coding-oriented; limited evidence |

**Next tier:** LongCat 2.5, Ling 3.2 Flash, ERNIE 5.5 Flash, Nemotron 3.5 Lightning,
Gemma 4 31B.

---

## 5. Vision models

Ranked for image understanding, screenshots/UI and video.

| # | Model | Tier | Why |
|---:|---|---|---|
| 1 | **Nemotron 3.5 Super VL** | S | Dedicated VL; 1M context; image + video; fastest TTFT class |
| 2 | **Gemma 4 31B** | S | Strong general vision; 262k; good fallback |
| 3 | **MiniMax M3.1 Flash** | A | Image + video; 262k; good speed |
| 4 | **Nemotron 3 Nano Omni** | A | Image + video + audio; 256k |
| 5 | **DiffusionGemma 26B A4B** | A− | Image + video; 250k; diffusion-based |
| 6 | **GLM-4.6V-Flash** | B+ | Image + video; 128k; smaller |
| 7 | **Step 5 Preview** | B+ | Image input; 1M context; not vision-first |
| 8 | **DeepSeek V4.1 Flash** | B | Image input in catalog; not a vision specialist |
| 9 | **Llama 3.2 90B Vision** | B | Older vision model; 128k |
| 10 | **Gemma 4 26B A4B** | B | Good vision, smaller than 31B |

**Next tier:** Llama 3.2 11B Vision, Muse Glimmer 30B, GLM-5.3 Flash, Hy4 Flash,
LongCat 2.5, Ling 3.2 Flash, Particle-1, MiMo V2.6 Flash.

**Not for this task:** Paligemma (vision but no tool support), Nemotron Voicechat
(audio chat, not image vision).

---

## 6. Overall best free models

Weighs quality, speed, context, OpenCode compatibility and free-tier reliability.

| # | Model | Tier | Best role |
|---:|---|---|---|
| 1 | **DeepSeek V4.1 Flash** | S | Default coding / agent / reasoning |
| 2 | **Big Pickle** | S | Coding alternative when DeepSeek is rate-limited |
| 3 | **GLM-5.3 Flash** | A | Best NVIDIA quality; single-agent use |
| 4 | **MiniMax M3.1 Flash** | A | Best speed/quality mix on NVIDIA |
| 5 | **Step 5 Preview** | A | Long-context planner / big-repo work |
| 6 | **Hy4 Flash** | A | 1M-context worker; high usage |
| 7 | **Nemotron 3.5 Super VL** | A− | Vision + fast subagent |
| 8 | **Nemotron 3.5 Lightning** | B+ | Cheap parallel subagent |
| 9 | **Beam 1.0** | B+ | Large generalist fallback |
| 10 | **Nemotron 3 Ultra 550B** | B+ | Long-context planner only |
| 11 | **LongCat 2.5** | B+ | 1M-context fallback |
| 12 | **Gemma 4 31B** | B+ | Vision / general fallback |

**Rest of the free set:** Ling 3.2 Flash, ERNIE 5.5 Flash Preview, GPT OSS 20B,
Laguna XS 2.1, Nemotron 3 Nano Omni, Flint 40B, Particle-1, Tidepool Alpha,
Iceman, Ampersand, Glyph Cluster, Muse Glimmer 30B, MiMo V2.6 Flash,
Gemma 4 26B A4B, DiffusionGemma 26B A4B, GLM-4.7-Flash, GLM-4.6V-Flash,
GLM-4.5-Flash, Llama 3.2 90B Vision, Llama 3.2 11B Vision.

**Avoid for agent work:** LFM3 1.7B (too small), ALLaM-2-7b (4k, no tools),
Paligemma (no tools), all guard/classifier models, Nemotron Voicechat.

---

## Provider reliability

| Provider | Free models here | Reality |
|---|---|---|
| `opencode/*` | DeepSeek, Big Pickle, Beam, Flint, Hy4, Iceman, LongCat, MiMo, Nemotron Lightning, Super VL, Tidepool | Shared free request budget; usually reliable; best default for parallel agents |
| `nvidia/*` | GLM-5.3 Flash, MiniMax, Step 5, Nemotron Ultra, Super VL, Lightning, Nano Omni, GPT OSS, Laguna, Muse, Gemma, DiffusionGemma, guards | Trial quota; local rate-guard caps at 5 RPM / 10s spacing; 429s are account-level |
| `openrouter/*:free` | Gemma 4, Ling, ERNIE, Particle, LFM3, Nemotron variants | Free variants are rate-limited; good fallback, not a parallel-work backbone |
| `vercel/*` | Beam, Flint, Ampersand, Glyph, Nemotron Lightning | Provider quota; less tested in this setup |
| `google/*` | Gemma 4 31B / 26B | Free direct entries; may depend on the Google credential |
| `zai/*` | GLM-4.5 / 4.6V / 4.7 Flash | Free flash tier; older than GLM-5.3 |
| `groq/*` | ALLaM-2-7b | Not agent-capable (4k, no tools) |

---

## Notes on duplicates

The following were collapsed to one entry each:

- **DeepSeek V4.1 Flash** — `opencode` + `nvidia`
- **Nemotron 3.5 Lightning** — `opencode` + `nvidia` + `openrouter` + `vercel`
- **Nemotron 3.5 Super VL** — `opencode` + `nvidia` + `openrouter`
- **Nemotron 3 Ultra 550B** — `nvidia` + `openrouter`
- **Nemotron 3 Nano Omni** — `nvidia` + `openrouter`
- **Gemma 4 31B** — `google` + `nvidia` + `openrouter`
- **Gemma 4 26B A4B** — `google` + `openrouter`
- **Beam 1.0** — `opencode` + `vercel` (two entries)
- **Flint 40B** — `opencode` + `openrouter` + `vercel`

No model appears twice within a category.
