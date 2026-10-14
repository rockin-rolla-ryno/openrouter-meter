//! Turn OpenRouter API data into the meter's messages (docs/protocol.md).
//!
//! Every message is one small JSON object (≤ MAX_BYTES) so it fits a single
//! Bluetooth write on any adapter. Money is integer cents.
use crate::api::{ActivityRow, Credits, KeyInfo};
use crate::config::{Clock, Settings};
use chrono::{Datelike, Duration, NaiveDate};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap, HashSet};

pub const HIST_DAYS: i64 = 30;
pub const ROWS: usize = 4; // named rows on Routes / Providers (+ "others")
pub const KEYS: usize = 4;
pub const MAX_BYTES: usize = 240;
const NAME_MAX: usize = 22;

pub fn cents(usd: f64) -> i64 {
    if usd.is_finite() { (usd * 100.0).round() as i64 } else { 0 }
}

fn day_of(row: &ActivityRow) -> Option<NaiveDate> {
    NaiveDate::parse_from_str(row.date.get(..10)?, "%Y-%m-%d").ok()
}

/// Per-day / per-model / per-provider aggregates over the 30-day window
/// ending today (UTC). /activity only reports completed days, so "today" is
/// filled from live sources by the caller.
/// Money is accumulated in exact dollars and rounded to cents only when a
/// total is read: /activity has thousands of sub-cent rows, and rounding each
/// one first would lose real spend.
#[derive(Debug, Default, Clone)]
pub struct Summary {
    pub by_day_usd: BTreeMap<NaiveDate, f64>,
    pub by_day_cents: BTreeMap<NaiveDate, i64>,
    pub req_by_day: BTreeMap<NaiveDate, i64>,
    pub tok_by_day: BTreeMap<NaiveDate, i64>,
    pub models: HashMap<String, (i64, i64)>,    // slug -> (requests, cents)
    pub providers: HashMap<String, (i64, i64)>, // base provider -> (requests, cents)
    models_usd: HashMap<String, f64>,
    providers_usd: HashMap<String, f64>,
    usd: f64,
    pub prompt: i64,
    pub completion: i64,
    pub reasoning: i64,
    pub cached: i64,
    pub requests: i64,
    pub free_requests: i64,
    pub cents: i64,
}

/// "deepinfra/fp8" -> "deepinfra": quantisation variants are the same provider.
pub fn base_provider(name: &str) -> String {
    let n = name.split('/').next().unwrap_or(name).trim();
    if n.is_empty() { "unknown".into() } else { n.to_ascii_lowercase() }
}

/// "deepseek/deepseek-v4.1-flash" -> "deepseek-v4.1-flash".
pub fn short_model(slug: &str) -> String {
    slug.rsplit_once('/').map(|(_, m)| m).unwrap_or(slug).to_string()
}

pub fn summarize(rows: &[ActivityRow], today: NaiveDate) -> Summary {
    let start = today - Duration::days(HIST_DAYS - 1);
    let mut s = Summary::default();
    for r in rows {
        let Some(d) = day_of(r) else { continue };
        if d < start || d > today {
            continue;
        }
        let usd = if r.usage.is_finite() { r.usage } else { 0.0 };
        *s.by_day_usd.entry(d).or_default() += usd;
        *s.req_by_day.entry(d).or_default() += r.requests;
        *s.tok_by_day.entry(d).or_default() += r.prompt_tokens + r.completion_tokens;
        s.models.entry(r.model.clone()).or_default().0 += r.requests;
        *s.models_usd.entry(r.model.clone()).or_default() += usd;
        let prov = base_provider(&r.provider_name);
        s.providers.entry(prov.clone()).or_default().0 += r.requests;
        *s.providers_usd.entry(prov).or_default() += usd;
        s.prompt += r.prompt_tokens;
        s.completion += r.completion_tokens;
        s.reasoning += r.reasoning_tokens;
        s.cached += r.cached_tokens;
        s.requests += r.requests;
        if usd <= 0.0 {
            s.free_requests += r.requests;
        }
        s.usd += usd;
    }
    // Round once, at the end.
    s.by_day_cents = s.by_day_usd.iter().map(|(d, u)| (*d, cents(*u))).collect();
    for (k, v) in s.models.iter_mut() {
        v.1 = cents(s.models_usd[k]);
    }
    for (k, v) in s.providers.iter_mut() {
        v.1 = cents(s.providers_usd[k]);
    }
    s.cents = cents(s.usd);
    s
}

