// time_utils.h
// Utilidades mínimas para medir tiempos de ejecución usando chrono.
// Imprime los tiempos solo si la salida estándar es un TTY (ejecución desde consola).
#pragma once

#include <chrono>
#include <string>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <filesystem>

namespace time_utils {

inline bool is_console() {
    return ::isatty(fileno(stdout));
}

struct ScopedTimer {
    using clock = std::chrono::steady_clock;
    std::string name;
    clock::time_point start;
    bool enabled;

    explicit ScopedTimer(const std::string &n) : name(n), start(clock::now()), enabled(is_console()) {}

    ~ScopedTimer() {
        if (!enabled) return;
        auto end = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "[TIMER] " << name << " -> " << ms << " ms" << std::endl;
        // Also append to project-root `output/timings.csv`.
        try {
            auto cwd = std::filesystem::current_path();
            std::filesystem::path outdir;
            if (cwd.filename() == "output") {
                // running from output/, use parent/output to avoid output/output
                outdir = cwd.parent_path() / "output";
            } else {
                outdir = cwd / "output";
            }
            std::filesystem::create_directories(outdir);
            std::ofstream out((outdir / "timings.csv").string(), std::ios::app);
            if (out.is_open()) {
                out << name << "," << ms << "\n";
            }
        } catch (...) {
            // ignore any errors writing timings
        }
    }
};

} // namespace time_utils
