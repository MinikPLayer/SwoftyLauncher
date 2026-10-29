# fe_flow: front-end flow / screen stack / transitions

Owner: see `subsystems.tsv`. Rows: `kb.py find '\tfe_flow\t'`. Related: `vm` (routes, screen loader), `fe_home`/`fe_tiles` (tile menus).

## Map
The front end is a `neon` flow state machine. `Vm_Update_FlowStateMachine` (vm, 0x144423ce0) ticks it each frame and acts on the `LinkRoute` a state's `onStateUpdate` returns. The frontend screens and their navigation sit on top of the VM registry (`view_model_data.*`, `ui_menu_data.*`).

### StateFrontend (the front-end driver state)
- `FeFlow_StateFrontend_OnStateEnter` (0x1436cdac0, L2, sig `fe_flow.state_frontend_enter`, vtable slot 0x14585adf0): entry point; by sub-state it fires the matching enter route — `enter_frontend_transition`, `enter_playercard`, `enter_challenge_career_episode`, `enter_challenge_career_season_complete`, `enter_f1_driver_career_setup_after_f2`, `enter_customisation`. Builds route names with `Ne_Make_StrFromCStr`. `FLOW/V` log channel.
- `FeFlow_Recalculate_TransitionState` (0x1436d1500, L2, sig `fe_flow.recalc_transition_state`): computes the **transition contract** — which context the front end is in — and writes `#ui_menu_data.frontend_transition_state` in `g_Vm_Registry` (via `Vm_Resolve_RegistryNode`). Contract values seen: `main_menu`, `hopper_lobby`, `custom_lobby`, `event_mode`, `pro_leagues`, `hopper_select`; keyed off `network.current_session.session_type`. Logs `TransitionStateContract changed from('%s') to('%s')`. Also at 0x1436d1430 (`recalculateFrontEndTransitionNotification`).
- `FeFlow_GoTo_Frontend` (0x1436d5320, L2): calls Recalculate, then fires the scene route `enter_frontend_transition` (scene route-fire helper at 0x1438574e0, arg form `(scene, 3, &name, &empty, &rc)`), consuming the message when `rc ∈ {0, 0x25}`.
- `FeFlow_TransitionVM_OnCommand` (0x141bb8a20, L2, vtable slot 0x1457391f0 of the transition VM vtable 0x1457391a0): `FrontendTransitionViewModel::onCommandRaised` (slot 10); resolves `#ui_menu_data.frontend_transition_state`. Core copies this VM's vtable as the base for custom-screen VMs (ui-native.md §1).

### Screen/tab builders (containers beyond the home tile menu)
Container VMs build their tab sub-screens by allocating a per-tab VM, running its ctor (`FUN_1414bdac0`) with template strings, then pushing it into a tab-VM vector and a parallel type/pill byte-array.
- `FeFlow_Add_DriversTab` (0x1422a0660, L2): `SessionDirectorContainerViewModel::addTabDriversScreen`; 0x8f0 tab VM; vector at container+0x288/+0x290/+0x298, kind array at +0x868. Siblings (same shape, L1 in `game` lane): `addTabIncidentsScreen` 0x1422a0990, `addTabPitStopsScreen` 0x1422a0cb0, `addTabLapTimesScreen` 0x1422a41d0.
- `FeFlow_Add_StandardTab` (0x141d93420, L2): `SessionResultsContainerViewModel::addTabStandardScreen`; 0xd20 tab VM; vector at +0x288, pill array at +0xa18 (5-byte stride).

## Hooking
- Change where the front end goes: hook `FeFlow_StateFrontend_OnStateEnter` or wrap the route fired.
- Add/reorder a results/director tab: wrap `FeFlow_Add_*Tab` (same pattern as the home tile injection: build the tab VM, push into the +0x288 vector and the type array).
- The clean route-safety hook is `Vm_Run_Route` (vm lane), not these states.

## Open
- Flow-state base class (`neon::NeFlowStateBase`, `onStateUpdate` → `LinkRoute`) object layout not mapped; the session-side states (`SessionState*`) live in `session`/`game`.
- Scene screen-stack object (`scene+0x130/+0x138` open-screen list, ui-native.md §10) not yet a struct.
- The addTab builders are L2 for Drivers/Standard only; the other tab kinds are L1 clones.

## Inbox
- 2026-10-28 17:12 ts3b: 2026-10-28 ts3b: title screen = StateIntro (flow djb2 0xa4d3a4b2, vt 0x14585bff0). FeFlow_StateIntro_OnUpdate (0x1436da4d0) leaves only after the title sync point (state+0xcd0; 0x1442626c0 = both VM bool nodes +0x80 and +0xa0 true, written by the title UI on a press), then a one-shot request (0x14388bbe0, kind 0x2e) whose callback FeFlow_StateIntro_OnEngaged (0x1436daa90) sets +0xef4; the next update returns g_FeFlow_IntroLinkFrontend (0x146cb2288). Cheapest agent continue: the virtual pad's A press. No-pad option: set both sync-point nodes true with Vm_NodeSet_Bool on the flow thread (node handles at state+0xd50 and +0xd70; names obfuscated, L1), which keeps the game's own request. Returning the link directly skips the request (L0 risk).
- 2026-10-28 22:42 ts-v1: ts-v1: FeFlow_StateIntro_OnUpdate 0x1436da4d0 independently re-read (V1486 agree) and signed (fe_flow.state_intro_on_update); left at L1 for you to raise to L2. The test agent hooks it (inline) and sets the sync point's two bool nodes.