/// Live spend today and this month from the keys list (OpenRouter computes
/// usage_daily / usage_monthly per key in UTC).
pub fn keys_totals(keys: &[KeyInfo]) -> (i64, i64) {
    keys.iter().fold((0, 0), |(d, m), k| (d + cents(k.usage_daily), m + cents(k.usage_monthly)))
}

/// Inputs gathered by one poll. Any source may be missing this cycle.
pub struct Poll<'a> {
    pub credits: &'a Credits,
    pub activity: Option<&'a Summary>,
    pub keys: Option<&'a [KeyInfo]>,
    pub live_day_cents: i64, // balance-delta fallback
    pub today: NaiveDate,
    pub now_local: Option<i64>,
    pub clock_24h: bool,
}

/// Best estimate of spend today: every source can only under-count
/// (keys miss deleted keys and the chatroom; the balance delta misses spend
/// before the app started), so take the largest.
pub fn spend_today(p: &Poll) -> i64 {
    let keys_day = p.keys.map(|k| keys_totals(k).0).unwrap_or(0);
    keys_day.max(p.live_day_cents)
}

pub fn core(p: &Poll) -> Value {
    let day = spend_today(p);
    let yday = p.today - Duration::days(1);
    let mut msg = json!({
        "m": "core",
        "bal": cents(p.credits.total_credits) - cents(p.credits.total_usage),
        "cr": cents(p.credits.total_credits),
        "day": day,
    });
    let month_keys = p.keys.map(|k| keys_totals(k).1).unwrap_or(0);
    let mut month = month_keys;
    if let Some(s) = p.activity {
        let month_start = p.today.with_day(1).unwrap_or(p.today);
        // Sum exact dollars, round once (see Summary).
        let activity_month = cents(s.by_day_usd.range(month_start..p.today).map(|(_, u)| u).sum::<f64>());
        month = month.max(activity_month + day);
        let s30 = cents(s.by_day_usd.range(..p.today).map(|(_, u)| u).sum::<f64>()) + day;
        msg["s30"] = json!(s30);
        msg["rq30"] = json!(s.requests);
        msg["yd"] = json!(s.by_day_cents.get(&yday).copied().unwrap_or(0));
        msg["yrq"] = json!(s.req_by_day.get(&yday).copied().unwrap_or(0));
        msg["ytk"] = json!(s.tok_by_day.get(&yday).copied().unwrap_or(0));
    }
    msg["mon"] = json!(month);
    if let Some(now) = p.now_local {
        msg["now"] = json!(now);
        msg["h24"] = json!(if p.clock_24h { 1 } else { 0 });
    }
    msg
}

pub fn hist(s: &Summary, today: NaiveDate, day: i64) -> Value {
    let d: Vec<i64> = (0..HIST_DAYS)
        .map(|i| {
            let date = today - Duration::days(HIST_DAYS - 1 - i);
            if date == today { day } else { s.by_day_cents.get(&date).copied().unwrap_or(0) }
        })
        .collect();
    json!({ "m": "hist", "d": d })
}

/// Top rows by requests (ties: spend, then name), the rest folded into
/// "others" so shares on the meter add up to 100%.
fn share_rows(map: &HashMap<String, (i64, i64)>, name: impl Fn(&str) -> String) -> (Vec<Value>, i64) {
    let mut v: Vec<(&String, &(i64, i64))> = map.iter().filter(|(_, (r, _))| *r > 0).collect();
    v.sort_by(|a, b| b.1 .0.cmp(&a.1 .0).then(b.1 .1.cmp(&a.1 .1)).then(a.0.cmp(b.0)));
    let total: i64 = v.iter().map(|(_, (r, _))| r).sum();
    let mut rows: Vec<Value> = v.iter().take(ROWS).map(|(k, (r, c))| json!([name(k), r, c])).collect();
    if v.len() > ROWS {
        let (r, c) = v[ROWS..].iter().fold((0, 0), |a, (_, (r, c))| (a.0 + r, a.1 + c));
        rows.push(json!(["others", r, c]));
    }
    (rows, total)
}

pub fn models(s: &Summary) -> Value {
    let (rows, total) = share_rows(&s.models, short_model);
    fit(json!({ "m": "models", "rows": rows, "rq": total }))
}

pub fn providers(s: &Summary) -> Value {
    let (rows, total) = share_rows(&s.providers, |n| n.to_string());
    let distinct = s.providers.iter().filter(|(_, (r, _))| *r > 0).map(|(k, _)| k).collect::<HashSet<_>>().len();
    fit(json!({ "m": "prov", "rows": rows, "rq": total, "n": distinct }))
}

