// struct: SaveDataContainer
// subsystem: core
// size: ?
// notes: neon save-data node (base ctor Ne_Construct_SaveDataContainer 0x14364d570). Obfuscated bools are {byte, key} pairs, true when byte != (key & 1). The save root save_game sits at SaveDataSystem+0x20 (vt_Game_SaveDataRootContainer).
struct SaveDataContainer {
    /* 0x000 */ void **vtable;                      // L3 decomp:re/decomp/143654650.c;xref:0x1436548fe;trace:T010/run2:86315;trace:T010/run3 the root stores vt_Game_SaveDataRootContainer here; slot +0x10 serialise, +0x18 deserialise, +0x20 reset
    /* 0x080 */ const char *name;                  // L3 decomp:re/decomp/14364d570.c;decomp:re/decomp/14364f3a0.c;xref:0x14364d61c,0x14364f41f,0x14364f83d,0x143651bff;trace:T010/run2:86526;trace:T010/run3 the container name (chars of the name string at +0x70) that the logs print and that Game_SaveData_SerialiseContainer_2 writes as the child's key (0x143651bff)
    /* 0x100 */ uint8_t retain_unknown;             // L2 decomp:re/decomp/14364f3a0.c;decomp:re/decomp/14364e000.c;xref:0x14364d72f,0x14364fef0,0x14364e22f,0x14364e8e9 keep child bags no registered child claims (load); only the bag-path serialiser writes them back, the inline profile save ignores them; value = (byte ^ retain_unknown_key) & 1; false on the root
    /* 0x108 */ uint64_t retain_unknown_key;        // L2 decomp:re/decomp/14364f3a0.c;decomp:re/decomp/14364e000.c;xref:0x14364d72f,0x14364fef7,0x14364e237,0x14364e8f1 random key of the retain_unknown pair, written with it by the base ctor
    /* 0x1c8 */ uint64_t retained_count;            // L2 decomp:re/decomp/14364f3a0.c;decomp:re/decomp/14364e000.c;xref:0x14364d82f,0x14365089b,0x14364e24a,0x143650285 number of retained unknown child bags (map at +0x190); zeroed by the ctor and by Game_SaveData_ClearRetained on each load
};
