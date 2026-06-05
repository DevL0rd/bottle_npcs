// Bottle NPCs -- an example native Dusklight mod (detour-based).
//
// Bottle a nearby NPC or enemy and dump it back out later, in the spirit of
// catching bugs/fairies. Bottles store fixed content *types*, not arbitrary
// actors, so the mod keeps the captured actor itself and uses a Red Chu bottle
// purely as the "occupied" indicator:
//
//   * Capture: swing an empty bottle while Z-targeting an in-range NPC/enemy ->
//     it is stored and removed, and the bottle becomes a Red Chu bottle. Only
//     one actor is held at a time.
//   * Release: use the (Red Chu) bottle -> instead of drinking, the bottle is
//     dumped out: the held actor reappears in front of you and the bottle goes
//     back to empty.
//
// Hooks daAlink_c::procBottleSwingInit (catch) and procBottleDrinkInit (dump),
// and calls game code directly through the game's exported symbols.

#include <dlfcn.h>

#include <cmath>
#include <cstdio>

#include <funchook.h>

#include "dusk/mod_sdk.h"

#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "d/actor/d_a_alink.h"  // daAlink_c, attention, bottle + item helpers

namespace {

constexpr const char* kSymSwingInit = "_ZN9daAlink_c19procBottleSwingInitEP10fopAc_ac_ci";
constexpr const char* kSymDrinkInit = "_ZN9daAlink_c19procBottleDrinkInitEt";

using SwingInitFn = int (*)(void* self, fopAc_ac_c* catchActor, int arg);
using DrinkInitFn = int (*)(void* self, unsigned short itemNo);

const DuskSetting kSettings[] = {
    {"catch_range", "Catch Range", "Maximum distance to an NPC/enemy to bottle it.",
        DUSK_SETTING_FLOAT, 150, 50, 500, 10},
    {"release_distance", "Release Distance",
        "How far in front of you a bottled actor is dumped out.", DUSK_SETTING_FLOAT, 80, 0, 300,
        10},
};

const DuskHost* g_host = nullptr;
DuskMod* g_self = nullptr;
funchook_t* g_funchook = nullptr;
SwingInitFn g_orig_swing = nullptr;
DrinkInitFn g_orig_drink = nullptr;

// Single held actor.
bool g_holding = false;
s16 g_heldName = 0;
u32 g_heldParams = 0;
u8 g_heldSlot = 0;  // bottle slot the Red Chu marker was placed in

void log_msg(const char* msg) {
    if (g_host != nullptr) {
        g_host->log(msg);
    }
}

bool is_bottleable(fopAc_ac_c* actor) {
    // Any actor except the player itself -- NPCs, enemies, animals, generic
    // actors. Capture is already limited to what you're targeting/looking at.
    return actor != nullptr && fopAcM_GetGroup(actor) != fopAc_PLAYER_e;
}

f32 setting(const char* key) {
    return static_cast<f32>(g_host->setting_get(g_self, key));
}

bool in_range(fopAc_ac_c* a, fopAc_ac_c* b, f32 range) {
    const f32 dx = a->current.pos.x - b->current.pos.x;
    const f32 dy = a->current.pos.y - b->current.pos.y;
    const f32 dz = a->current.pos.z - b->current.pos.z;
    return (dx * dx + dy * dy + dz * dz) <= range * range;
}

void capture(daAlink_c* link, fopAc_ac_c* npc) {
    g_heldName = fopAcM_GetName(npc);
    g_heldParams = fopAcM_GetParam(npc);
    g_heldSlot = link->mSelectItemId;
    g_holding = true;

    // Clear the Z-lock before deleting the target: the attention lock is tracked
    // by process id, and the freed id could otherwise re-latch onto a later
    // actor and make Z-targeting flicker.
    if (link->mAttention != nullptr) {
        link->mAttention->mLockTargetID = fpcM_ERROR_PROCESS_ID_e;
        link->mAttention->mAttnStatus = static_cast<u8>(dAttention_c::EState_NONE);
    }
    fopAcM_delete(npc);

    // Make the bottle a Red Chu bottle to show it's occupied.
    dComIfGs_setEquipBottleItemIn(g_heldSlot, dItemNo_CHUCHU_RED_e);
    link->setBottleModel(dItemNo_CHUCHU_RED_e);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Bottle NPCs: bottled actor (profile %d)", g_heldName);
    log_msg(buf);
}

void dump_out(daAlink_c* link) {
    fopAc_ac_c* player = reinterpret_cast<fopAc_ac_c*>(link);
    const f32 dist = setting("release_distance");
    const f32 yaw = player->shape_angle.y * (3.14159265f / 32768.0f);
    cXyz pos = player->current.pos;
    pos.x += std::sin(yaw) * dist;
    pos.z += std::cos(yaw) * dist;
    csXyz angle{};
    angle.y = player->shape_angle.y;
    fopAcM_create(g_heldName, g_heldParams, &pos, fopAcM_GetRoomNo(player), &angle, nullptr, -1);

    // Empty the bottle again.
    dComIfGs_setEquipBottleItemEmpty(g_heldSlot);
    link->setBottleModel(dItemNo_EMPTY_BOTTLE_e);
    g_holding = false;

    char buf[96];
    std::snprintf(buf, sizeof(buf), "Bottle NPCs: dumped out actor (profile %d)", g_heldName);
    log_msg(buf);
}

// Catch: swing an empty bottle at an in-range NPC/enemy (only when not already
// holding one).
int detour_swing(void* self, fopAc_ac_c* catchActor, int arg) {
    daAlink_c* link = static_cast<daAlink_c*>(self);
    fopAc_ac_c* targeted = link->mTargetedActor;  // read before the original runs

    const int ret = g_orig_swing(self, catchActor, arg);
    if (g_holding) {
        return ret;  // hold only one at a time
    }

    fopAc_ac_c* candidates[] = {targeted, catchActor, dComIfGp_att_getLookTarget(),
        dComIfGp_att_getCatghTarget()};
    for (fopAc_ac_c* c : candidates) {
        if (c != nullptr && is_bottleable(c) &&
            in_range(reinterpret_cast<fopAc_ac_c*>(link), c, setting("catch_range"))) {
            capture(link, c);
            break;
        }
    }
    return ret;
}

// Dump: using the Red Chu bottle we set on capture releases the actor instead of
// drinking it.
int detour_drink(void* self, unsigned short itemNo) {
    daAlink_c* link = static_cast<daAlink_c*>(self);
    if (g_holding && link->mSelectItemId == g_heldSlot) {
        dump_out(link);
        return link->checkWaitAction();  // back to idle -- skip the drink entirely
    }
    return g_orig_drink(self, itemNo);
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

    g_orig_swing = reinterpret_cast<SwingInitFn>(resolve(kSymSwingInit));
    g_orig_drink = reinterpret_cast<DrinkInitFn>(resolve(kSymDrinkInit));
    if (g_orig_swing == nullptr || g_orig_drink == nullptr) {
        host->log("Bottle NPCs: could not resolve game symbols");
        return 1;
    }

    g_funchook = funchook_create();
    if (g_funchook == nullptr ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_swing),
            reinterpret_cast<void*>(detour_swing)) != 0 ||
        funchook_prepare(g_funchook, reinterpret_cast<void**>(&g_orig_drink),
            reinterpret_cast<void*>(detour_drink)) != 0 ||
        funchook_install(g_funchook, 0) != 0) {
        host->log("Bottle NPCs: failed to install hooks");
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
    g_holding = false;
    g_orig_swing = nullptr;
    g_orig_drink = nullptr;
    g_self = nullptr;
    g_host = nullptr;
}

}  // extern "C"
