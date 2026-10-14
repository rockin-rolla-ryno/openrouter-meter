//! Tray icon artwork, drawn at runtime (no image files): a hub ring in the
//! status colour with a cyan packet at its centre.
use tray_icon::Icon;

pub const MINT: [u8; 3] = [0x3E, 0xE6, 0xA0];
pub const AMBER: [u8; 3] = [0xFF, 0xB5, 0x47];
pub const CORAL: [u8; 3] = [0xFF, 0x5D, 0x73];
pub const GREY: [u8; 3] = [0x8A, 0x8F, 0xA3];
const CYAN: [u8; 3] = [0x38, 0xE1, 0xFF];
const HUB: [u8; 3] = [0x0E, 0x0F, 0x15];

pub fn rgba(size: u32, ring: [u8; 3]) -> Vec<u8> {
    let s = size as f32;
    let c = s / 2.0;
    let r_out = s * 0.46;
    let r_ring = s * 0.33;
    let r_dot = s * 0.15;
    let mut px = vec![0u8; (size * size * 4) as usize];
    // 4x4 supersampling for smooth edges.
    for y in 0..size {
        for x in 0..size {
            let mut acc = [0f32; 4];
            for sy in 0..4 {
                for sx in 0..4 {
                    let fx = x as f32 + (sx as f32 + 0.5) / 4.0 - c;
                    let fy = y as f32 + (sy as f32 + 0.5) / 4.0 - c;
                    let d = (fx * fx + fy * fy).sqrt();
                    let col = if d <= r_dot {
                        Some(CYAN)
                    } else if d <= r_ring {
                        Some(HUB)
                    } else if d <= r_out {
                        Some(ring)
                    } else {
                        None
                    };
                    if let Some(k) = col {
                        acc[0] += k[0] as f32;
                        acc[1] += k[1] as f32;
                        acc[2] += k[2] as f32;
                        acc[3] += 255.0;
                    }
                }
            }
            let i = ((y * size + x) * 4) as usize;
            let n = acc[3] / 255.0;
            if n > 0.0 {
                px[i] = (acc[0] / n) as u8;
                px[i + 1] = (acc[1] / n) as u8;
                px[i + 2] = (acc[2] / n) as u8;
                px[i + 3] = (acc[3] / 16.0) as u8;
            }
        }
    }
    px
}

pub fn icon(ring: [u8; 3]) -> Icon {
    Icon::from_rgba(rgba(32, ring), 32, 32).expect("icon")
}
