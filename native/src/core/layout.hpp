#pragma once
#include <cstddef>
#include <cstdint>

#include <swofty/game.hpp>

// The KB structs the core reads. bindgen exports a struct's fields only once they pass the export
// gate, so a core built against headers without them still compiles: the concepts below are false
// for an undeclared or incomplete struct, and the features that need it report unavailable.

namespace swofty::game {
struct CareerEntity;
struct ChampionshipHolder;
struct EntrantRecord;
struct FlowState;
struct GameManager;
struct GameMessageBusHolder;
struct GameModeContext;
struct GdbStringRecord;
struct NeMessageBus;
struct NeBagCodec;
struct NeCountingStream;
struct NeMemWriteStream;
struct NePropertyVtbl;
struct NePropertyTypeVtbl;
struct NeStreamVtbl;
struct SaveDataContainer;
}

namespace runtime::layout {

namespace g = swofty::game;

template <class T>
concept flow_state = requires(T *s) {
    s->name_size;
    s->name;
    s->name_hash;
    s->status;
};

template <class T>
concept gdb_string = requires(T *r) {
    r->enc_size;
    r->enc;
};

template <class M, class C, class H, class E>
concept career_chain = requires(M *m, C *c, H *h, E *e) {
    m->mode_context;
    c->championship_holder;
    h->entity;
    e->component_registry;
    e->entrants_begin;
    e->entrants_end;
};

template <class R>
concept entrant = requires(R *r) {
    r->ordinal_flags;
    r->id_lo;
    r->team_stat_id;
};

template <class H, class B>
concept message_bus = requires(H *h, B *b) {
    h->bus;
    b->active;
};

template <class C, class CS, class MS, class SV>
concept save_streams = requires(C *c, CS *cs, MS *ms, SV *sv) {
    c->vtable;
    c->stream;
    cs->vtable;
    cs->count;
    ms->vtable;
    ms->data;
    ms->capacity;
    ms->position;
    sv->write;
    sv->write_length;
};

template <class S>
concept named_container = requires(S *s) {
    s->vtable;
    s->name;
};

template <class PV, class TV>
concept property = requires(PV *pv, TV *tv) {
    pv->type;
    pv->value;
    tv->id;
    tv->key_offset;
};

inline constexpr bool FLOW = flow_state<g::FlowState>;
inline constexpr bool GDB_STRING = gdb_string<g::GdbStringRecord>;
inline constexpr bool CAREER = career_chain<g::GameManager, g::GameModeContext, g::ChampionshipHolder, g::CareerEntity>;
inline constexpr bool ENTRANT = entrant<g::EntrantRecord>;
inline constexpr bool BUS = message_bus<g::GameMessageBusHolder, g::NeMessageBus>;
inline constexpr bool SAVE_SPLICE = save_streams<g::NeBagCodec, g::NeCountingStream, g::NeMemWriteStream, g::NeStreamVtbl> &&
                                    property<g::NePropertyVtbl, g::NePropertyTypeVtbl> &&
                                    named_container<g::SaveDataContainer>;

}
