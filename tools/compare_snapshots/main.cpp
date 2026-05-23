// compare_snapshots — WorldSnapshot field-level diff CLI (SCHE-06, D-19).
//
// Reads two fixture/snapshot JSON files, walks their "expected" objects field
// by field, applies per-field absolute tolerances from ToleranceMatchers.h
// (the single source of truth shared with the GoogleTest matchers, D-18), and
// prints a field-level diff. Exit codes:
//   0  all compared fields match within tolerance ("PASS")
//   1  one or more field mismatches ("FAIL", diff printed)
//   2  wrong argument count or JSON parse error (usage / error on stderr)
//
// This is developer/CI tooling, not game source: no GPL header, no SPRaise —
// errors are reported via std::cerr + return codes. Untrusted JSON is parsed
// with allow_exceptions=false and guarded with contains() before at(), so a
// malformed input file can never throw or crash (threats T-02-14, T-02-16).

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "ToleranceMatchers.h" // resolved via Tests/Helpers include path

using json = nlohmann::json;
using namespace spades::tests; // ToleranceForField + tolerance constants

// Recursively compare two JSON values, building a dotted/indexed field path.
// Floats compare within ToleranceForField(path); integers/bools/strings/null
// compare exactly. Sets anyMismatch=true on any difference and prints a line.
//
// SYMMETRIC by design (CR-01): the comparison flags a field present in EITHER
// snapshot but absent in the other. A key in `a` missing from `b` is reported
// MISSING; a key in `b` missing from `a` is reported EXTRA. This is essential to
// the cross-language parity guarantee — a port (`b`) that emits undocumented
// extra fields, or omits expected ones, must FAIL rather than silently PASS, and
// the verdict must not depend on argument order.
static void compareFields(const json& a, const json& b, const std::string& path,
                          bool& anyMismatch) {
	if (a.is_object() && b.is_object()) {
		for (auto& [key, va] : a.items()) {
			if (!b.contains(key)) {
				std::cout << "MISSING  " << path << "." << key << "\n";
				anyMismatch = true;
				continue;
			}
			// b.contains(key) verified above → b.at(key) cannot throw (T-02-16).
			compareFields(va, b.at(key), path + "." + key, anyMismatch);
		}
		// Symmetric pass: flag keys present in b but absent in a as EXTRA so a
		// port emitting undocumented fields cannot slip through unchecked.
		for (auto& [key, vb] : b.items()) {
			(void)vb;
			if (!a.contains(key)) {
				std::cout << "EXTRA    " << path << "." << key << "\n";
				anyMismatch = true;
			}
		}
		return;
	}

	if (a.is_array() && b.is_array()) {
		if (a.size() != b.size()) {
			std::cout << "MISMATCH " << path << "  a.size=" << a.size()
			          << "  b.size=" << b.size() << "\n";
			anyMismatch = true;
		}
		const std::size_t n = std::min(a.size(), b.size());
		for (std::size_t i = 0; i < n; ++i) {
			compareFields(a[i], b[i], path + "[" + std::to_string(i) + "]",
			              anyMismatch);
		}
		return;
	}

	if (a.is_number_float() || b.is_number_float()) {
		const double va = a.get<double>();
		const double vb = b.get<double>();
		const double tol = ToleranceForField(path);
		if (std::abs(va - vb) > tol) {
			std::cout << "MISMATCH " << path << "  a=" << va << "  b=" << vb
			          << "  diff=" << std::abs(va - vb) << "  tol=" << tol << "\n";
			anyMismatch = true;
		}
		return;
	}

	// Integer, bool, string, null, or mismatched container types: exact compare.
	if (a != b) {
		std::cout << "MISMATCH " << path << "  a=" << a.dump() << "  b=" << b.dump()
		          << "\n";
		anyMismatch = true;
	}
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "usage: compare_snapshots <baseline.json> <candidate.json>\n"
		             "  baseline  : the reference/oracle snapshot (file_a)\n"
		             "  candidate : the snapshot under test, e.g. a port's output (file_b)\n"
		             "Comparison is symmetric: fields present in only one file are reported\n"
		             "(MISSING = in baseline, absent in candidate; EXTRA = in candidate,\n"
		             "absent in baseline). The PASS/FAIL verdict is independent of order.\n";
		return 2;
	}

	std::ifstream fa(argv[1]);
	std::ifstream fb(argv[2]);

	// allow_exceptions=false: malformed JSON yields a discarded value instead of
	// throwing, so a bad input file exits cleanly with code 2 (T-02-14).
	json a = json::parse(fa, nullptr, /*allow_exceptions=*/false);
	json b = json::parse(fb, nullptr, /*allow_exceptions=*/false);
	if (a.is_discarded() || b.is_discarded()) {
		std::cerr << "JSON parse error\n";
		return 2;
	}

	if (!a.contains("expected") || !b.contains("expected")) {
		std::cerr << "Input files must have an 'expected' key\n";
		return 2;
	}

	bool anyMismatch = false;
	// at("expected") is safe — contains("expected") verified on both above.
	compareFields(a.at("expected"), b.at("expected"), "expected", anyMismatch);

	std::cout << (anyMismatch ? "FAIL\n" : "PASS\n");
	return anyMismatch ? 1 : 0;
}