pub fn tokens(s: &Summary) -> Value {
    json!({
        "m": "tok",
        "p": s.prompt, "c": s.completion, "r": s.reasoning, "ca": s.cached,
        "rq": s.requests, "fr": s.free_requests, "usd": s.cents,
    })
}

pub fn keys(list: &[KeyInfo]) -> Value {
    let mut v: Vec<&KeyInfo> = list.iter().collect();
    // Most-used first; disabled keys last.
    v.sort_by(|a, b| {
        a.disabled.cmp(&b.disabled)
            .then(cents(b.usage_monthly).cmp(&cents(a.usage_monthly)))
            .then(a.name.cmp(&b.name))
    });
    let rows: Vec<Value> = v
        .iter()
        .take(KEYS)
        .map(|k| {
            let name = if k.name.trim().is_empty() { k.label.clone() } else { k.name.clone() };
            json!([
                name,
                cents(k.usage_daily),
                cents(k.usage_weekly),
                cents(k.usage_monthly),
                k.limit.map(cents).unwrap_or(-1),
                k.limit_remaining.map(cents).unwrap_or(-1),
                if k.disabled { 1 } else { 0 }
            ])
        })
        .collect();
    fit(json!({ "m": "keys", "rows": rows }))
}

pub fn settings_msg(s: &Settings) -> Value {
    json!({
        "m": "cfg",
        "low": s.low_balance_alert_cents,
        "bud": s.daily_budget_alert_cents,
        "scr": s.screens_mask,
        "dim": s.dim_after_minutes,
    })
}

pub fn key_error() -> Value {
    json!({ "m": "err", "code": "key" })
}

pub fn clock_enabled(s: &Settings) -> bool {
    s.clock != Clock::Off
}

/// Shorten row names until the message fits one write.
pub fn fit(mut msg: Value) -> Value {
    let mut limit = NAME_MAX;
    loop {
        if let Some(rows) = msg.get_mut("rows").and_then(|r| r.as_array_mut()) {
            for r in rows.iter_mut() {
                if let Some(n) = r.get(0).and_then(|n| n.as_str()) {
                    if n.chars().count() > limit {
                        r[0] = json!(n.chars().take(limit).collect::<String>());
                    }
                }
            }
        }
        if serde_json::to_string(&msg).map(|t| t.len()).unwrap_or(0) <= MAX_BYTES || limit <= 6 {
            return msg;
        }
        limit -= 2;
    }
}

