#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hesia {

std::vector<uint8_t> kdf_sp800_108_hmac_sha256(const std::vector<uint8_t>& key,
                                               const std::string& label,
                                               const std::vector<uint8_t>& context,
                                               size_t out_len);

} // namespace hesia
