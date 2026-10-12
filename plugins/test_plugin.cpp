#include "plugin_abi.hpp"

static int ticks = 0;

static void on_load(const CaracalApi*) {
    ticks = 0;
}
static void on_unload(void) {}
static void on_tick(double) {
    ++ticks;
}

static const CaracalPlugin plugin = {CARACAL_PLUGIN_ABI, "test", on_load, on_unload, on_tick};

extern "C" const CaracalPlugin* caracal_plugin(void) {
    return &plugin;
}