pub fn encode(msg: &Value) -> Vec<u8> {
    serde_json::to_vec(msg).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn d(s: &str) -> NaiveDate {
        NaiveDate::parse_from_str(s, "%Y-%m-%d").unwrap()
    }

    fn row(date: &str, model: &str, prov: &str, usd: f64, req: i64) -> ActivityRow {
        ActivityRow {
            date: format!("{date} 00:00:00"),
            model: model.into(),
            provider_name: prov.into(),
            usage: usd,
            requests: req,
            prompt_tokens: 1000 * req,
            completion_tokens: 100 * req,
            reasoning_tokens: 10 * req,
            cached_tokens: 500 * req,
        }
    }

    fn sample() -> Vec<ActivityRow> {
        vec![
            row("2026-10-13", "deepseek/deepseek-v4.1-flash", "streamlake/fp8", 0.30, 9),
            row("2026-10-13", "openai/gpt-6-astra", "openai", 0.03, 1),
            row("2026-10-02", "openai/gpt-6-astra", "deepinfra/fp8", 0.20, 2),
            row("2026-09-16", "openai/gpt-6-astra", "openai", 21.87, 161),
            row("2026-09-14", "x/old", "old", 9.99, 99), // outside the 30-day window
            row("2026-10-12", "nvidia/free-model", "nvidia", 0.0, 5),
        ]
    }

    const TODAY: &str = "2026-10-14";

    fn credits() -> Credits {
        Credits { total_credits: 35.0, total_usage: 27.54 }
    }

    fn keys_list() -> Vec<KeyInfo> {
        vec![
            KeyInfo { name: "rynos".into(), usage_daily: 0.03, usage_weekly: 0.08, usage_monthly: 0.11, limit: Some(50.0), limit_remaining: Some(49.89), ..Default::default() },
            KeyInfo { name: "Default key".into(), usage_daily: 0.0, usage_weekly: 0.28, usage_monthly: 0.55, limit: Some(50.0), limit_remaining: Some(25.31), ..Default::default() },
        ]
    }

    #[test]
    fn summary_window_and_providers() {
        let s = summarize(&sample(), d(TODAY));
        assert_eq!(s.requests, 9 + 1 + 2 + 161 + 5);
        assert_eq!(s.providers.get("streamlake"), Some(&(9, 30)));
        assert_eq!(s.providers.get("deepinfra"), Some(&(2, 20)));
        assert!(!s.providers.contains_key("old"));
        assert_eq!(s.free_requests, 5);
        assert_eq!(s.cached, 500 * 178);
    }

    #[test]
    fn core_uses_best_today_and_live_month() {
        let s = summarize(&sample(), d(TODAY));
        let k = keys_list();
        let p = Poll { credits: &credits(), activity: Some(&s), keys: Some(&k), live_day_cents: 1,
                       today: d(TODAY), now_local: Some(1_791_993_600), clock_24h: true };
        let c = core(&p);
        assert_eq!(c["bal"], 746);
        assert_eq!(c["day"], 3);                   // keys beat the balance delta
        // October so far: activity 20 + 33 (+ today 3) = 56 < keys' live 11 + 55 = 66.
        assert_eq!(c["mon"], 66);
        assert_eq!(c["yd"], 33);
        assert_eq!(c["yrq"], 10);
        assert_eq!(c["s30"], 30 + 3 + 20 + 2187 + 3);
        assert_eq!(c["now"], 1_791_993_600i64);
    }

    #[test]
    fn hist_ends_with_live_today() {
        let s = summarize(&sample(), d(TODAY));
        let h = hist(&s, d(TODAY), 7);
        let a = h["d"].as_array().unwrap();
        assert_eq!(a.len(), 30);
        assert_eq!(a[29], 7);
        assert_eq!(a[28], 33);
        assert_eq!(a[1], 2187);   // 2026-09-16 is day index 1 of the window
    }

    #[test]
    fn sub_cent_rows_are_not_lost() {
        // 300 rows of $0.004 = $1.20; rounding each row first would give $0.00.
        let rows: Vec<ActivityRow> = (0..300).map(|_| row("2026-10-10", "a/m", "p", 0.004, 1)).collect();
        let s = summarize(&rows, d(TODAY));
        assert_eq!(s.cents, 120);
        assert_eq!(s.models["a/m"].1, 120);
        assert_eq!(s.by_day_cents[&d("2026-10-10")], 120);
        assert_eq!(s.free_requests, 0);
    }

    #[test]
    fn share_rows_fold_others_and_sum_to_total() {
        let mut rows = vec![];
        for i in 0..9 {
            rows.push(row("2026-10-10", &format!("vendor/model-{i}-with-an-extremely-long-name"), &format!("p{i}/fp8"), 0.01, 20 - i));
        }
        let s = summarize(&rows, d(TODAY));
        let m = models(&s);
        let r = m["rows"].as_array().unwrap();
        assert_eq!(r.len(), 5);
        assert_eq!(r[4][0], "others");
        let sum: i64 = r.iter().map(|x| x[1].as_i64().unwrap()).sum();
        assert_eq!(sum, m["rq"].as_i64().unwrap());
        assert!(encode(&m).len() <= MAX_BYTES);
        let p = providers(&s);
        assert_eq!(p["n"], 9);
        assert!(encode(&p).len() <= MAX_BYTES);
    }

    #[test]
    fn keys_sorted_and_fit() {
        let k = keys(&keys_list());
        assert_eq!(k["rows"][0][0], "Default key");
        assert_eq!(k["rows"][0][4], 5000);
        assert_eq!(k["rows"][0][5], 2531);
        assert!(encode(&k).len() <= MAX_BYTES);
    }

    #[test]
    fn every_message_fits_one_write() {
        let s = summarize(&sample(), d(TODAY));
        let k = keys_list();
        let p = Poll { credits: &credits(), activity: Some(&s), keys: Some(&k), live_day_cents: 0,
                       today: d(TODAY), now_local: Some(1_791_993_600), clock_24h: false };
        for m in [core(&p), hist(&s, d(TODAY), 3), models(&s), providers(&s), tokens(&s), keys(&k),
                  settings_msg(&Settings::default()), key_error()] {
            assert!(encode(&m).len() <= MAX_BYTES, "{m}");
        }
    }
}
