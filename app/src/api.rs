//! OpenRouter API client. Uses only free, read-only endpoints with the
//! management key: /credits, /activity and /keys.
use serde::Deserialize;
use std::time::Duration;

const BASE: &str = "https://openrouter.ai/api/v1";

#[derive(Debug, Clone, Default, Deserialize, PartialEq)]
pub struct Credits {
    #[serde(default)]
    pub total_credits: f64,
    #[serde(default)]
    pub total_usage: f64,
}

/// One /activity row: a (UTC day, model, provider endpoint) aggregate.
#[derive(Debug, Clone, Default, Deserialize, PartialEq)]
pub struct ActivityRow {
    #[serde(default, deserialize_with = "de_str")]
    pub date: String,
    #[serde(default, deserialize_with = "de_str")]
    pub model: String,
    #[serde(default, deserialize_with = "de_str")]
    pub provider_name: String,
    #[serde(default, deserialize_with = "de_f64")]
    pub usage: f64,
    #[serde(default, deserialize_with = "de_i64")]
    pub requests: i64,
    #[serde(default, deserialize_with = "de_i64")]
    pub prompt_tokens: i64,
    #[serde(default, deserialize_with = "de_i64")]
    pub completion_tokens: i64,
    #[serde(default, deserialize_with = "de_i64")]
    pub reasoning_tokens: i64,
    #[serde(default, deserialize_with = "de_i64")]
    pub cached_tokens: i64,
}

#[derive(Debug, Clone, Default, Deserialize, PartialEq)]
pub struct KeyInfo {
    #[serde(default, deserialize_with = "de_str")]
    pub name: String,
    #[serde(default, deserialize_with = "de_str")]
    pub label: String,
    #[serde(default)]
    pub disabled: bool,
    #[serde(default)]
    pub limit: Option<f64>,
    #[serde(default)]
    pub limit_remaining: Option<f64>,
    #[serde(default, deserialize_with = "de_f64")]
    pub usage_daily: f64,
    #[serde(default, deserialize_with = "de_f64")]
    pub usage_weekly: f64,
    #[serde(default, deserialize_with = "de_f64")]
    pub usage_monthly: f64,
}

// Tolerate nulls / wrong types from the API rather than failing a whole poll.
fn de_f64<'de, D: serde::Deserializer<'de>>(d: D) -> Result<f64, D::Error> {
    let v = serde_json::Value::deserialize(d)?;
    Ok(match v {
        serde_json::Value::Number(n) => n.as_f64().unwrap_or(0.0),
        serde_json::Value::String(s) => s.parse().unwrap_or(0.0),
        _ => 0.0,
    })
}
fn de_i64<'de, D: serde::Deserializer<'de>>(d: D) -> Result<i64, D::Error> {
    Ok(de_f64(d)?.round() as i64)
}
fn de_str<'de, D: serde::Deserializer<'de>>(d: D) -> Result<String, D::Error> {
    let v = serde_json::Value::deserialize(d)?;
    Ok(match v {
        serde_json::Value::String(s) => s,
        serde_json::Value::Null => String::new(),
        other => other.to_string(),
    })
}

#[derive(Debug)]
pub enum ApiError {
    /// 401/403 — key missing, wrong, or not a management key.
    Auth,
    /// Network failure / 5xx / bad JSON — try again next poll.
    Transient(String),
}

impl std::fmt::Display for ApiError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ApiError::Auth => write!(f, "management key rejected"),
            ApiError::Transient(e) => write!(f, "{e}"),
        }
    }
}

#[derive(Deserialize)]
struct Envelope<T> {
    data: T,
}

pub struct Client {
    http: reqwest::Client,
}

impl Client {
    pub fn new() -> Self {
        let http = reqwest::Client::builder()
            .timeout(Duration::from_secs(20))
            .user_agent(concat!("openrouter-meter/", env!("CARGO_PKG_VERSION")))
            .build()
            .expect("http client");
        Client { http }
    }

    async fn get<T: for<'de> Deserialize<'de>>(&self, path: &str, key: &str) -> Result<T, ApiError> {
        let resp = self
            .http
            .get(format!("{BASE}{path}"))
            .bearer_auth(key)
            .send()
            .await
            .map_err(|e| ApiError::Transient(format!("{path}: {}", e.without_url())))?;
        let status = resp.status();
        if status.as_u16() == 401 || status.as_u16() == 403 {
            return Err(ApiError::Auth);
        }
        if !status.is_success() {
            return Err(ApiError::Transient(format!("{path}: HTTP {status}")));
        }
        resp.json::<Envelope<T>>()
            .await
            .map(|e| e.data)
            .map_err(|e| ApiError::Transient(format!("{path}: {e}")))
    }

    pub async fn credits(&self, key: &str) -> Result<Credits, ApiError> {
        self.get("/credits", key).await
    }
    pub async fn activity(&self, key: &str) -> Result<Vec<ActivityRow>, ApiError> {
        self.get("/activity", key).await
    }
    pub async fn keys(&self, key: &str) -> Result<Vec<KeyInfo>, ApiError> {
        self.get("/keys?include_disabled=true", key).await
    }
}

impl Default for Client {
    fn default() -> Self {
        Self::new()
    }
}
