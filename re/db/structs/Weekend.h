// struct: Weekend
// subsystem: career
// size: 0x4f0 L2 decomp:re/decomp/140ae6050.c;xref:0x140ae60f6 (Career_Create_SeasonWeekend allocates 0x4f0 and runs Career_Build_WeekendFromName)
// notes: game::championship::Weekend, one round of a Season. Raised by ce-season with the lead's authorisation.
struct Weekend {
    /* 0x000 */ void **vtable;                       // L2 decomp:re/decomp/140aee2b0.c;xref:0x140aee4e0 vt_Career_Weekend
    /* 0x028 */ char *name;                          // L2 decomp:re/decomp/140af1230.c;xref:0x140af14f7 the weekend name Season serialise writes into weekend_names[]
    /* 0x220 */ Session *sessions_begin;             // L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140aece00.c;xref:0x140ae88db sessions inline, 0xa0 each
    /* 0x228 */ Session *sessions_end;               // L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140aece00.c;xref:0x140ae88e2 end of the sessions
    /* 0x238 */ uint32_t current_session;            // L2 decomp:re/decomp/140af0460.c;decomp:re/decomp/140af7680.c;xref:0x140af052e,0x140af7a35,0x140af781f,0x140af7b3f index of the session Weekend::start fills: 0x140af0460 reads it (0x140af052e) and fills sessions[index] (a later session from sessions[index-1]'s results); the reset in 0x140af7680 zeroes it before calling Weekend::start (qword with +0x23c at 0x140af781f, dword at 0x140af7b3f)
    /* 0x23c */ uint32_t progress_state;             // L2 decomp:re/decomp/143099650.c;xref:0x1430997bc,0x143099901 0 = not started: RebuildNotStartedWeekends removes trailing weekends while it is 0
    /* 0x278 */ uint8_t track_stat_id;               // L2 decomp:re/decomp/140aee2b0.c;decomp:re/decomp/140ae6050.c;xref:0x140aee534,0x14309e33b the weekend's track; 0xff = none (placeholder, indexed unchecked by 0x14309e220)
};
