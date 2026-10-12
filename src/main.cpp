#include "config_util.hpp"
#include "constants.hpp"
#include "coordinator.hpp"
#include "log_utils.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        std::string path = argc > 1 ? argv[1] : "./config.json";
        if (!caracal::config::config_exists(path)) {
            std::cerr << "Using config path " << path << "\n";
        }
        return caracal::run_coordinator(path);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
