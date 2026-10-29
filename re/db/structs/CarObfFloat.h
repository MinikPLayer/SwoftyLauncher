// struct: CarObfFloat
// subsystem: car
// size: 0x10  L2 decomp:re/decomp/142e88290.c (spline fields +0x60/+0x70 and samples 16 bytes apart)
// notes: XOR-obfuscated float used for VTF numbers at runtime (spline samples, aero/engine coefficients).
// float bits = a ^ b. Decode: u32 v = *(u32*)(p+0) ^ *(u32*)(p+8). Encode a value f with a fresh key k:
// *(u32*)(p+8) = k; *(u32*)(p+0) = bits(f) ^ k (keeping the old key works too: write bits(f) ^ old_b).
// Writers draw a 64-bit key per field: key = (mt() << 32) | mt() from g_Car_ObfRng (0x143929b90),
// stored as a qword at +8 (e.g. Game_Read_VtfSuspensionSystem 0x1446aa7a0..0x1446aa7c0). The decoder
// reads only the low dwords.
struct CarObfFloat {
    /* 0x00 */ uint32_t a;     // L2 decomp:re/decomp/142e88290.c;xref:0x142e8829a,0x142e882e5 value bits ^ key
    /* 0x08 */ uint32_t b;     // L2 decomp:re/decomp/142e88290.c;xref:0x142e882a1,0x142e882f4 key (xor partner)
};
