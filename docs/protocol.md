# Meter protocol

The app talks to the meter over **Bluetooth LE GATT**. Every update is one
small JSON object written to the DATA characteristic. Messages are independent,
and each one only updates its own part of the meter's model, so the app can send
just what changed.

## GATT

| | UUID | Properties |
|---|---|---|
| Service | `589b0001-eb3f-4774-98f5-45672a450cf3` | |
| DATA | `589b0002-eb3f-4774-98f5-45672a450cf3` | write (encryption required), max 512 bytes |
| REQUEST | `589b0003-eb3f-4774-98f5-45672a450cf3` | notify / read: the meter sends `r` to ask for an immediate refresh (on connect) |
| Battery | `0x180F` / `0x2A19` | standard Battery Service, so Windows shows the charge level |

Advertised name: **`OpenRouter Meter`**. The service UUID is in the advertising
packet and the name is in the scan response.

### Pairing and security

- Pairing is LE Secure Connections, "Just Works" (the meter has no keyboard),
  with bonding. DATA can only be written over an encrypted link.
- The meter accepts a **new** PC only when it has no pairing yet, or for 2
  minutes after the user holds PWR for 3 s and releases it. Any other pairing
  attempt is rejected and its bond deleted.
- The app writes **with response**. Windows handles long writes on any adapter,
  and the response throttles sending so the meter's 8-slot queue never
  overflows. The app still keeps every message under **240 bytes**.

## Messages

All money is **integer US cents**. Days are **UTC**, as on OpenRouter.

### `core` — every poll (~20 s)

```json
{"m":"core","bal":746,"cr":3500,"day":3,"mon":167,"s30":2754,"rq30":1964,
 "yd":33,"yrq":102,"ytk":2945920,"now":1792006370,"h24":0}
```

| Field | Meaning |
|---|---|
| `bal` | Balance: total credits − total usage |
| `cr` | Total credits purchased |
| `day` | Spend today (best live estimate, see below) |
| `mon` | Spend this calendar month (UTC) |
| `s30` | Spend over the 30-day window ending today |
| `rq30` | Requests over the completed days of that window |
| `yd` / `yrq` / `ytk` | Yesterday's spend / requests / tokens (prompt + completion) |
| `now` | Optional local wall-clock time as epoch seconds (clock in the header). Omitted when the clock is off. |
| `h24` | 1 = 24-hour clock, 0 = 12-hour |

The meter treats a `core` message as a heartbeat. With none for 90 s, it marks
the data **STALE**.

### `hist` — 30-day daily spend

```json
{"m":"hist","d":[0,2187,184, … ,33,3]}
```

30 values, oldest first. The last value is today and equals `core.day`.

### `models` / `prov` — traffic share

```json
{"m":"models","rows":[["deepseek-v4.1-flash",1085,256], … ,["others",274,53]],"rq":1964}
{"m":"prov","rows":[["streamlake",391,65], … ],"rq":1964,"n":32}
```

Each row is `[name, requests_30d, cents_30d]`: the top 4 by requests, then
`others` holding everything else, so shares add up to `rq`. Model names drop
the provider prefix. Provider names drop quantisation suffixes
(`deepinfra/fp8` → `deepinfra`). `n` is the number of distinct providers.

### `tok` — tokens (30 days)

```json
{"m":"tok","p":75468555,"c":1476829,"r":846864,"ca":47900659,"rq":1964,"fr":244,"usd":2751}
```

`p` prompt, `c` completion, `r` reasoning, `ca` cached (cache reads), `rq`
requests, `fr` requests that cost $0, `usd` spend in cents. The meter treats
`ca` as part of `p` and `r` as part of `c` (OpenAI-style accounting, consistent
with OpenRouter's numbers), so total tokens = `p + c`.

### `keys` — API keys

```json
{"m":"keys","rows":[["Default key",0,28,55,5000,2531,0], …]}
```

`[name, today, week, month, limit, limit_remaining, disabled]`. `limit` and
`limit_remaining` are −1 when the key has no limit. Up to 4 keys are sent:
enabled keys first, then by month-to-date spend.

### `cfg` — user settings

```json
{"m":"cfg","low":500,"bud":0,"scr":0,"dim":0}
```

`low` low-balance alert (cents, 0 = off), `bud` daily budget alert (cents,
0 = off), `scr` screen bitmask (bit 0 credits, 1 routes, 2 spend, 3 tokens,
4 providers, 5 keys; 0 = all), `dim` minutes to auto-dim (0 = never).

### `err`

```json
{"m":"err","code":"key"}
```

The management key is missing or was rejected. The meter shows guidance until
the next `core` arrives.

## How the app computes "spend today"

OpenRouter's `/activity` only covers **completed** UTC days. For today the app
uses the larger of:

1. the sum of every key's `usage_daily` from `/keys` (computed by OpenRouter),
2. the rise in lifetime `total_usage` (from `/credits`) since the app first saw
   the current UTC day. This catches usage that isn't tied to a key, such as the
   web chatroom.

Both can only under-count, so the larger is the better estimate. Money from
`/activity` is summed in exact dollars and rounded to cents once. Rounding each
row first would lose thousands of sub-cent requests.
