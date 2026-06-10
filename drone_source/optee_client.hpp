#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hesia {

enum class OpteeMldsaSlot : std::uint32_t {
    DroneIdentity = 0,
    ServerIdentity = 1,
};

bool optee_available();
void optee_set_session_auth_secret_path(const std::filesystem::path& path);
void optee_require_session_auth(bool required);
void optee_require_session_auth_ready_or_throw();
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
std::vector<uint8_t> optee_get_attestation_public_key();
std::vector<uint8_t> optee_sign_attestation_digest(const std::vector<uint8_t>& digest);
bool optee_check_and_update_firmware_version(std::uint64_t version);
bool optee_restore_session_auth_from_blob(const std::vector<uint8_t>& sealed_or_raw_secret);
bool optee_rotate_session_auth_secret(const std::vector<uint8_t>& new_secret);
bool optee_import_mldsa_key_from_sealed_blob(const std::filesystem::path& sealed_path,
                                             OpteeMldsaSlot slot = OpteeMldsaSlot::DroneIdentity);
std::vector<uint8_t> optee_get_mldsa_public_key(OpteeMldsaSlot slot = OpteeMldsaSlot::DroneIdentity);
std::vector<uint8_t> optee_sign_mldsa_payload(const std::vector<uint8_t>& payload,
                                              OpteeMldsaSlot slot = OpteeMldsaSlot::DroneIdentity);
bool optee_mldsa_signing_ready(OpteeMldsaSlot slot = OpteeMldsaSlot::DroneIdentity);

enum class OpteeSlotId : std::uint8_t {
    Invalid = 0,
    SlotA = 1,
    SlotB = 2,
};

struct OpteeSlotMeta {
    bool initialized = false;
    OpteeSlotId active_slot = OpteeSlotId::Invalid;
    OpteeSlotId pending_slot = OpteeSlotId::Invalid;
    std::uint64_t max_firmware_version = 0;
    std::uint64_t slot_a_firmware_version = 0;
    std::uint64_t slot_b_firmware_version = 0;
    std::uint64_t slot_a_asset_version = 0;
    std::uint64_t slot_b_asset_version = 0;
    std::uint64_t pending_firmware_version = 0;
    std::uint64_t pending_asset_version = 0;
};

OpteeSlotId optee_slot_id_from_string(const std::string& slot);
std::string optee_slot_id_to_string(OpteeSlotId slot);
bool optee_stage_slot_update(OpteeSlotId slot,
                             std::uint64_t firmware_version,
                             std::uint64_t asset_version);
bool optee_commit_slot_boot(OpteeSlotId slot,
                            std::uint64_t firmware_version,
                            std::uint64_t asset_version);
OpteeSlotMeta optee_read_slot_meta();

} // namespace hesia
