#[repr(C)]
pub struct KernelSprite {
    radius: f32, offset_x: f32, offset_y: f32, e00: f32, e10: f32, e01: f32, e11: f32,
    flip_x: u32, flip_y: u32, hflip: u32, width: i32, height: i32,
    rows: *const *const u8, mask: u32,
}
#[repr(C)]
pub struct KernelQuery { sprite: *const KernelSprite, dx: f32, dy: f32 }
#[inline]
unsafe fn hit(s: &KernelSprite, dx: f32, dy: f32) -> u8 {
    if dx.mul_add(dx, dy * dy) > s.radius * s.radius { return 0; }
    let mut x = s.e00.mul_add(dx, s.e10 * dy);
    let mut y = s.e01.mul_add(dx, s.e11 * dy);
    x = if s.flip_x != 0 { -x } else { x };
    y = if s.flip_y != 0 { -y } else { y };
    x = if s.hflip != 0 { -x } else { x };
    let ix = (x - s.offset_x).floor() as i32;
    let iy = (y - s.offset_y).floor() as i32;
    (ix >= 0 && iy >= 0 && ix < s.width && iy < s.height &&
        *(*s.rows.add(iy as usize)).add(ix as usize) as u32 != s.mask) as u8
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_hit(s: *const KernelSprite, dx: f32, dy: f32) -> u8 {
    hit(&*s, dx, dy)
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_batch(queries: *const KernelQuery, output: *mut u8, count: usize) {
    for i in 0..count {
        let q = &*queries.add(i);
        *output.add(i) = hit(&*q.sprite, q.dx, q.dy);
    }
}
