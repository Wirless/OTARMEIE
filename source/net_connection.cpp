//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "net_connection.h"

/*
RME Server-Client Split Roadmap:

1. Server Project (New Linux-based project)
   - Core Components:
     * OTBM file loader (port from RME)
     * SPR/DAT loaders
     * items.otb handler
     * Network message handler
     * Live editing protocol
     * Client session management
     * Map change broadcasting
     * Undo/Redo synchronization
   
   - Required Features:
     * Headless operation (no GUI)
     * Multi-client support
     * File change monitoring
     * Backup system
     * Change validation
     * Permission system
     * Map locking mechanism
     * Auto-save functionality

2. RME Client Modifications:
   - Remove:
     * LiveServer class
     * Server-side packet handlers
     * Host/server creation UI
     * Local map saving (server handles this)
   
   - Modify:
     * NetworkMessage class (client-only packets)
     * LiveClient to handle all server communication
     * Editor class to work in client-only mode
     * GUI to reflect client-only status
     * ActionQueue to forward all changes to server

3. Protocol Updates:
   - New Packets:
     * Client authentication
     * Map region requests
     * Change proposals
     * Lock requests
     * Client synchronization
     * Server commands
   
   - Security:
     * Implement proper encryption
     * Add checksum validation
     * Session management
     * Anti-tampering measures

4. Data Flow:
   Server:                    Client:
   [OTBM] <-> [Server] <---> [RME Client] <-> [GUI]
   [items.otb]    ^                ^
   [spr/dat]      |                |
                  +----------------+
                  Network Protocol

Implementation Priority:
1. Create basic server project structure
2. Port core file handlers to server
3. Implement basic client-server protocol
4. Remove server code from RME
5. Add security features
6. Implement advanced features
7. Testing and optimization

Note: This requires significant restructuring of the RME codebase
and careful handling of backwards compatibility for offline editing.
*/

NetworkMessage::NetworkMessage() {
	clear();
}

void NetworkMessage::clear() {
	buffer.resize(4);
	position = 4;
	size = 0;
}

void NetworkMessage::expand(const size_t length) {
	if (position + length >= buffer.size()) {
		buffer.resize(position + length + 1);
	}
	size += length;
}

template <>
std::string NetworkMessage::read<std::string>() {
	const uint16_t length = read<uint16_t>();
	char* strBuffer = reinterpret_cast<char*>(&buffer[position]);
	position += length;
	return std::string(strBuffer, length);
}

template <>
Position NetworkMessage::read<Position>() {
	Position position;
	position.x = read<uint16_t>();
	position.y = read<uint16_t>();
	position.z = read<uint8_t>();
	return position;
}

template <>
void NetworkMessage::write<std::string>(const std::string& value) {
	const size_t length = value.length();
	write<uint16_t>(length);

	expand(length);
	memcpy(&buffer[position], &value[0], length);
	position += length;
}

template <>
void NetworkMessage::write<Position>(const Position& value) {
	write<uint16_t>(value.x);
	write<uint16_t>(value.y);
	write<uint8_t>(value.z);
}

// NetworkConnection
NetworkConnection::NetworkConnection() :
	service(nullptr), thread(), stopped(false) {
	//
}

NetworkConnection::~NetworkConnection() {
	stop();
}

NetworkConnection& NetworkConnection::getInstance() {
	static NetworkConnection connection;
	return connection;
}

bool NetworkConnection::start() {
	if (thread.joinable()) {
		if (stopped) {
			return false;
		}
		return true;
	}

	stopped = false;
	if (!service) {
		service = new boost::asio::io_service;
	}

	thread = std::thread([this]() -> void {
		boost::asio::io_service& serviceRef = *service;
		try {
			while (!stopped) {
				serviceRef.run_one();
				serviceRef.reset();
			}
		} catch (std::exception& e) {
			std::cout << e.what() << std::endl;
		}
	});
	return true;
}

void NetworkConnection::stop() {
	if (!service) {
		return;
	}

	service->stop();
	stopped = true;
	thread.join();

	delete service;
	service = nullptr;
}

boost::asio::io_service& NetworkConnection::get_service() {
	return *service;
}
