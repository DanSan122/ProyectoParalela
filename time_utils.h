// time_utils.h
// Utilidades mínimas para medir tiempos de ejecución usando chrono.
// Imprime los tiempos solo si la salida estándar es un TTY (ejecución desde consola).
#pragma once

#include <chrono>
#include <string>
#include <iostream>
#include <unistd.h>

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
    }
};

} // namespace time_utils
