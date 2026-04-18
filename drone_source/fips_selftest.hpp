// fips_selftest.hpp
// Power-up self tests (KAT/CAST style) for the FIPS-oriented wrapper.
//
// IMPORTANT:
// - In a real FIPS 140-3 validation, self-tests must match the exact module
//   boundary, lifecycle, and CMVP Implementation Guidance expectations.
// - If you are binding/embedding an already validated module (e.g. OpenSSL FIPS
//   Provider), that module already implements its own integrity + self-tests.
//   This file provides an additional defensive layer and a clean place to keep
//   deterministic tests, but it may be redundant for a bound module.

#pragma once

#include <string>

namespace hesia::fips::selftest {

// Runs a small set of deterministic Known Answer Tests (KAT) / Conditional
// Algorithm Self Tests (CAST-like) for:
// - SHA-256
// - HMAC-SHA-256
// - HKDF-SHA-256 (RFC 5869 test case 1)
// - AES-256-GCM (encrypt KAT)
//
// propq:
//   - nullptr      : use default OpenSSL fetch properties
//   - "fips=yes"   : force FIPS provider algorithms (requires FIPS provider)
//
// Returns true if all tests pass, else false and fills err.
bool RunAll(const char* propq, std::string& err);

} // namespace hesia::fips::selftest
