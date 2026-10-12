#include "character_thread.hpp"
#include "client_scripts.hpp"
#include "config_util.hpp"
#include "constants.hpp"
#include "entity.hpp"
#include "file_stored_key_values.hpp"
#include "g_data.hpp"
#include "game_files.hpp"
#include "html_globals.hpp"
#include "html_prelude.hpp"
#include "ipc_storage.hpp"
#include "json.hpp"
#include "monitoring_util.hpp"
#include "pathing.hpp"
#include "plugin_host.hpp"
#include "realm_rtt.hpp"
#include "realm_safe_clone.hpp"
#include "socket_io.hpp"
#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unistd.h>

using caracal::Json;
namespace fs = std::filesystem;

TEST(json_roundtrip) {
    auto j = Json::parse(R"({"a":1,"b":[true,null,"x"],"c":{"d":1.5}})");
    CHECK(j["a"].as_number() == 1);
    CHECK(j["b"].as_array().size() == 3);
    CHECK(j["b"][0].as_bool());
    CHECK(j["b"][1].is_null());
    CHECK(j["c"]["d"].as_number() == 1.5);
    auto again = Json::parse(j.dump());
    CHECK(again["b"][2].as_string() == "x");
}

TEST(follows_latest_client) {
    CHECK(caracal::game_files::follows_latest_client(std::nullopt));
    CHECK(caracal::game_files::follows_latest_client_any(Json(nullptr)));
    CHECK(caracal::game_files::follows_latest_client_any(Json(0)));
    CHECK(caracal::game_files::follows_latest_client_any(Json("0")));
    CHECK(caracal::game_files::follows_latest_client_any(Json("")));
    CHECK(!caracal::game_files::follows_latest_client_any(Json(5158)));
    CHECK(!caracal::game_files::follows_latest_client_any(Json("5158")));
    CHECK(!caracal::game_files::follows_latest_client_any(Json("halflife3")));
}

TEST(game_files_fallback) {
    auto files = caracal::game_files::get_game_files_fallback();
    int html = -1, merrit = -1;
    for (int i = 0; i < static_cast<int>(files.size()); ++i) {
        if (files[i] == "/js/html.js") {
            html = i;
        }
        if (files[i] == "/js/merrit_stand_notice.js") {
            merrit = i;
        }
    }
    CHECK(html >= 0);
    CHECK(merrit >= 0);
    CHECK(merrit > html);
    auto runner = caracal::game_files::get_runner_files_fallback();
    CHECK(runner.size() == 4);
    CHECK(runner[0] == "/js/old_common_functions.js");
    CHECK(runner[3] == "/js/runner_compat.js");
    CHECK(caracal::game_files::get_game_files() == files);
    CHECK(caracal::game_files::get_runner_files() == runner);
}

static const char* INDEX_FIXTURE = R"(
<script src="/js/pixi/4.8.2-roundpixels/pixi.min.js"></script>
<script src="/js/pixi-layers/0.1.7.2/pixi-layers.js"></script>
<script src="/js/socket.io/4.2.0/socket.io.min.js"></script>
<script src="/js/jquery/jquery-3.2.0.min.js"></script>
<script src="/js/howler/2.0.13/howler.min.js"></script>
<script src="/js/libraries/combined.js?v=4"></script>
<script src="/js/codemirror/5.65.1/codemirror.js"></script>
<script src="/js/common_functions.js?v=8507"></script>
<script src="/js/old_common_functions.js?v=8507"></script>
<script src="/js/functions.js?v=8507"></script>
<script src="/js/game.js?v=8507"></script>
<script src="/js/html.js?v=8507"></script>
<script src="/js/merrit_stand_notice.js?v=8507"></script>
<script src="/js/steam_news.js?v=8507"></script>
<script src="/js/payments.js?v=8507"></script>
<script src="/js/keyboard.js?v=8507"></script>
<script src="/data.js?v=8507"></script>
<script src="/js/npc_obstruction_hint.js?v=8507"></script>
<script src="/js/ios-drag-drop.js"></script>
)";

