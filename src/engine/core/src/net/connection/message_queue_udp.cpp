#include "halley/net/connection/message_queue_udp.h"

using namespace Halley;

MessageQueueUDP::MessageQueueUDP(std::shared_ptr<AckUnreliableConnection> connection)
    : connection(std::move(connection))
{

}

bool MessageQueueUDP::isConnected() const
{
    return connection->getStatus() == ConnectionStatus::Connected;
}

void MessageQueueUDP::enqueue(OutboundNetworkPacket packet, uint8_t channel)
{
    Outbound p = {packet, channel};
    outboundQueued.emplace_back(p);
}

void MessageQueueUDP::sendAll()
{
    if (connection->getStatus() != ConnectionStatus::Connected) {
        return;
    }

    constexpr size_t maxSubPacketsInFlight = 32;
    constexpr size_t maxSubPacketsPerFlush = 8;
    constexpr size_t maxLogicalPacketsPerFlush = 8;

    size_t sentQueued = 0;
    size_t sentSubPackets = 0;
    size_t sentLogicalPackets = 0;
    for (auto& p : outboundQueued) {
        const size_t nSubPackets = connection->estimateNumOutboundPackets(p.packet.getSize());
        const bool forceOne = sentLogicalPackets == 0 && connection->getNumOutboundPacketsInFlight() == 0;
        if (!forceOne) {
            if (sentLogicalPackets >= maxLogicalPacketsPerFlush) {
                break;
            }
            if (sentSubPackets + nSubPackets > maxSubPacketsPerFlush) {
                break;
            }
            if (connection->getNumOutboundPacketsInFlight() + nSubPackets > maxSubPacketsInFlight) {
                break;
            }
        }

        connection->send(IConnection::TransmissionType::Unreliable, p.packet);
        sentSubPackets += nSubPackets;
        ++sentLogicalPackets;
        ++sentQueued;

        if (connection->getStatus() != ConnectionStatus::Connected) {
            break;
        }
    }

    if (sentQueued > 0) {
        connection->flushOutboundQueue();
        outboundQueued.erase(outboundQueued.begin(), outboundQueued.begin() + static_cast<ptrdiff_t>(sentQueued));
    }

}

Vector<InboundNetworkPacket> MessageQueueUDP::receivePackets()
{
    Vector<InboundNetworkPacket> packetsIn;

    InboundNetworkPacket packet;
    while (connection->receive(packet)) {
        packetsIn.push_back(std::move(packet));
    }

    return packetsIn;
}

void MessageQueueUDP::close()
{
    connection->close();
}

ConnectionStatus MessageQueueUDP::getStatus() const
{
    return connection->getStatus();
}

float MessageQueueUDP::getLatency() const
{
    return connection->getLatency();
}

size_t MessageQueueUDP::getMaxPacketSize() const
{
    // AckUnreliableConnection can split up packets into up to 16*MTU
    return connection->getMaxUnreliablePacketSize() * 16;
}
