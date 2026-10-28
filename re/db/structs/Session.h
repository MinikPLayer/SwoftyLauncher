// struct: Session
// subsystem: career
// size: 0xa0 L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140aece00.c (Weekend.sessions are indexed and destroyed in steps of 0xa0)
// notes: game::championship::Session, one session of a Weekend (serialised by 0x140af3750: completion, ruleset, id, results_list, penalties). Raised by ce-season with the lead's authorisation.
struct Session {
    /* 0x008 */ uint32_t session_id;                 // L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140af3750.c;xref:0x140ae8913 the session id the standings key their sessions by
    /* 0x010 */ uint32_t completion;                 // L2 decomp:re/decomp/140aecf80.c;decomp:re/decomp/140af3750.c;xref:0x140aed2ef,0x140af8a43 2 = complete (cmp dword [x+0x10],2)
    /* 0x018 */ NeAllocator *participants_allocator; // L2 decomp:re/decomp/140af0460.c;decomp:re/decomp/140af2ab0.c;xref:0x140af09b2,0x140af09f5,0x140af0ad9 the participants vector object {allocator, begin, end, capacity}: Weekend::start loads it from session+0x18 and grows the buffer through its vtable +0x18 (alloc n*0x88, align 8, call at 0x140af0a01), frees the old buffer through +0x38 (0x140af0ae4); Sess_ParticipantVec_PushBack does the same with vec[0]
    /* 0x020 */ EntrantRecord *participants_begin;   // L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140af4660.c;xref:0x140ae8ec4,0x140af4701 the session's entrants (results_list order)
    /* 0x028 */ EntrantRecord *participants_end;     // L2 decomp:re/decomp/140ae8880.c;decomp:re/decomp/140af4660.c;xref:0x140ae8ec8,0x140af46fd end of the entrants
    /* 0x030 */ uint64_t participants_capacity;      // L2 decomp:re/decomp/140af0460.c;decomp:re/decomp/140af2ab0.c;xref:0x140af09c2,0x140af09d6,0x140af0af5 element count the participants buffer holds: Weekend::start compares it with the entrant count (cmp at 0x140af09d6, grows when smaller) and stores the new count after a grow (0x140af0af5); Sess_ParticipantVec_PushBack grows when count == vec[3]
};
