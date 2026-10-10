#include "plugin_abi.hpp"

/* Port of CODE/caracAL/examples/crabs.js (commit before the JS src removal). */

static const CaracalApi* api = nullptr;
static double last_respawn_ms = 0;
static double last_loop_ms = 0;
static const char* mon_type = "crab";

static void on_load(const CaracalApi* a) {
  api = a;
  last_respawn_ms = 0;
  last_loop_ms = 0;
  if (api && api->log) api->log(api->ctx, "crabs plugin loaded");
}

static void on_unload(void) {
  if (api && api->log) api->log(api->ctx, "crabs plugin unloading");
  api = nullptr;
}

static void on_tick(double now_ms) {
  if (!api || !api->self) return;
  const CaracalEntity* me = api->self(api->ctx);
  if (!me) return;

  if (now_ms - last_respawn_ms >= 2000) {
    last_respawn_ms = now_ms;
    if (me->rip) api->respawn(api->ctx);
  }

  if (now_ms - last_loop_ms < 100) return;
  last_loop_ms = now_ms;
  if (me->rip) return;

  if (api->can_use(api->ctx, "hp")) {
    if (me->max_hp > 0 &&
        (me->hp / me->max_hp <= 0.5 || me->max_hp - me->hp >= 200)) {
      api->use(api->ctx, "hp");
    }
    if (me->max_mp > 0 &&
        (me->mp / me->max_mp <= 0.5 || me->max_mp - me->mp >= 300)) {
      api->use(api->ctx, "mp");
    }
  }
  api->loot(api->ctx);

  const CaracalEntity* target = api->nearest_monster(api->ctx, mon_type);
  if (target) {
    api->change_target(api->ctx, target->id);
    if (api->can_attack(api->ctx, target->id)) {
      api->attack(api->ctx, target->id);
    } else {
      double dist = api->simple_distance(api->ctx, target->real_x, target->real_y, me->real_x,
                                         me->real_y);
      if (!api->is_moving(api->ctx) && dist > me->range - 10) {
        if (api->can_move_to(api->ctx, target->real_x, target->real_y)) {
          api->move(api->ctx, (target->real_x + me->real_x) / 2,
                    (target->real_y + me->real_y) / 2);
        } else {
          api->smart_move_xy(api->ctx, target->real_x, target->real_y);
        }
      }
    }
  } else if (!api->is_moving(api->ctx)) {
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