static const char* RUNNER_FIXTURE = R"(
<script src="/js/jquery/jquery-3.2.0.min.js"></script>
<script src="/js/common_functions.js?v=8507"></script>
<script src="/js/old_common_functions.js?v=8507"></script>
<script src="/js/runner_functions.js?v=8507"></script>
<script src="/js/runner_compat.js?v=8507"></script>
)";

TEST(client_scripts_extract) {
    auto srcs = caracal::client_scripts::extract_script_srcs(R"(<script src="/js/game.js?v=9"></script>)");
    CHECK(srcs.size() == 1);
    CHECK(srcs[0] == "/js/game.js");
}

TEST(client_scripts_index) {
    auto files = caracal::client_scripts::resolve_game_files_from_index_html(INDEX_FIXTURE);
    CHECK(files[0] == "/js/pixi/fake/pixi.min.js");
    auto has = [&](const char* s) { return std::find(files.begin(), files.end(), s) != files.end(); };
    CHECK(has("/js/libraries/combined.js"));
    CHECK(has("/js/codemirror/fake/codemirror.js"));
    CHECK(has("/js/merrit_stand_notice.js"));
    CHECK(has("/js/steam_news.js"));
    CHECK(has("/js/npc_obstruction_hint.js"));
    CHECK(has("/data.js"));
    CHECK(!has("/js/howler/2.0.13/howler.min.js"));
    CHECK(!has("/js/ios-drag-drop.js"));
    for (auto& f : files) {
        CHECK(f.find("socket.io") == std::string::npos);
    }
    auto html = std::find(files.begin(), files.end(), "/js/html.js");
    auto merrit = std::find(files.begin(), files.end(), "/js/merrit_stand_notice.js");
    CHECK(html < merrit);
}

TEST(client_scripts_runner) {
    auto files = caracal::client_scripts::resolve_runner_files_from_runner_html(RUNNER_FIXTURE);
    CHECK(files.size() == 4);
    CHECK(files[0] == "/js/common_functions.js");
    CHECK(files[1] == "/js/old_common_functions.js");
    CHECK(files[2] == "/js/runner_functions.js");
    CHECK(files[3] == "/js/runner_compat.js");
}

TEST(html_globals_extract) {
    const char* fixture = R"(
<html><body>
<script>
	var inside="login";
	var proximity_guides='1';
	var last_deploy="[30/08/26]";
	var X={};
	function payment_logic(){};
	if(is_electron) { electron_init(); }
</script>
</body></html>
)";
    auto script = caracal::extract_page_globals(fixture);
    CHECK(script.find("var inside=\"login\"") != std::string::npos);
    CHECK(script.find("proximity_guides") != std::string::npos);
    CHECK(script.find("function payment_logic") != std::string::npos);
    CHECK(script.find("is_electron") == std::string::npos);
}

TEST(html_prelude_has_proximity_guides) {
    std::string s(caracal::html_prelude_source);
    CHECK(s.find("proximity_guides") != std::string::npos);
    CHECK(s.find("proximity_guides = false") != std::string::npos);
}

TEST(realm_safe_clone_overlay_present) {
    std::string s(caracal::realm_safe_clone_overlay);
    CHECK(s.find("__caracAL_realm_safe") != std::string::npos);
    CHECK(s.find("circular_attribute[clone]") != std::string::npos);
}

TEST(parse_realm_host) {
    auto a = caracal::parse_realm_host("wss://asia1.adventure.land/socket.io/");
    CHECK(a.host == "asia1.adventure.land");
    CHECK(!a.port);
    auto b = caracal::parse_realm_host("eu1.adventure.land:2053");
    CHECK(b.host == "eu1.adventure.land");
    CHECK(b.port && *b.port == 2053);
}

