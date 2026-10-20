#pragma once

// Minimal self-registering unit test framework (no external dependency).

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace gddstest {

struct TestCase {
    char const* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(char const* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& checksRun() {
    static int n = 0;
    return n;
}

}  // namespace gddstest

#define GDDS_CONCAT2(a, b) a##b
#define GDDS_CONCAT(a, b) GDDS_CONCAT2(a, b)

#define TEST_CASE(name)                                                                       \
    static void GDDS_CONCAT(test_fn_, __LINE__)();                                            \
    static ::gddstest::Registrar GDDS_CONCAT(test_reg_, __LINE__)(name, &GDDS_CONCAT(test_fn_, __LINE__)); \
    static void GDDS_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++::gddstest::checksRun();                                                            \
        if (!(cond)) {                                                                        \
            std::ostringstream _os;                                                           \
            _os << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond;                   \
            throw ::gddstest::Failure{_os.str()};                                             \
        }                                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        ++::gddstest::checksRun();                                                            \
        auto _va = (a);                                                                       \
        auto _vb = (b);                                                                       \
        if (!(_va == _vb)) {                                                                  \
            std::ostringstream _os;                                                           \
            _os << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b          \
                << "  (" << _va << " vs " << _vb << ")";                                     \
            throw ::gddstest::Failure{_os.str()};                                             \
        }                                                                                     \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                 \
    do {                                                                                      \
        ++::gddstest::checksRun();                                                            \
        double _va = double(a), _vb = double(b);                                              \
        if (std::abs(_va - _vb) > (eps)) {                                                    \
            std::ostringstream _os;                                                           \
            _os << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a " ~ " #b         \
                << "  (" << _va << " vs " << _vb << ")";                                     \
            throw ::gddstest::Failure{_os.str()};                                             \
        }                                                                                     \
    } while (0)
