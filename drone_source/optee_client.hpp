#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace hesia {

bool optee_available();
std::filesystem::path optee_default_sealed_puf_path();
std::filesystem::path optee_default_dilithium_sealed_path();
std::vector<uint8_t> optee_unseal_file(const std::filesystem::path& sealed_path,
                                       size_t expected_len);
bool optee_seal_file(const std::filesystem::path& sealed_path,
                     const std::vector<uint8_t>& plaintext);
bool optee_rotate_key();
bool optee_wipe_key();
std::vector<uint8_t> optee_hkdf_sha3_512(const std::vector<uint8_t>& ikm,
                                         const std::vector<uint8_t>& salt,
                                         const std::vector<uint8_t>& info,
                                         size_t out_len);
bool optee_check_and_update_firmware_version(std::uint64_t version);

} // namespace hesia
