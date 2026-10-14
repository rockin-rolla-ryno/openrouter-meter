// Embed the app icon and version info into OpenRouterMeter.exe (Windows only).
fn main() {
    #[cfg(windows)]
    {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("assets/icon.ico");
        res.set("FileDescription", "OpenRouter Meter");
        res.set("ProductName", "OpenRouter Meter");
        res.set("LegalCopyright", "PolyForm Noncommercial 1.0.0");
        if let Err(e) = res.compile() {
            println!("cargo:warning=icon not embedded: {e}");
        }
    }
}
