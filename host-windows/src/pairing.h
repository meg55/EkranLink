// Eşleşmiş tabletlerin kalıcı anahtarları: HKCU\Software\EkranLink\Pairs\<cihaz kimliği>
#pragma once

#include <array>
#include <string>

#include "crypto.h"

namespace ekran::pairing {

std::array<uint8_t, 16> pcId();  // ilk çağrıda rastgele üretilip saklanır
std::string pcName();
bool load(const std::string& deviceId, crypto::Bytes32& key);
void save(const std::string& deviceId, const crypto::Bytes32& key);
void forget(const std::string& deviceId);
void forgetAll();

}  // namespace ekran::pairing
