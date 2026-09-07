#pragma once

// Smallest thing that can call itself a test framework: a counter, a macro and a summary.
// Deliberately dependency-free so the tests build anywhere the server builds.

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace f4mp::test
{
	inline int g_checks = 0;
	inline int g_failures = 0;
	inline std::string g_suite;

	inline void Suite(std::string_view a_name)
	{
		g_suite = a_name;
		std::printf("\n-- %s\n", g_suite.c_str());
	}

	inline void Report(bool a_ok, std::string_view a_what, const char* a_file, int a_line, std::string_view a_detail = {})
	{
		++g_checks;
		if (a_ok) {
			std::printf("   ok   %.*s\n", static_cast<int>(a_what.size()), a_what.data());
			return;
		}
		++g_failures;
		std::printf("   FAIL %.*s\n        at %s:%d\n", static_cast<int>(a_what.size()), a_what.data(), a_file, a_line);
		if (!a_detail.empty()) {
			std::printf("        %.*s\n", static_cast<int>(a_detail.size()), a_detail.data());
		}
	}

	[[nodiscard]] inline bool Near(double a_a, double a_b, double a_epsilon = 0.001)
	{
		return std::fabs(a_a - a_b) <= a_epsilon;
	}

	[[nodiscard]] inline int Summary()
	{
		std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
		return g_failures == 0 ? 0 : 1;
	}
}

#define CHECK(cond, what) ::f4mp::test::Report((cond), (what), __FILE__, __LINE__)
#define CHECK_MSG(cond, what, detail) ::f4mp::test::Report((cond), (what), __FILE__, __LINE__, (detail))
