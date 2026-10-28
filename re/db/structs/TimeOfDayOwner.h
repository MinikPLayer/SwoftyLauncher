// struct: TimeOfDayOwner
// subsystem: session
// size: ?
// notes: the object at GameWeatherSystem+0x1da8 that setupSession passes to the dataset build thunks
// (Sess_Tod_BuildSessionDataset_Thunk, 0x14479c560); class name unknown. Raised by ce-weather.
struct TimeOfDayOwner {
    /* 0x0d0 */ TodCurveBinding rain_intensity_curve[4]; // L1 decomp:re/decomp/14479b950.c "weather/rainfall/intensity" bound per weather dataset (+0xd0, +0x100, +0x130, +0x160); raised by vmknolle
    /* 0x2c8 */ void *rain_render;      // L1 decomp:re/decomp/14479c3f0.c gets the intensity at +0x75ca4 every update
    /* 0x2d0 */ TimeOfDaySystem *tod;   // L2 decomp:re/decomp/155755570.c;xref:0x1557555ed,0x1438b776e the TimeOfDaySystem the thunk bodies build into (same object as GameWeatherSystem+0x1d98)
    /* 0x304 */ int32_t tod_version;    // L1 decomp:re/decomp/14479c3f0.c compared with tod+0xb438; a change re-binds the curves (0x14479b950)
    /* 0x318 */ float rain_intensity;   // L1 decomp:re/decomp/14479c3f0.c current rain intensity, written every update from 0x14479ca80 (curve of dataset tod+0x26c4 at the session time); also read by ParameterStudioGameSystem as the wet fallback
};
