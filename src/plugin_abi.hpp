#pragma once

/* C ABI for hot-reloadable character plugins (.so / .dll).
 * Plugins must not keep pointers into the host across on_tick returns.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define CARACAL_PLUGIN_ABI 1

typedef struct CaracalEntity {
  const char* id;
  const char* type;
  const char* name;
  int rip;
  double hp;
  double max_hp;
  double mp;
  double max_mp;
  double x;
  double y;
  double real_x;
  double real_y;
  double range;
  int moving;
  double going_x;
  double going_y;
} CaracalEntity;

typedef struct CaracalApi {
  int abi;
  void* ctx;
  const CaracalEntity* (*self)(void* ctx);
  const CaracalEntity* (*nearest_monster)(void* ctx, const char* type);
  int (*can_attack)(void* ctx, const char* id);
  int (*can_use)(void* ctx, const char* skill);
  int (*can_move_to)(void* ctx, double x, double y);
  double (*simple_distance)(void* ctx, double x1, double y1, double x2, double y2);
  int (*is_moving)(void* ctx);
  void (*respawn)(void* ctx);
  void (*use)(void* ctx, const char* what);
  void (*loot)(void* ctx);
  void (*change_target)(void* ctx, const char* id);
  void (*attack)(void* ctx, const char* id);
  void (*move)(void* ctx, double x, double y);
  void (*smart_move_xy)(void* ctx, double x, double y);
  void (*smart_move_named)(void* ctx, const char* dest);
  void (*log)(void* ctx, const char* msg);
} CaracalApi;

typedef struct CaracalPlugin {
  int abi;
  const char* name;
  void (*on_load)(const CaracalApi* api);
  void (*on_unload)(void);
  void (*on_tick)(double now_ms);
} CaracalPlugin;

/* Each plugin exports this symbol. */
const CaracalPlugin* caracal_plugin(void);

#ifdef __cplusplus
}
#endif
