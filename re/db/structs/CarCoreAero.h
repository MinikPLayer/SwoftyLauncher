// struct: CarCoreAero
// subsystem: car
// size: ?  L1 decomp:re/decomp/1555dac10.c
// notes: live CoreAero system at CarLive+0x568 (ctor .data1 0x1555dac10). Slipstream switches and the
// per-car wake density ratio that F1Aero multiplies into lift/drag.
struct CarCoreAero {
    /* 0x0000 */ void *vtable;                                // L2 decomp:re/decomp/1555dac10.c;kb:subsystems/car.md#layout system vtable 0x145bf9f20
    /* 0x0008 */ CarLive *car;                                // L2 decomp:re/decomp/1555dac10.c;kb:subsystems/car.md#layout owning car (factory argument); StepVehicle reads [sys+8]+0x568/+0x580
    /* 0x0010 */ void *allocator;                             // L2 decomp:re/decomp/1555dac10.c;kb:subsystems/car.md#layout car allocator [car+0xd0]
    /* 0x0018 */ void *data_vtable;                           // L2 decomp:re/decomp/1555dac10.c;kb:subsystems/car.md#layout embedded data object vtable 0x145bf9eb0 {dtor, GetName, Car_Read_<X>, Car_Write_<X>}
    /* 0x0028 */ uint8_t slip_streaming;                      // L2 decomp:re/decomp/14467d080.c;kb:subsystems/car.md#coreaero VTF CoreAero/SlipStreaming bool (ctor 1)
    /* 0x0029 */ uint8_t slip_stream_while_braking;           // L2 decomp:re/decomp/14467cfd0.c;kb:subsystems/car.md#coreaero VTF CoreAero/SlipStreamWhileBraking bool (ctor 1; writer reads data+0x11)
    /* 0x0030 */ float slipstream_density;                    // L2 decomp:re/decomp/1446dceb0.c;xref:0x1446dcf3c ctor 1.0; air density ratio in the wake; F1Aero lift x (k + (1-k)*this), k = m_aeroSlipStreamAirDensityScaleLift
    /* 0x0040 */ uint8_t unk_0x40;                            // L1 decomp:re/decomp/1555dac10.c ctor 0xff (followed-car index?)
};
