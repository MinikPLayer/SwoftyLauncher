#pragma once
#include <swofty/game/base.hpp>

namespace swofty::game {

struct VmPropHolder {
    uint8_t _pad_0x0[0x8];
    void *node; // L3 stable
};
static_assert(offsetof(VmPropHolder, node) == 0x8);

using Vm_GetOrCreate_ListChild_t = void **(void *registry, void *parent, const uint32_t *hash);
inline constexpr fn<"Vm_GetOrCreate_ListChild", Vm_GetOrCreate_ListChild_t> Vm_GetOrCreate_ListChild{}; // L2 experimental
using Vm_List_AppendChild_t = void **(void *list);
inline constexpr fn<"Vm_List_AppendChild", Vm_List_AppendChild_t> Vm_List_AppendChild{}; // L2 experimental
using Vm_Release_Node_t = void(void *registry, void *node);
inline constexpr fn<"Vm_Release_Node", Vm_Release_Node_t> Vm_Release_Node{}; // L2 experimental
using Vm_Resolve_RegistryNode_t = void *(void *registry, const char *path, const void *base, void *create);
inline constexpr fn<"Vm_Resolve_RegistryNode", Vm_Resolve_RegistryNode_t> Vm_Resolve_RegistryNode{}; // L2 experimental
using Vm_Set_Bool_t = void *(void *registry, void *parent, const char *name, uint8_t *value, int overwrite);
inline constexpr fn<"Vm_Set_Bool", Vm_Set_Bool_t> Vm_Set_Bool{}; // L2 experimental
using Vm_Set_NodeRef_t = void **(void *registry, void *parent, const char *name, void *value, int overwrite);
inline constexpr fn<"Vm_Set_NodeRef", Vm_Set_NodeRef_t> Vm_Set_NodeRef{}; // L2 experimental
using Vm_Set_String_t = void *(void *registry, void *parent, const char *name, NeStr **value, int overwrite);
inline constexpr fn<"Vm_Set_String", Vm_Set_String_t> Vm_Set_String{}; // L2 experimental
using Vm_Set_TabEnabled_t = void(uintptr_t tabs, uintptr_t node, uint32_t enabled);
inline constexpr fn<"Vm_Set_TabEnabled", Vm_Set_TabEnabled_t> Vm_Set_TabEnabled{}; // L2 experimental
inline constexpr data<"g_Vm_Registry", void *> g_Vm_Registry{}; // L3 stable

}
