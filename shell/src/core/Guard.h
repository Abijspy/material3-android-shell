#pragma once

#include <exception>
#include <iostream>

namespace m3 {

// Runs `fn`; an exception it throws is logged and dropped, so one bad value
// (a D-Bus reply of an unexpected shape, a setting of the wrong type) can't
// end the shell. Used wherever callbacks enter our code from the event loop
// or from C libraries (libwayland, sd-bus), which exceptions must never
// unwind through.
template <typename F>
void guard(const char* what, F&& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        std::cerr << "material3ui: error in " << what << " (kept running): " << e.what() << '\n';
    } catch (...) {
        std::cerr << "material3ui: unknown error in " << what << " (kept running)\n";
    }
}

} // namespace m3