TEST(load_deadline_from_rtt) {
    using caracal::load_deadline_max_ms;
    using caracal::load_deadline_min_ms;
    using caracal::load_deadline_ms_from_rtt;
    using caracal::probe_timeout_ms;
    CHECK(load_deadline_ms_from_rtt(40) == load_deadline_min_ms + 12 * 40);
    CHECK(load_deadline_ms_from_rtt(40) < load_deadline_min_ms + 1000);
    int asia = load_deadline_ms_from_rtt(450);
    CHECK(asia > load_deadline_min_ms);
    CHECK(asia == load_deadline_min_ms + 12 * 450);
    int mid = load_deadline_ms_from_rtt(800);
    CHECK(mid > load_deadline_min_ms);
    CHECK(mid < load_deadline_max_ms);
    CHECK(mid == load_deadline_min_ms + 12 * 800);
    CHECK(load_deadline_ms_from_rtt(10'000) == load_deadline_max_ms);
    CHECK(load_deadline_ms_from_rtt(probe_timeout_ms) == load_deadline_max_ms);
}

TEST(should_refresh_client) {
    CHECK(caracal::should_refresh_client_for_welcome(17139, 17175));
    CHECK(!caracal::should_refresh_client_for_welcome(17175, 17139));
    CHECK(!caracal::should_refresh_client_for_welcome(17175, 17175));
    CHECK(!caracal::should_refresh_client_for_welcome(std::numeric_limits<double>::quiet_NaN(), 17175));
    CHECK(!caracal::should_refresh_client_for_welcome(17175, std::numeric_limits<double>::quiet_NaN()));
}

TEST(config_ports) {
    CHECK(caracal::config::privileged_port_ceiling() == 1024);
    CHECK(caracal::config::needs_elevated_bind(80));
    CHECK(!caracal::config::needs_elevated_bind(1924));
    CHECK(caracal::config::default_web_panel_port() == caracal::constants::default_web_panel_port_unix);
}

TEST(login_success_auth) {
    Json data = Json::array();
    Json msg = Json::object();
    msg["message"] = "Logged In!";
    data.as_array().push_back(msg);
    auto auth = caracal::config::login_success_auth(data, {"foo=1", "auth=abc-def; Path=/", "other=x"});
    CHECK(auth && *auth == "abc-def");
    Json fail = Json::array();
    Json m2 = Json::object();
    m2["message"] = "Wrong Password";
    fail.as_array().push_back(m2);
    CHECK(!caracal::config::login_success_auth(fail, {"auth=abc-def"}));
}

TEST(monitoring_items) {
    Json item = Json::object();
    item["name"] = "hpot1";
    item["q"] = 12;
    auto slim = caracal::monitoring::slim_item_instance(item);
    CHECK(slim && (*slim)["name"].as_string() == "hpot1");
    CHECK((*slim)["q"].as_number() == 12);
    Json items = Json::array();
    items.as_array().push_back(item);
    Json G = Json::object();
    G["items"] = Json::object();
    auto hp = caracal::monitoring::best_pot_instance(items, G, "hp");
    CHECK(hp && (*hp)["name"].as_string() == "hpot1");
    CHECK(caracal::monitoring::find_pot_slot(items, G, "hp") == 0);
    CHECK(caracal::monitoring::find_pot_slot(items, G, "mp") == -1);
    items.as_array().push_back(Json(nullptr));
    Json mp = Json::object();
    mp["name"] = "mpot0";
    mp["q"] = 3;
    items.as_array().push_back(mp);
    CHECK(caracal::monitoring::find_pot_slot(items, G, "mp") == 2);
    CHECK(caracal::monitoring::find_pot_slot(items, G, "hp") == 0);
    CHECK(caracal::monitoring::humanize_int(500, 1) == "500");
    CHECK(caracal::monitoring::humanize_int(1500, 1) == "1.5k");
}

TEST(character_auth_reject) {
    CHECK(caracal::character::is_auth_reject_text("authentication_failed"));
    CHECK(caracal::character::is_auth_reject_payload(Json("Wrong passphrase")));
    Json o = Json::object();
    o["phrase"] = "characters_unconfirmed";
    CHECK(caracal::character::is_auth_reject_payload(o));
    CHECK(!caracal::character::is_auth_reject_payload(Json("hello")));
    caracal::character::ProcessArgs a;
    a.base_url = "https://adventure.land";
    CHECK(caracal::character::server_address(a, "asia1.adventure.land") == "wss://asia1.adventure.land");
    a.base_url = "http://localhost:8083";
    CHECK(caracal::character::server_address(a, "127.0.0.1") == "ws://127.0.0.1");
    CHECK(caracal::character::msg_process_ready()["type"].as_string() == "process_ready");
}

TEST(file_stored_key_values) {
    auto dir = fs::temp_directory_path() / ("caracal-fskv-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    auto main = (dir / "garage.jsonl").string();
    auto repl = (dir / "garage.new.jsonl").string();
    {
        caracal::FileStoredKeyValues kv(main, repl, std::chrono::milliseconds(60'000));
        kv.set("a", "1").set("b", "2");
        CHECK(kv.get("a") == std::optional<std::string>{"1"});
        kv.erase("b");
        CHECK(!kv.get("b"));
        kv.shutdown();
    }
    {
        caracal::FileStoredKeyValues kv(main, repl, std::chrono::milliseconds(60'000));
        CHECK(kv.get("a") == std::optional<std::string>{"1"});
        CHECK(!kv.get("b"));
        kv.close();
    }
    fs::remove_all(dir);
}

TEST(ipc_storage_roundtrip) {
    Json last = Json(nullptr);
    caracal::IpcStorage st("ls", [&](const Json& m) { last = m; });
    CHECK(last["op"].as_string() == "init");
    st.set_item("k", "v");
    CHECK(st.get_item("k") == std::optional<std::string>{"v"});
    CHECK(last["op"].as_string() == "set");
    Json incoming = Json::object();
    incoming["type"] = "stor";
    incoming["ident"] = "ls";
    incoming["op"] = "set";
    Json data = Json::object();
    data["k2"] = "z";
    incoming["data"] = data;
    st.on_message(incoming);
    CHECK(st.get_item("k2") == std::optional<std::string>{"z"});
}

TEST(socket_io_event_roundtrip) {
    Json data = Json::object();
    data["id"] = "m1";
    auto enc = caracal::encode_socket_event("attack", data);
    CHECK(enc.starts_with("42"));
    auto pkt = caracal::parse_engine_packet(enc);
    CHECK(pkt.has_value());
    CHECK(pkt->event == "attack");
    CHECK(pkt->data["id"].as_string() == "m1");
}

TEST(entity_distance_and_json) {
    CHECK(caracal::simple_distance(0, 0, 3, 4) == 5);
    auto e = caracal::entity_from_json(Json::parse(R"({"id":"a","type":"crab","x":1,"y":2,"hp":10})"));
    CHECK(e.id == "a");
    CHECK(e.type == "crab");
    CHECK(e.x == 1);
    auto n = caracal::entity_from_json(Json::parse(R"({"id":4123,"type":"monster","mtype":"crab","x":10,"y":20})"));
    CHECK(n.id == "4123");
    CHECK(n.type == "crab");
    CHECK(caracal::entity_matches_type(n, "crab"));
    CHECK(!n.moving);
    auto z = caracal::entity_from_json(Json::parse(R"({"id":1,"x":0,"y":0,"range":0})"));
    CHECK(z.range == 40);
}

TEST(data_js_parse) {
    auto g = caracal::try_parse_data_js("var G={\"version\":1,\"maps\":{}};");
    CHECK(g["version"].as_number() == 1);
    CHECK(g["maps"].is_object());
}

TEST(pathing_walls_and_clamp) {
    Json node = Json::object();
    node["min_x"] = -100;
    node["max_x"] = 100;
    node["min_y"] = -100;
    node["max_y"] = 100;
    Json xs = Json::array();
    Json wall = Json::array();
    wall.as_array().push_back(0);
    wall.as_array().push_back(-20);
    wall.as_array().push_back(20);
    xs.as_array().push_back(wall);
    node["x_lines"] = xs;
    node["y_lines"] = Json::array();
    auto geo = caracal::map_geo_from_json(node, "test");
    CHECK(geo.ok);
    CHECK(caracal::can_stand(geo, -40, 0));
    CHECK(!caracal::can_move(geo, -1, 0, 1, 0));
    CHECK(caracal::can_move(geo, -40, 0, -20, 0));
    CHECK(!caracal::can_move(geo, -40, 0, 40, 0));
    CHECK(caracal::can_move(geo, -40, 50, 40, 50));
    CHECK(!caracal::in_bounds(geo, 200, 0));
    auto clamped = caracal::clamp_move(geo, -40, 0, 40, 0);
    CHECK(clamped.has_value());
    CHECK(clamped->first < 0);
    CHECK(clamped->first > -40);
}

TEST(pathing_find_path_around_wall) {
    Json node = Json::object();
    node["min_x"] = -100;
    node["max_x"] = 100;
    node["min_y"] = -100;
    node["max_y"] = 100;
    Json xs = Json::array();
    Json wall = Json::array();
    wall.as_array().push_back(0);
    wall.as_array().push_back(-20);
    wall.as_array().push_back(20);
    xs.as_array().push_back(wall);
    node["x_lines"] = xs;
    node["y_lines"] = Json::array();
    auto geo = caracal::map_geo_from_json(node, "test");
    auto direct = caracal::find_path(geo, -40, 50, 40, 50);
    CHECK(direct.size() == 1);
    auto around = caracal::find_path(geo, -40, 0, 40, 0);
    CHECK(!around.empty());
    double px = -40, py = 0;
    bool went_around = false;
    for (auto [x, y] : around) {
        CHECK(caracal::can_move(geo, px, py, x, y));
        if (std::abs(y) > 20) {
            went_around = true;
        }
        px = x;
        py = y;
    }
    CHECK(went_around);
    CHECK(std::hypot(around.back().first - 40, around.back().second - 0) < 25);
}

TEST(pathing_main_bee_wall) {
    const char* path = "game_files/adventure.land/17665/data.js";
    if (!fs::exists(path)) {
        return;
    }
    auto g = caracal::load_g_data(path);
    auto geo = caracal::map_geo_from_g(g, "main");
    CHECK(geo.ok);
    CHECK(!caracal::can_move(geo, 224, 706, 450.4, 705.1));
    auto around = caracal::find_path(geo, 224, 706, 450.4, 705.1);
    CHECK(!around.empty());
    bool left_the_wall = false;
    for (auto [x, y] : around) {
        if (x < 224 - 4 || y < 680 || y > 776) {
            left_the_wall = true;
        }
    }
    CHECK(left_the_wall);
    auto slide = caracal::calculate_move(geo, 224, 706, 215.9, 796);
    auto unstuck = caracal::unstuck_move(geo, 224, 706, 215.9, 796);
    CHECK(unstuck.has_value());
    CHECK(std::hypot(unstuck->first - 224, unstuck->second - 706) > 1);
    (void)slide;
}

TEST(plugin_load_and_tick) {
#ifdef CARACAL_TEST_PLUGIN
    const char* path = CARACAL_TEST_PLUGIN;
    if (!fs::exists(path)) {
        return;
    }
    caracal::PluginHost host;
    CaracalApi api{};
    api.abi = CARACAL_PLUGIN_ABI;
    host.bind(api);
    host.load(path);
    CHECK(host.loaded());
    host.on_tick(1);
    host.unload();
    CHECK(!host.loaded());
#endif
}

int main() {
    int ran = 0;
    for (auto& c : caracal::test::registry()) {
        ++ran;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++caracal::test::failures;
            std::cerr << "EXC " << c.name << ": " << e.what() << "\n";
        }
    }
    if (caracal::test::failures) {
        std::cerr << caracal::test::failures << " failure(s) in " << ran << " tests (" << caracal::test::checks
                  << " checks)\n";
        return 1;
    }
    std::cout << ran << " tests, " << caracal::test::checks << " checks ok\n";
    return 0;
}
