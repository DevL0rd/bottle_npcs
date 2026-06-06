// Bottle NPCs -- a Dusklight mod.
//
// Bottle a nearby actor and dump it back out later, in the spirit of catching
// bugs/fairies. Bottles store fixed content *types*, not arbitrary actors, so the
// mod keeps the captured actor itself and uses a Red Chu bottle purely as the
// "occupied" indicator:
//
//   * Capture: swing an empty bottle while Z-targeting an in-range actor -> it is
//     stored and removed, and the bottle becomes a Red Chu bottle. Only one actor
//     is held at a time.
//   * Release: use the (Red Chu) bottle -> instead of drinking, the bottle is
//     dumped out: the held actor reappears in front of you and the bottle empties.
//
// Hooks daAlink_c::procBottleSwingInit (catch) and procBottleDrinkInit (dump).

#include <cmath>

#include "dusk/hook.hpp"
#include "dusk/mod_api.h"

#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "d/actor/d_a_alink.h"  // daAlink_c, attention, bottle + item helpers

DUSK_REQUIRE_API_VERSION

namespace {

using DrinkEntry = dusk::HookEntry<&daAlink_c::procBottleDrinkInit>;

const DuskSetting kSettings[] = {
    {"catch_range", "Catch Range", "Maximum distance to an actor to bottle it.",
        DUSK_SETTING_FLOAT, 150, 50, 500, 10},
    {"release_distance", "Release Distance",
        "How far in front of you a bottled actor is dumped out.", DUSK_SETTING_FLOAT, 80, 0, 300, 10},
};

// Single held actor.
bool g_holding = false;
s16 g_heldName = 0;
u32 g_heldParams = 0;
u8 g_heldSlot = 0;  // bottle slot the Red Chu marker was placed in

// Z-target read before the swing original runs (it may clear the lock).
fopAc_ac_c* g_swingTargeted = nullptr;

// Settings cached each tick (hooks run outside the mod context).
f32 g_catchRange = 150.0f;
f32 g_releaseDist = 80.0f;

bool is_bottleable(fopAc_ac_c* actor) {
    // Any actor except the player itself. Capture is already limited to what
    // you're targeting/looking at.
    return actor != nullptr && fopAcM_GetGroup(actor) != fopAc_PLAYER_e;
}

bool in_range(fopAc_ac_c* a, fopAc_ac_c* b, f32 range) {
    const f32 dx = a->current.pos.x - b->current.pos.x;
    const f32 dy = a->current.pos.y - b->current.pos.y;
    const f32 dz = a->current.pos.z - b->current.pos.z;
    return (dx * dx + dy * dy + dz * dz) <= range * range;
}

void capture(daAlink_c* link, fopAc_ac_c* actor) {
    g_heldName = fopAcM_GetName(actor);
    g_heldParams = fopAcM_GetParam(actor);
    g_heldSlot = link->mSelectItemId;
    g_holding = true;

    // Clear the Z-lock before deleting the target: the attention lock is tracked
    // by process id, and the freed id could otherwise re-latch onto a later actor
    // and make Z-targeting flicker.
    if (link->mAttention != nullptr) {
        link->mAttention->mLockTargetID = fpcM_ERROR_PROCESS_ID_e;
        link->mAttention->mAttnStatus = static_cast<u8>(dAttention_c::EState_NONE);
    }
    fopAcM_delete(actor);

    // Make the bottle a Red Chu bottle to show it's occupied.
    dComIfGs_setEquipBottleItemIn(g_heldSlot, dItemNo_CHUCHU_RED_e);
    link->setBottleModel(dItemNo_CHUCHU_RED_e);
}

void dump_out(daAlink_c* link) {
    fopAc_ac_c* player = reinterpret_cast<fopAc_ac_c*>(link);
    const f32 yaw = player->shape_angle.y * (3.14159265f / 32768.0f);
    cXyz pos = player->current.pos;
    pos.x += std::sin(yaw) * g_releaseDist;
    pos.z += std::cos(yaw) * g_releaseDist;
    csXyz angle{};
    angle.y = player->shape_angle.y;
    fopAcM_create(g_heldName, g_heldParams, &pos, fopAcM_GetRoomNo(player), &angle, nullptr, -1);

    // Empty the bottle again.
    dComIfGs_setEquipBottleItemEmpty(g_heldSlot);
    link->setBottleModel(dItemNo_EMPTY_BOTTLE_e);
    g_holding = false;
}

// Read the Z-target before the swing original runs.
int32_t swing_pre(void* args) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    g_swingTargeted = link->mTargetedActor;
    return 0;  // don't cancel
}

// Catch: swing an empty bottle at an in-range actor (only when not already
// holding one).
void swing_post(void* args, void* /*retval*/) {
    if (g_holding) {
        return;  // hold only one at a time
    }
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    fopAc_ac_c* catchActor = dusk::arg<fopAc_ac_c*>(args, 1);

    fopAc_ac_c* candidates[] = {g_swingTargeted, catchActor, dComIfGp_att_getLookTarget(),
        dComIfGp_att_getCatghTarget()};
    for (fopAc_ac_c* c : candidates) {
        if (c != nullptr && is_bottleable(c) &&
            in_range(reinterpret_cast<fopAc_ac_c*>(link), c, g_catchRange)) {
            capture(link, c);
            break;
        }
    }
}

// Dump: using the Red Chu bottle we set on capture releases the actor instead of
// drinking it.
void drink_replace(void* args, void* retval) {
    daAlink_c* link = dusk::arg<daAlink_c*>(args, 0);
    const u16 itemNo = dusk::arg<u16>(args, 1);
    int* ret = static_cast<int*>(retval);
    if (g_holding && link->mSelectItemId == g_heldSlot) {
        dump_out(link);
        *ret = link->checkWaitAction();  // back to idle -- skip the drink entirely
    } else {
        *ret = DrinkEntry::g_orig(link, itemNo);
    }
}

}  // namespace

extern "C" {

void mod_init(DuskModAPI* api) {
    dusk::init(api);
    api->define_settings(kSettings, sizeof(kSettings) / sizeof(kSettings[0]));
    dusk::hookAddPre<&daAlink_c::procBottleSwingInit>(swing_pre);
    dusk::hookAddPost<&daAlink_c::procBottleSwingInit>(swing_post);
    dusk::hookSetReplace<&daAlink_c::procBottleDrinkInit>(drink_replace);
    api->log_info("Bottle NPCs enabled");
}

void mod_tick(DuskModAPI* api) {
    g_catchRange = static_cast<f32>(api->setting_get("catch_range"));
    g_releaseDist = static_cast<f32>(api->setting_get("release_distance"));
}

void mod_cleanup(DuskModAPI* api) {
    (void)api;
    g_holding = false;
    g_swingTargeted = nullptr;
}

}  // extern "C"
