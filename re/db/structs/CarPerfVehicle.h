// struct: CarPerfVehicle
// subsystem: car
// size: 0x1b10 L2 decomp:re/decomp/143514670.c;kb:structs/Upgrades.h (same object as career Upgrades, alloc 0x1b10)
// notes: NOT a physics object. This is the per-team R&D model `Upgrades` (career lane, structs/Upgrades.h,
// one per team in UpgradesComponent+0x298..+0x2a0), seen from Car_Fit_UpgradesToRating's write-back. The
// old reading "attribute array at +0xd10 stride 0xc40" was the UpgradeTree array: trees at +0xe0 (stride
// 0xc40, Upgrades.trees[2]), each tree's fitted-instance pointer list at tree+0x30 (= +0x110 for tree 0) with
// its count at tree+0xc30 (= +0xd10), tree count at +0x1960. fitUpgradesUpToRating std::find's the current
// effect-def in every tree's list to find which team car carries it. The physics effect of an R&D attribute
// never goes through this object: it is the radUpgrade<Type> VTF block replayed onto the live car
// (subsystems/car.md#perf). Mass is the one value computed here (Car_Upgrades_CalcNonLinearMass).
struct CarPerfVehicle {
    /* 0x0008 */ uint8_t mode;                   // L2 decomp:re/decomp/1435352a0.c;kb:structs/Upgrades.h 2 = per-driver trees (the per-type mass helper resolves the driver's tree through the robin_hood map at +0x1a60..+0x1a94 only in mode 2)
    /* 0x0050 */ float target_mass_at_max;       // L2 decomp:re/decomp/143535480.c;kb:subsystems/car.md#mass "Target mass at max" of Car_Upgrades_CalcNonLinearMass: nonlinear = linear + f*(f*target - linear), f = linear / sum(type mass at full)
    /* 0x0110 */ void *tree0_instances[0x180];   // L2 decomp:re/decomp/143514670.c;decomp:re/decomp/1435352a0.c;xref:0x143514c70 tree 0 fitted upgrade-instance pointers (UpgradeTree at +0xe0, list at tree+0x30)
    /* 0x0d10 */ uint64_t tree0_instance_count;  // L2 decomp:re/decomp/143514670.c;xref:0x143514c64,0x143514c77 count of tree0_instances (tree+0xc30); the loop base rbx = +0xd10 is this count, the list is rbx-0xc00
    /* 0x1960 */ uint64_t tree_count;            // L2 decomp:re/decomp/143514670.c;xref:0x143514c50 trees in use (1, or 2 in per-driver mode); loop stride 0xc40
    /* 0x1a38 */ void *type_meta_begin;          // L2 decomp:re/decomp/143535480.c;decomp:re/decomp/1435352a0.c;kb:subsystems/car.md#mass upgrade_type_metadata records, stride 0x20: +0 u8 upgrade type, +4 float mass adjustment at full upgrade
    /* 0x1a40 */ void *type_meta_end;            // L2 decomp:re/decomp/143535480.c;kb:subsystems/car.md#mass end of the type metadata records
};
