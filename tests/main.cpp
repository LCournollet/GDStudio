#include "TestFramework.hpp"

#include <chrono>
#include <cstring>

int main(int argc, char** argv) {
    char const* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (auto& tc : gddstest::registry()) {
        if (filter && !std::strstr(tc.name, filter)) continue;
        try {
            tc.fn();
            ++passed;
            std::printf("[ OK ] %s\n", tc.name);
        }
        catch (gddstest::Failure const& f) {
            ++failed;
            std::printf("[FAIL] %s\n       %s\n", tc.name, f.message.c_str());
        }
        catch (std::exception const& e) {
            ++failed;
            std::printf("[FAIL] %s\n       exception: %s\n", tc.name, e.what());
        }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d passed, %d failed, %d checks (%.0f ms)\n", passed, failed, gddstest::checksRun(), ms);
    return failed ? 1 : 0;
}
