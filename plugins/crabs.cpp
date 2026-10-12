#include "plugin_abi.hpp"
#include "Logger.h"

#include <cstdio>

/* Port of CODE/caracAL/examples/crabs.js (commit before the JS src removal). */

static const CaracalApi* api = nullptr;
static double last_respawn_ms = 0;
static double last_loop_ms = 0;
static const char* mon_type = "bee";

static void on_load(const CaracalApi* a) {
  api = a;
  last_respawn_ms = 0;
  last_loop_ms = 0;
  EWE::Log::Normal("crabs plugin loaded\n");
}

static void on_unload(void) {
  EWE::Log::Normal("crabs plugin unloading\n");
  api = nullptr;
}

static void drink_potions(const CaracalEntity* me) {
  if (me->max_hp > 0 && api->can_use(api->ctx, "hp")) {
    if (me->hp / me->max_hp <= 0.8 || me->max_hp - me->hp >= 50) {
      EWE::Log::Debug("use_hp hp=%.0f/%.0f\n", me->hp, me->max_hp);
      api->use(api->ctx, "hp");
    }
  }
  if (me->max_mp > 0 && api->can_use(api->ctx, "mp")) {
    if (me->mp / me->max_mp <= 0.8 || me->max_mp - me->mp >= 40) {
      EWE::Log::Debug("use_mp mp=%.0f/%.0f\n", me->mp, me->max_mp);
      api->use(api->ctx, "mp");
    }
  }
}

static void on_tick(double now_ms) {
  if (!api || !api->self) return;
  const CaracalEntity* me = api->self(api->ctx);
  if (!me) return;

  if (now_ms - last_respawn_ms >= 2000) {
    last_respawn_ms = now_ms;
    if (me->rip) {
      EWE::Log::Debug("respawn at (%.1f,%.1f)\n", me->real_x, me->real_y);
      api->respawn(api->ctx);
    }
  }

  if (now_ms - last_loop_ms < 100) return;
  last_loop_ms = now_ms;
  if (me->rip) return;

  drink_potions(me);
  api->loot(api->ctx);

  const CaracalEntity* target = api->nearest_monster(api->ctx, mon_type);
  if (target) {
    char tid[64];
    std::snprintf(tid, sizeof(tid), "%s", target->id ? target->id : "");
    api->change_target(api->ctx, tid);
    double dist = api->simple_distance(api->ctx, target->real_x, target->real_y, me->real_x,
                                       me->real_y);
    double range = me->range > 1 ? me->range : 40;
    int in_atk = api->can_attack(api->ctx, tid);
    if (in_atk || dist <= range + 40) {
      //EWE::Log::Debug("attack %s d=%.1f range=%.1f can=%d\n", tid, dist, range, in_atk);
      api->attack(api->ctx, tid);
      if (dist <= range - 8) return;
    }
    if (dist <= range - 8) {
      //EWE::Log::Debug("hold range d=%.1f range=%.1f can_atk=%d\n", dist, range, in_atk);
      return;
    }
    if (api->is_moving(api->ctx)) {
      return;
    }
    int los = api->can_move_to(api->ctx, target->real_x, target->real_y);
    //EWE::Log::Debug("smart_move target %s los=%d d=%.1f me=(%.1f,%.1f) tgt=(%.1f,%.1f)\n", tid, los, dist, me->real_x, me->real_y, target->real_x, target->real_y);
    api->smart_move_xy(api->ctx, target->real_x, target->real_y);
  } 
  else if (!api->is_moving(api->ctx)) {
    //EWE::Log::Debug("smart_move named %s me=(%.1f,%.1f)\n", mon_type, me->real_x, me->real_y);
    api->smart_move_named(api->ctx, mon_type);
  }
}

static const CaracalPlugin plugin = {
    CARACAL_PLUGIN_ABI,
    "crabs",
    on_load,
    on_unload,
    on_tick,
};

extern "C" const CaracalPlugin* caracal_plugin(void) { return &plugin; }
