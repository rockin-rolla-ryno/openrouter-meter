# Setting up your OpenRouter Meter

This guide takes about **10 minutes**. You don't need to know anything about
programming — just follow the steps in order.

**You will need**

- The display: a **Waveshare ESP32-C6-Touch-AMOLED-2.16**
- A **Windows 10 or 11** PC with Bluetooth
- A **USB-C cable** that can carry data (some phone-charging cables can't — if
  step 2 doesn't see the meter, try another cable)
- Your **OpenRouter** account

---

## Step 1 — Get your OpenRouter "management key"

The meter needs permission to *read* your usage. A management key gives exactly
that: it can see your balance and usage but **cannot spend your credits**.

1. Go to **<https://openrouter.ai/settings/management-keys>** and sign in.
2. Click **Create** (or "New key"). Name it something like `Desk meter`.
3. Copy the key that appears (it starts with `sk-or-v1-`). Keep the page open
   or paste the key into a note for a minute — you'll need it in step 4.

> Treat the key like a password: don't share it or post it online.

---

## Step 2 — Put the meter software on the display (one time)

The display needs the OpenRouter Meter software installed on it. The easiest
way is from your web browser.

1. Open **Google Chrome** or **Microsoft Edge** (other browsers can't do this)
   and go to the **web installer**:
   **<https://rockin-rolla-ryno.github.io/openrouter-meter/>**
2. Put the display into **install mode**:
   1. Unplug the USB cable from the display.
   2. Find the small **BOOT** button on the side of the display and **press and hold it**.
   3. While still holding BOOT, **plug the USB cable back in** (other end in your PC).
   4. Keep holding for about **3 seconds**, then let go. The screen stays black —
      that's correct.
3. On the web page click **Install**, pick the port that appears (it's usually
   called *"USB JTAG/serial debug unit"*), and click **Connect**, then **Install**.
4. Wait for **"Installation complete"** (about a minute).
5. **Restart the display:** hold the **PWR** button for **8 seconds** until the
   screen goes off, then press PWR **once** briefly to turn it back on.

The display now shows a glowing circle and **"Ready to pair"**. 🎉

> The display can run on its battery or stay plugged into USB — either is fine.

---

## Step 3 — Install the app on your PC

1. Download **`OpenRouterMeter.exe`** from the
   **[latest release](https://github.com/rockin-rolla-ryno/openrouter-meter/releases/latest)**.
2. **Double-click** it.
   - If Windows shows *"Windows protected your PC"*, click **More info** →
     **Run anyway**. (This appears for any new app that isn't from a big
     publisher.)
3. A message pops up saying OpenRouter Meter is running, and **Notepad** opens
   with your settings.

That's it for installing — the app copies itself to the right place and will
start automatically whenever Windows starts. You can delete the downloaded file.

---

## Step 4 — Paste your key

In the Notepad window that opened:

1. Find the line `management_key =`
2. Paste your key **after** the `=` sign, so it looks like
   `management_key = sk-or-v1-abc123…`
3. Press **Ctrl + S** to save, then close Notepad.

---

## Step 5 — Done!

Within about 30 seconds the app finds your display and **pairs with it
automatically** (no PIN, nothing to click). The display fills with your
balance and starts animating.

- **Tap the screen** to see the next screen. There are six.
- Look for the app's icon near the clock (you may need to click the **^**
  arrow). Its colour tells you how things are:
  - 🟢 green — live and connected
  - 🟠 amber — looking for the meter / something needs attention
  - 🔴 red — a problem (hover over the icon to read it)
- **Right-click** the icon for *Refresh now*, *Settings*, *Start with Windows*,
  and more.

---

## Changing settings

Right-click the tray icon → **Settings…**. Notepad opens; change a value, save
(Ctrl + S) and the meter updates within seconds.

| Setting | What it does | Example |
|---|---|---|
| `management_key` | Your OpenRouter management key | `sk-or-v1-…` |
| `low_balance_alert` | Alert when your balance drops below this many dollars (`0` = off) | `5.00` |
| `daily_budget_alert` | Alert when today's spend reaches this many dollars (`0` = off) | `2.00` |
| `clock` | Clock in the corner: `auto`, `24h`, `12h` or `off` | `auto` |
| `screens` | Which screens to show | `credits, spend, keys` or `all` |
| `dim_after_minutes` | Dim the screen after this many minutes without a touch (`0` = never) | `10` |
| `poll_seconds` | How often to refresh (10 or more) | `20` |

---

## Using the display

| Do this | To |
|---|---|
| Tap the screen, or press **BOOT** | Go to the next screen |
| Press **KEY** | Go back a screen |
| Press **PWR** briefly | Change brightness |
| Hold **PWR** for 3 seconds, then let go | Pair with a different PC |
| Hold **PWR** for 8 seconds | Turn the display off (press briefly to turn on) |

When an **alert** fires, the display brightens, its edges glow and a banner
explains why. Tap once to dismiss the banner.

---

## If something isn't right

**The display says "Waiting for your PC"**
The display is paired but the app isn't connected. Make sure your PC is awake,
Bluetooth is on, and the app is running (look for its icon by the clock; if
it's missing, open the Start menu and search for *OpenRouterMeter*, or run the
downloaded file again).

**The tray icon says "Pairing needed"**
The display only accepts a new PC when you allow it. Hold the display's **PWR**
button for **3 seconds** and let go — it shows *"Ready to pair"* — then wait a
few seconds; the app pairs by itself.

**The display says "Check your OpenRouter key"**
The key is missing, mistyped, or it's a normal API key instead of a
**management** key. Right-click the tray icon → *Settings…* and paste the
management key again (step 1 and step 4).

**The top of the display says "STALE"**
The display hasn't had fresh numbers for over 90 seconds — usually your PC went
to sleep or lost internet. It recovers by itself.

**"Bluetooth is off or missing"**
Turn Bluetooth on in Windows *Settings → Bluetooth & devices*.

**Moving the display to a new PC**
Install the app on the new PC (steps 3–4), then hold the display's **PWR**
button for 3 seconds and let go. The new PC pairs automatically.

**Uninstalling**
Right-click the tray icon → *Uninstall…*. Optionally remove the display from
*Settings → Bluetooth & devices*, and delete the folder
`%APPDATA%\OpenRouterMeter` (your settings).

**Still stuck?** Right-click the tray icon → *Open log*, and include that file
(it never contains your key) when you ask for help on the project's
[Issues page](https://github.com/rockin-rolla-ryno/openrouter-meter/issues).
