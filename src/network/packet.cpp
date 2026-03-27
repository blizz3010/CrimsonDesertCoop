#include <cdcoop/network/packet.h>

namespace cdcoop {

std::vector<uint8_t> PacketBuilder::serialize(const PacketHeader& header, const void* payload, size_t size) {
    std::vector<uint8_t> buf(sizeof(PacketHeader) + size);
    memcpy(buf.data(), &header, sizeof(PacketHeader));
    if (payload && size > 0) {
        memcpy(buf.data() + sizeof(PacketHeader), payload, size);
    }
    return buf;
}

bool PacketBuilder::validate(const uint8_t* data, size_t size) {
    if (size < sizeof(PacketHeader)) return false;
    auto* header = reinterpret_cast<const PacketHeader*>(data);
    if (header->magic[0] != 'C' || header->magic[1] != 'D') return false;
    if (sizeof(PacketHeader) + header->payload_size > size) return false;
    return true;
}

} // namespace cdcoop
