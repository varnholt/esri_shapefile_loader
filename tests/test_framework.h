#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace test
{

struct TestCase
{
   std::string_view _name;
   std::function<void()> _body;
};

inline std::vector<TestCase>& registry()
{
   static std::vector<TestCase> test_cases;
   return test_cases;
}

inline int32_t& failureCount()
{
   static int32_t failures = 0;
   return failures;
}

struct Registrar
{
   Registrar(std::string_view name, std::function<void()> body)
   {
      registry().push_back({name, std::move(body)});
   }
};

inline void check(bool condition, std::string_view expression, const std::string& details, std::source_location location)
{
   if (!condition)
   {
      ++failureCount();
      std::println("   FAILED {}:{}: {} {}", location.file_name(), location.line(), expression, details);
   }
}

/// \brief runs all registered tests; returns the process exit code.
inline int32_t runAll()
{
   for (const auto& test_case : registry())
   {
      std::println("[ RUN  ] {}", test_case._name);
      const auto failures_before = failureCount();
      test_case._body();
      std::println("[ {} ] {}", failureCount() == failures_before ? " OK " : "FAIL", test_case._name);
   }
   std::println("{} failed check(s)", failureCount());
   return failureCount() == 0 ? 0 : 1;
}

}  // namespace test

#define TEST_CONCAT_INNER(lhs, rhs) lhs##rhs
#define TEST_CONCAT(lhs, rhs) TEST_CONCAT_INNER(lhs, rhs)

#define TEST_CASE(name)                                                                                     \
   static void TEST_CONCAT(test_body_, __LINE__)();                                                         \
   static test::Registrar TEST_CONCAT(test_registrar_, __LINE__){name, &TEST_CONCAT(test_body_, __LINE__)}; \
   static void TEST_CONCAT(test_body_, __LINE__)()

#define CHECK(condition) test::check((condition), #condition, "", std::source_location::current())

#define CHECK_MESSAGE(condition, ...) test::check((condition), #condition, std::format(__VA_ARGS__), std::source_location::current())
