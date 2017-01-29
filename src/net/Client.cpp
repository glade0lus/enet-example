#include "net/Client.h"

#include "Common.h"
#include "log/Log.h"
#include "time/Time.h"

const uint8_t SERVER_ID = 0;
const std::time_t CONNECTION_TIMEOUT_MS = 5000;

Client::Shared Client::alloc() {
	return std::make_shared<Client>();
}

Client::Client()
	: host_(nullptr) {
	// initialize enet
	// TODO: prevent this from being called multiple times
	if (enet_initialize() != 0) {
		LOG_ERROR("An error occurred while initializing ENet");
		return;
	}
	// create a host
	host_ = enet_host_create(
		nullptr, // create a client host
		1, // only allow 1 outgoing connection
		NUM_CHANNELS, // allow up TO N channels to be used
		0, // assume any amount of incoming bandwidth
		0); // assume any amount of outgoing bandwidth
	// check if creation was successful
	// NOTE: this only fails if malloc fails inside `enet_host_create`
	if (host_ == nullptr) {
		LOG_ERROR("An error occurred while trying to create an ENet client host");
	}
}

Client::~Client() {
	// disconnect if haven't already
	disconnect();
	// destroy the host
	// NOTE: safe to call when host is nullptr
	enet_host_destroy(host_);
	host_ = nullptr;
	// TODO: prevent this from being called multiple times
	enet_deinitialize();
}

bool Client::connect(const std::string& host, uint32_t port) {
	if (isConnected()) {
		LOG_DEBUG("Client is already connected to a server");
		return 0;
	}
	// set address to connect to
	ENetAddress address;
	enet_address_set_host(&address, host.c_str());
	address.port = port;
	// initiate the connection, allocating the two channels 0 and 1.
	server_ = enet_host_connect(host_, &address, NUM_CHANNELS, 0);
	if (server_ == nullptr) {
		LOG_ERROR("No available peers for initiating an ENet connection");
		return 1;
	}
	// attempt to connect to the peer (server)
	ENetEvent event;
	// wait N for connection to succeed
	// NOTE: we don't need to check / destroy packets because the server will be
	// unable to send the packets without first establishing a connection
	if (enet_host_service(host_, &event, CONNECTION_TIMEOUT_MS) > 0 &&
		event.type == ENET_EVENT_TYPE_CONNECT) {
		// connection successful
		LOG_DEBUG("Connection to `" << host << ":" << port << "` succeeded");
		return 0;
	}
	// failure to connect
	LOG_ERROR("Connection to `" << host << ":" << port << "` failed");
	enet_peer_reset(server_);
	server_ = nullptr;
	return 1;
}

bool Client::disconnect() {
	if (!isConnected()) {
		return 0;
	}
	LOG_DEBUG("Disconnecting from server...");
	// attempt to gracefully disconnect
	enet_peer_disconnect(server_, 0);
	// wait for the disconnect to be acknowledged
	ENetEvent event;
	auto timestamp = Time::timestamp();
	bool success = false;
	while (true) {
		int32_t res = enet_host_service(host_, &event, 0);
		if (res > 0) {
			// event occured
			if (event.type == ENET_EVENT_TYPE_RECEIVE) {
				// throw away any received packets during disconnect
				enet_packet_destroy(event.packet);
			} else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
				// disconnect successful
				LOG_DEBUG("Disconnection from server successful");
				success = true;
				break;
			}
		} else if (res < 0) {
			// error occured
			LOG_ERROR("Encountered error while polling");
			break;
		}
		// check for timeout
		if (Time::timestamp() - timestamp > Time::fromMilliseconds(CONNECTION_TIMEOUT_MS)) {
			break;
		}
	}
	if (!success) {
		// disconnect attempt didn't succeed yet, force close the connection
		LOG_ERROR("Disconnection was not acknowledged by server, shutdown forced");
		enet_peer_reset(server_);
	}
	server_ = nullptr;
	return !success;
}

bool Client::isConnected() const {
	return host_->connectedPeers > 0;
}

void Client::send(DeliveryType type, const std::vector<uint8_t>& data) const {
	if (!isConnected()) {
		LOG_DEBUG("Client is not connected to any server");
		return;
	}
	uint32_t channel = 0;
	uint32_t flags = 0;
	if (type == DeliveryType::RELIABLE) {
		channel = RELIABLE_CHANNEL;
		flags = ENET_PACKET_FLAG_RELIABLE;
	} else {
		channel = UNRELIABLE_CHANNEL;
		flags = ENET_PACKET_FLAG_UNSEQUENCED;
	}
	// create the packet
	ENetPacket* p = enet_packet_create(
		&data[0],
		data.size(),
		flags);
	// send the packet to the peer
	enet_peer_send(server_, channel, p);
	// flush / send the packet queue
	enet_host_flush(host_);
}

std::vector<Message::Shared> Client::poll() {
	std::vector<Message::Shared> msgs;
	if (!isConnected()) {
		LOG_DEBUG("Client is not connected to any server");
		return msgs;
	}
	ENetEvent event;
	while (true) {
		// poll with a zero timeout
		int32_t res = enet_host_service(host_, &event, 0);
		if (res > 0) {
			// event occured
			if (event.type == ENET_EVENT_TYPE_RECEIVE) {
				// received a packet
				// LOG_DEBUG("A packet of length "
				// 	<< event.packet->dataLength
				// 	<< " containing `"
				// 	<< event.packet->data
				// 	<< "` was received on channel "
				// 	<< event.channelID);
				auto msg = Message::alloc(
					SERVER_ID,
					MessageType::DATA,
					StreamBuffer::alloc(
						event.packet->data,
						event.packet->dataLength));
				msgs.push_back(msg);
				// destroy packet payload
				enet_packet_destroy(event.packet);

			} else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
				// server disconnected
				LOG_DEBUG("Connection to server has been lost");
				auto msg = Message::alloc(
					SERVER_ID,
					MessageType::DISCONNECT);
				msgs.push_back(msg);
				server_ = nullptr;
			}
		} else if (res < 0) {
			// error occured
			LOG_ERROR("Encountered error while polling");
			break;
		} else {
			// no event
			break;
		}
	}
	return msgs;
}
