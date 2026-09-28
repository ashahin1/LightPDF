#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <functional>
#include <chrono>

namespace doctest {

struct Approx {
    double value;
    double epsilon = 0.001;

    explicit Approx(double val) : value(val) {}
    Approx& epsilon_value(double eps) { epsilon = eps; return *this; }

    bool operator==(double other) const {
        return std::abs(value - other) <= epsilon;
    }
    bool operator==(float other) const {
        return std::abs(value - (double)other) <= epsilon;
    }
};

inline bool operator==(double other, const Approx& a) { return a == other; }
inline bool operator==(float other, const Approx& a) { return a == other; }

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

class Registry {
public:
    static Registry& instance() {
        static Registry reg;
        return reg;
    }

    void add(const std::string& suite, const std::string& name, std::function<void()> fn) {
        m_tests.push_back({ suite, name, std::move(fn) });
    }

    int run() {
        int passedCases = 0;
        int failedCases = 0;
        int totalAsserts = 0;
        int failedAsserts = 0;

        std::cout << "===============================================================================\n";
        std::cout << "[LightPDF Test Runner] Executing " << m_tests.size() << " test cases...\n";
        std::cout << "===============================================================================\n";

        auto startTime = std::chrono::high_resolution_clock::now();

        for (const auto& tc : m_tests) {
            currentTestCasePassed = true;
            currentFailCount = 0;
            currentAssertCount = 0;

            std::cout << "\n[RUN] " << (tc.suite.empty() ? "" : tc.suite + "::") << tc.name << "\n";
            try {
                tc.fn();
            } catch (const std::exception& e) {
                std::cout << "  [EXCEPTION] Unhandled exception: " << e.what() << "\n";
                currentTestCasePassed = false;
                currentFailCount++;
            } catch (...) {
                std::cout << "  [EXCEPTION] Unknown exception thrown!\n";
                currentTestCasePassed = false;
                currentFailCount++;
            }

            totalAsserts += currentAssertCount;
            failedAsserts += currentFailCount;

            if (currentTestCasePassed && currentFailCount == 0) {
                std::cout << "  [PASS] (" << currentAssertCount << " assertions)\n";
                passedCases++;
            } else {
                std::cout << "  [FAIL] (" << currentFailCount << " failed assertions)\n";
                failedCases++;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        std::cout << "\n===============================================================================\n";
        std::cout << "TEST RESULTS:\n";
        std::cout << "  Test Cases: " << passedCases << " passed | " << failedCases << " failed | " << m_tests.size() << " total\n";
        std::cout << "  Assertions: " << (totalAsserts - failedAsserts) << " passed | " << failedAsserts << " failed | " << totalAsserts << " total\n";
        std::cout << "  Elapsed:    " << ms << " ms\n";
        std::cout << "===============================================================================\n";

        if (failedCases == 0) {
            std::cout << ">>> ALL TESTS PASSED SUCCESSFULLY! <<<\n";
            return 0;
        } else {
            std::cout << ">>> TEST RUN FAILED! <<<\n";
            return 1;
        }
    }

    bool currentTestCasePassed = true;
    int currentAssertCount = 0;
    int currentFailCount = 0;
    std::string currentSuite;

private:
    std::vector<TestCase> m_tests;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> fn) {
        Registry::instance().add(suite, name, std::move(fn));
    }
};

struct RequireFailedException {};

} // namespace doctest

#define DOCTEST_CONCAT_IMPL(s1, s2) s1##s2
#define DOCTEST_CONCAT(s1, s2) DOCTEST_CONCAT_IMPL(s1, s2)

#define TEST_SUITE(name) namespace

#define TEST_CASE(name) \
    static void DOCTEST_CONCAT(test_case_fn_, __LINE__)(); \
    static const ::doctest::TestRegistrar DOCTEST_CONCAT(registrar_, __LINE__)( \
        ::doctest::Registry::instance().currentSuite, name, DOCTEST_CONCAT(test_case_fn_, __LINE__) \
    ); \
    static void DOCTEST_CONCAT(test_case_fn_, __LINE__)()

#define SUBCASE(name) \
    if (true)

#define CHECK(expr) \
    do { \
        ::doctest::Registry::instance().currentAssertCount++; \
        if (!(expr)) { \
            ::doctest::Registry::instance().currentTestCasePassed = false; \
            ::doctest::Registry::instance().currentFailCount++; \
            std::cout << "  CHECK FAILED: " << #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        } \
    } while (false)

#define CHECK_FALSE(expr) CHECK(!(expr))

#define REQUIRE(expr) \
    do { \
        ::doctest::Registry::instance().currentAssertCount++; \
        if (!(expr)) { \
            ::doctest::Registry::instance().currentTestCasePassed = false; \
            ::doctest::Registry::instance().currentFailCount++; \
            std::cout << "  REQUIRE FAILED: " << #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            throw ::doctest::RequireFailedException(); \
        } \
    } while (false)

#define REQUIRE_FALSE(expr) REQUIRE(!(expr))

#ifdef DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
int main(int, char**) {
    return ::doctest::Registry::instance().run();
}
#endif
