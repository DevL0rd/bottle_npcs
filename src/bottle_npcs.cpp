// Bottle NPCs -- an example native Dusklight mod (detour-based).
//
// Lets you "bottle" an NPC and release it later, in the spirit of catching bugs
// or fairies. Bottles store fixed content *types*, not arbitrary actors, so this
// mod keeps its own stack of captured NPCs instead of touching the bottle
// inventory:
//
//   * Capture: swing a bottle while Z-targeting an NPC -> the NPC is recorded
//     (profile, params, position, angle, room), removed from the world, and
//     pushed onto the stack.
//   * Release: swing a bottle with nothing targeted -> the most recently bottled
//     NPC is recreated in front of you.
//
// Both are driven by a hook on daAlink_c::procBottleSwingInit (the bottle swing).
// The mod calls game functions directly through the game's exported symbols.

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include <funchook.h>

#include "dusk/mod_sdk.h"

#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"

// Game functions we call. Declared here (not via game headers) so the linker
// leaves them undefined and the loader resolves them against the executable's
// exported symbols. Mangling ignores return type, so fopAc_ac_c* is fine even
// where the real return type is a subclass.
fopAc_ac_c* daPy_getPlayerActorClass();
fopAc_ac_c* dComIfGp_att_getLookTarget();
fopAc_ac_c* dComIfGp_att_getCatghTarget();

namespace {

// Hook target: daAlink_c::procBottleSwingInit(fopAc_ac_c*, int).
constexpr const char* kSymBottleSwingInit = "_ZN9daAlink_c19procBottleSwingInitEP10fopAc_ac_ci";

using SwingInitFn = int (*)(void* self, fopAc_ac_c* catchActor, int arg);

const DuskSetting kSettings[] = {
    {"release_distance", "Release Distance",
        "How far in front of you a bottled NPC is released.", DUSK_SETTING_FLOAT, 80, 0, 300, 10},
};

const DuskHost* g_host = nullptr;
DuskMod* g_self = nullptr;
funchook_t* g_funchook = nullptr;
SwingInitFn g_orig_swing = nullptr;

struct CapturedNpc {
    s16 name;     // fpcNm_* profile, from fopAcM_GetName
    u32 params;   // fopAcM_GetParam
    cXyz pos;     // capture position
    csXyz angle;  // capture facing
    s8 room;
};
std::vector<CapturedNpc> g_stack;

void log_msg(const char* msg) {
    if (g_host != nullptr) {
        g_host->log(msg);
    }
}

void capture(fopAc_ac_c* npc) {
    CapturedNpc c{};
    c.name = fopAcM_GetName(npc);
    c.params = fopAcM_GetParam(npc);
    c.pos = npc->current.pos;
    c.angle = npc->shape_angle;
    c.room = static_cast<s8>(fopAcM_GetRoomNo(npc));
    g_stack.push_back(c);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Bottle NPCs: bottled NPC (profile %d), %zu held", c.name,
        g_stack.size());
    log_msg(buf);

    fopAcM_delete(npc);
}

void release() {
    if (g_stack.empty()) {
        return;
    }
    fopAc_ac_c* player = daPy_getPlayerActorClass();
    if (player == nullptr) {
        return;
    }

    const CapturedNpc c = g_stack.back();
    g_stack.pop_back();

    const f32 dist = static_cast<f32>(g_host->setting_get(g_self, "release_distance"));
    const f32 yaw = player->shape_angle.y * (3.14159265f / 32768.0f);
    cXyz pos = player->current.pos;
    pos.x += std::sin(yaw) * dist;
    pos.z += std::cos(yaw) * dist;

    csXyz angle{};
    angle.y = player->shape_angle.y;
    fopAcM_create(c.name, c.params, &pos, fopAcM_GetRoomNo(player), &angle, nullptr, -1);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Bottle NPCs: released NPC (profile %d), %zu held", c.name,
        g_stack.size());
    log_msg(buf);
}

// Which actors can be bottled: NPCs and enemies (not the player, world, etc.).
bool is_bottleable(fopAc_ac_c* actor) {
    const u8 group = fopAcM_GetGroup(actor);
    return group == fopAc_NPC_e || group == fopAc_ENEMY_e;
}

// Hook: the bottle swing. Capture a Z-targeted NPC or enemy, or release when
// swinging at nothing.
int detour_swing(void* self, fopAc_ac_c* catchActor, int arg) {
    const int ret = g_orig_swing(self, catchActor, arg);

    fopAc_ac_c* look = dComIfGp_att_getLookTarget();
    if (look != nullptr && is_bottleable(look)) {
        capture(look);
    } else if (look == nullptr && dComIfGp_att_getCatghTarget() == nullptr) {
        release();
    }
    return ret;
}

void* resolve(const char* symbol) {
    void* self = dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL);
    return self ? dlsym(self, symbol) : nullptr;
}

}  // namespace

extern "C" {

int dusk_mod_init(const DuskHost* host, DuskMod* self) {
    if (host == nullptr || host->abi_version != DUSK_MOD_ABI_VERSION) {
        return 1;
    }
    g_host = host;
    g_self = self;
    host->define_settings(self, kSettings, sizeof(kSettings) / sizeof(kSettings[0]));

    g_orig_swing = reinterpret_cast<SwingInitFn>(resolve(kSymBottleSwingInit));
    if (g_orig_swing == nullptr) {
        host->log("Bottle NPCs: could not resolve procBottleSwingInit");
        return 1;
    }

    g_funchook = funchook_create();
    if (g_funchook == nullptr ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_swing),
            reinterpret_cast<void*>(detour_swing)) != 0 ||
        funchook_install(g_funchook, 0) != 0) {
        host->log("Bottle NPCs: failed to install hook");
        if (g_funchook != nullptr) {
            funchook_destroy(g_funchook);
            g_funchook = nullptr;
        }
        return 1;
    }

    host->log("Bottle NPCs enabled");
    return 0;
}

void dusk_mod_dispose(void) {
    if (g_funchook != nullptr) {
        funchook_uninstall(g_funchook, 0);
        funchook_destroy(g_funchook);
        g_funchook = nullptr;
    }
    g_stack.clear();
    g_orig_swing = nullptr;
    g_self = nullptr;
    g_host = nullptr;
}

}  // extern "C"
