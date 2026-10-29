// struct: CarSpline
// subsystem: car
// size: 0x2a8  L2 decomp:re/decomp/144685680.c;xref:0x1446857d0 (Car_Read_Engine steps 0x2a8 per spline; car-b: RevsPowerSpline at data+0x10, next field +0x2b8)
// notes: runtime VTF spline evaluated by Car_Spline_EvalObfuscated (0x142e88290). Two modes chosen by
// sample_count (the last index, n-1): >= 0 uniform table (index = x*scale + offset, clamp, lerp between samples[i] and
// samples[i+1], samples inline from +0x98, one CarObfFloat each); < 0 point list (32-byte points in
// [points_begin, points_end): x = CarObfFloat at +0, y = CarObfFloat at +0x10; first point with x > input,
// lerped). Every number is a CarObfFloat (a ^ b). Inline uniform capacity: (0x2a8-0x98)/0x10 = 33 samples; the table holds [+0x90]+1 samples (V1325).
struct CarSpline {
    /* 0x0008 */ void *points_begin;        // L2 decomp:re/decomp/142e88290.c;xref:0x142e88344 point mode: 32-byte {CarObfFloat x; CarObfFloat y} records
    /* 0x0010 */ void *points_end;          // L2 decomp:re/decomp/142e88290.c;xref:0x142e88344 point mode end
    /* 0x0060 */ CarObfFloat scale;         // L2 decomp:re/decomp/142e88290.c;xref:0x142e8829a,0x142e882a1 uniform mode x scale
    /* 0x0070 */ CarObfFloat offset;        // L2 decomp:re/decomp/142e88290.c;xref:0x142e8829d,0x142e882a8 uniform mode x offset
    /* 0x0090 */ int32_t sample_count;      // L2 decomp:re/decomp/142e88290.c;xref:0x142e88290 the last sample index n-1 (written at 0x14466ddc6 by Car_Assets_FindSpline), so the uniform table holds sample_count+1 samples; -1 (negative) = point mode (V1325)
    /* 0x0098 */ CarObfFloat samples[1];    // L2 decomp:re/decomp/142e88290.c;xref:0x142e882e5,0x142e882f4 inline uniform samples (sample_count+1 entries)
};
