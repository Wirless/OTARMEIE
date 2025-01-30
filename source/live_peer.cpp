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

#include "live_peer.h"
#include "live_server.h"
#include "live_tab.h"
#include "live_action.h"

#include "editor.h"

LivePeer::LivePeer(LiveServer* server, boost::asio::ip::tcp::socket socket) :
	LiveSocket(),
	readMessage(), server(server), socket(std::move(socket)), color(), id(0), clientId(0), connected(false) {
	ASSERT(server != nullptr);
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Peer created, socket open: %d\n",
		getHostName(), socket.is_open()).c_str());
}

LivePeer::~LivePeer() {
	if (socket.is_open()) {
		socket.close();
	}
}

void LivePeer::close() {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Closing connection, socket open: %d\n",
		getHostName(), socket.is_open()).c_str());
		
	if (socket.is_open()) {
		boost::system::error_code ec;
		socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		socket.close(ec);
	}
	
	server->removeClient(id);
}

bool LivePeer::handleError(const boost::system::error_code& error) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Handling error: %s, socket open: %d\n",
		getHostName(), error.message(), socket.is_open()).c_str());
		
	if (error == boost::asio::error::eof || 
		error == boost::asio::error::connection_reset ||
		error == boost::asio::error::operation_aborted) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Connection reset/closed\n",
			getHostName()).c_str());
		close();
		return true;
	}
	return false;
}

std::string LivePeer::getHostName() const {
	return socket.remote_endpoint().address().to_string();
}

void LivePeer::receiveHeader() {
	if (!socket.is_open()) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Socket closed before receiving header\n",
			getHostName()).c_str());
		return;
	}

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Waiting for packet header, socket open: %d\n", 
		getHostName(), socket.is_open()).c_str());
		
	readMessage.position = 0;
	
	try {
		boost::asio::async_read(socket, boost::asio::buffer(readMessage.buffer, 4), 
			[this](const boost::system::error_code& error, size_t bytesReceived) -> void {
				if (!socket.is_open()) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Socket closed during header read\n",
						getHostName()).c_str());
					return;
				}

				if (error) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Header receive error: %s\n",
						getHostName(), error.message()).c_str());
					if (error == boost::asio::error::eof) {
						OutputDebugStringA(wxString::Format("[LivePeer:%s] Client disconnected (EOF)\n",
							getHostName()).c_str());
						close();
					} else if (error == boost::asio::error::operation_aborted) {
						OutputDebugStringA(wxString::Format("[LivePeer:%s] Operation aborted\n",
							getHostName()).c_str());
						// Don't close - might be temporary
						receiveHeader(); // Try again
					} else if (!handleError(error)) {
						logMessage(wxString() + getHostName() + ": " + error.message());
					}
				} else if (bytesReceived < 4) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Incomplete header received: %zu/4 bytes - retrying\n",
						getHostName(), bytesReceived).c_str());
					// Instead of closing, try to receive the rest
					receiveHeader();
				} else {
					uint32_t packetSize = readMessage.read<uint32_t>();
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Header received, packet size: %u\n",
						getHostName(), packetSize).c_str());
						
					if (packetSize == 0 || packetSize > 1024 * 1024) { // 1MB max
						OutputDebugStringA(wxString::Format("[LivePeer:%s] Invalid packet size: %u\n",
							getHostName(), packetSize).c_str());
						close();
						return;
					}
					receive(packetSize);
				}
			}
		);
	} catch (const std::exception& e) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Exception in receiveHeader: %s\n",
			getHostName(), e.what()).c_str());
		close();
	}
}

void LivePeer::receive(uint32_t packetSize) {
	if (!socket.is_open()) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Socket closed before receiving packet\n",
			getHostName()).c_str());
		return;
	}

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Receiving packet of size %u\n",
		getHostName(), packetSize).c_str());
		
	readMessage.buffer.resize(readMessage.position + packetSize);
	try {
		boost::asio::async_read(socket, 
			boost::asio::buffer(&readMessage.buffer[readMessage.position], packetSize),
			[this, packetSize](const boost::system::error_code& error, size_t bytesReceived) -> void {
				if (!socket.is_open()) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Socket closed during packet read\n",
						getHostName()).c_str());
					return;
				}

				if (error) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Receive error: %s\n",
						getHostName(), error.message()).c_str());
					if (error == boost::asio::error::operation_aborted) {
						OutputDebugStringA(wxString::Format("[LivePeer:%s] Operation aborted - retrying\n",
							getHostName()).c_str());
						receive(packetSize); // Try again
					} else if (!handleError(error)) {
						logMessage(wxString() + getHostName() + ": " + error.message());
					}
				} else if (bytesReceived < readMessage.buffer.size() - 4) {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Incomplete packet: %zu/%zu bytes - retrying\n",
						getHostName(), bytesReceived, readMessage.buffer.size() - 4).c_str());
					// Try to receive the rest instead of closing
					receive(packetSize - bytesReceived);
				} else {
					OutputDebugStringA(wxString::Format("[LivePeer:%s] Full packet received (%zu bytes), processing\n",
						getHostName(), bytesReceived).c_str());
					wxTheApp->CallAfter([this]() {
						if (connected) {
							OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing as editor packet\n",
								getHostName()).c_str());
							parseEditorPacket(std::move(readMessage));
						} else {
							OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing as login packet\n",
								getHostName()).c_str());
							parseLoginPacket(std::move(readMessage));
						}
						receiveHeader();
					});
				}
			}
		);
	} catch (const std::exception& e) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Exception in receive: %s\n",
			getHostName(), e.what()).c_str());
		close();
	}
}

void LivePeer::send(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Sending packet size: %zu bytes\n",
		getHostName(), message.getSize()).c_str());

	// Write size at start of buffer
	uint32_t size = message.getSize();
	memcpy(&message.buffer[0], &size, 4);

	// Send entire buffer including size header
	boost::asio::async_write(socket, boost::asio::buffer(message.buffer), 
		[this](const boost::system::error_code& error, size_t bytesTransferred) -> void {
			if(error) {
				OutputDebugStringA(wxString::Format("[LivePeer:%s] Send error: %s\n",
					getHostName(), error.message()).c_str());
				logMessage(wxString() + getHostName() + ": " + error.message());
			} else {
				OutputDebugStringA(wxString::Format("[LivePeer:%s] Successfully sent %zu bytes\n",
					getHostName(), bytesTransferred).c_str());
			}
		}
	);
}

void LivePeer::parseLoginPacket(NetworkMessage message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Parsing login packet, size: %zu\n",
		getHostName(), message.buffer.size()).c_str());
		
	// Only process one packet type per message during login
	if (message.position >= message.buffer.size()) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] No more data in login packet\n",
			getHostName()).c_str());
		return;
	}

	uint8_t packetType = message.read<uint8_t>();
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Login packet type: 0x%02X\n",
		getHostName(), packetType).c_str());
			
	switch(packetType) {
		case PACKET_HELLO_FROM_CLIENT:
			OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing HELLO packet\n",
				getHostName()).c_str());
			parseHello(message);
			break;
		case PACKET_READY_CLIENT:
			OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing READY packet\n",
				getHostName()).c_str());
			parseReady(message);
			break;
		default:
			OutputDebugStringA(wxString::Format("[LivePeer:%s] Invalid login packet type: 0x%02X\n",
				getHostName(), packetType).c_str());
			log->Message("Invalid login packet received, connection severed.");
			close();
			break;
	}
}

void LivePeer::parseEditorPacket(NetworkMessage message) {
	uint8_t packetType;
	while (message.position < message.buffer.size()) {
		packetType = message.read<uint8_t>();
		switch (packetType) {
			case PACKET_REQUEST_NODES:
				parseNodeRequest(message);
				break;
			case PACKET_CHANGE_LIST:
				parseReceiveChanges(message);
				break;
			case PACKET_ADD_HOUSE:
				parseAddHouse(message);
				break;
			case PACKET_EDIT_HOUSE:
				parseEditHouse(message);
				break;
			case PACKET_REMOVE_HOUSE:
				parseRemoveHouse(message);
				break;
			case PACKET_CLIENT_UPDATE_CURSOR:
				parseCursorUpdate(message);
				break;
			case PACKET_CLIENT_TALK:
				parseChatMessage(message);
				break;
			default: {
				log->Message("Invalid editor packet receieved, connection severed.");
				close();
				break;
			}
		}
	}
}

void LivePeer::parseHello(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Parsing HELLO packet\n",
		getHostName()).c_str());
		
	if(connected) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Already connected, closing\n",
			getHostName()).c_str());
		close();
		return;
	}

	uint32_t rmeVersion = message.read<uint32_t>();
	uint32_t netVersion = message.read<uint32_t>();
	uint32_t clientVersion = message.read<uint32_t>();
	std::string nickname = message.read<std::string>();
	std::string password = message.read<std::string>();

	OutputDebugStringA(wxString::Format("[LivePeer:%s] HELLO info - RME: %u, Net: %u, Client: %u, Name: %s\n",
		getHostName(), rmeVersion, netVersion, clientVersion, nickname.c_str()).c_str());

	if (server->getPassword() != wxString(password.c_str(), wxConvUTF8)) {
		log->Message("Client tried to connect, but used the wrong password, connection refused.");
		close();
		return;
	}

	name = wxString(nickname.c_str(), wxConvUTF8);
	log->Message(name + " (" + getHostName() + ") connected.");

	NetworkMessage outMessage;
	if (static_cast<ClientVersionID>(clientVersion) != g_gui.GetCurrentVersionID()) {
		outMessage.write<uint8_t>(PACKET_CHANGE_CLIENT_VERSION);
		outMessage.write<uint32_t>(g_gui.GetCurrentVersionID());
	} else {
		outMessage.write<uint8_t>(PACKET_ACCEPTED_CLIENT);
	}
	send(outMessage);

	OutputDebugStringA(wxString::Format("[LivePeer:%s] HELLO processing complete\n",
		getHostName()).c_str());
}

void LivePeer::parseReady(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing READY packet, connected=%d\n",
		getHostName(), connected).c_str());
		
	if (connected) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Already connected, closing connection\n",
			getHostName()).c_str());
		close();
		return;
	}

	connected = true;
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Connection state set to connected\n",
		getHostName()).c_str());

	// Find free client id
	clientId = server->getFreeClientId();
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Assigned client ID: %u\n",
		getHostName(), clientId).c_str());
		
	if (clientId == 0) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] No free client IDs available\n",
			getHostName()).c_str());
		NetworkMessage outMessage;
		outMessage.write<uint8_t>(PACKET_KICK);
		outMessage.write<std::string>("Server is full.");
		send(outMessage);
		close();
		return;
	}

	server->updateClientList();
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Client list updated\n",
		getHostName()).c_str());

	// Send reply with map info
	NetworkMessage outMessage;
	outMessage.write<uint8_t>(PACKET_HELLO_FROM_SERVER);

	Map& map = server->getEditor()->map;
	std::string mapName = map.getName();
	uint16_t width = map.getWidth();
	uint16_t height = map.getHeight();

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Preparing map info packet - Name: %s Size: %dx%d\n",
		getHostName(), mapName.c_str(), width, height).c_str());

	outMessage.write<std::string>(mapName);
	outMessage.write<uint16_t>(width);
	outMessage.write<uint16_t>(height);

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Sending HELLO_FROM_SERVER packet, size: %zu\n",
		getHostName(), outMessage.getSize()).c_str());

	send(outMessage);
}

void LivePeer::parseNodeRequest(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing node request\n",
		getHostName()).c_str());
		
	Map& map = server->getEditor()->map;
	uint32_t totalNodes = message.read<uint32_t>();
	
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Client requested %u nodes\n",
		getHostName(), totalNodes).c_str());
	
	uint32_t processedNodes = 0;
	uint32_t sentNodes = 0;
	
	for (uint32_t nodes = totalNodes; nodes != 0; --nodes) {
		uint32_t ind = message.read<uint32_t>();
		processedNodes++;

		int32_t ndx = ind >> 18;
		int32_t ndy = (ind >> 4) & 0x3FFF;
		bool underground = ind & 1;

		OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing node request %u/%u at (%d,%d) %s\n",
			getHostName(), processedNodes, totalNodes, ndx * 4, ndy * 4, 
			underground ? "underground" : "above ground").c_str());

		QTreeNode* node = map.createLeaf(ndx * 4, ndy * 4);
		if (node) {
			sendNode(clientId, node, ndx, ndy, underground ? 0xFF00 : 0x00FF);
			sentNodes++;
		} else {
			OutputDebugStringA(wxString::Format("[LivePeer:%s] Failed to create node at (%d,%d)\n",
				getHostName(), ndx * 4, ndy * 4).c_str());
		}
	}
	
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Node request complete - Processed: %u, Sent: %u\n",
		getHostName(), processedNodes, sentNodes).c_str());
}

void LivePeer::parseReceiveChanges(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Parsing received changes\n",
		getHostName()).c_str());
		
	Editor& editor = *server->getEditor();

	// -1 on address since we skip the first START_NODE when sending
	const std::string& data = message.read<std::string>();
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Received change data size: %zu bytes\n",
		getHostName(), data.size()).c_str());
		
	mapReader.assign(reinterpret_cast<const uint8_t*>(data.c_str() - 1), data.size());

	BinaryNode* rootNode = mapReader.getRootNode();
	BinaryNode* tileNode = rootNode->getChild();

	NetworkedAction* action = static_cast<NetworkedAction*>(editor.actionQueue->createAction(ACTION_REMOTE));
	action->owner = clientId;

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing tile changes from client %u\n",
		getHostName(), clientId).c_str());

	int changesCount = 0;
	if (tileNode) {
		do {
			Tile* tile = readTile(tileNode, editor, nullptr);
			if (tile) {
				action->addChange(newd Change(tile));
				changesCount++;
			}
		} while (tileNode->advance());
	}
	mapReader.close();

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processed %d tile changes\n",
		getHostName(), changesCount).c_str());

	editor.actionQueue->addAction(action);
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Added changes to action queue\n",
		getHostName()).c_str());

	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void LivePeer::parseAddHouse(NetworkMessage& message) {
}

void LivePeer::parseEditHouse(NetworkMessage& message) {
}

void LivePeer::parseRemoveHouse(NetworkMessage& message) {
}

void LivePeer::parseCursorUpdate(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing cursor update\n",
		getHostName()).c_str());
		
	LiveCursor cursor = readCursor(message);
	cursor.id = clientId;

	OutputDebugStringA(wxString::Format("[LivePeer:%s] Cursor position: (%d,%d,%d)\n",
		getHostName(), cursor.pos.x, cursor.pos.y, cursor.pos.z).c_str());

	if (cursor.color != color) {
		OutputDebugStringA(wxString::Format("[LivePeer:%s] Client color changed to RGB(%d,%d,%d,%d)\n",
			getHostName(), cursor.color.Red(), cursor.color.Green(), 
			cursor.color.Blue(), cursor.color.Alpha()).c_str());
			
		setUsedColor(cursor.color);
		server->updateClientList();
	}

	server->broadcastCursor(cursor);
	g_gui.RefreshView();
}

void LivePeer::parseChatMessage(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Processing chat message\n",
		getHostName()).c_str());
		
	const std::string& chatMessage = message.read<std::string>();
	OutputDebugStringA(wxString::Format("[LivePeer:%s] Broadcasting chat: %s\n",
		getHostName(), chatMessage.c_str()).c_str());
		
	server->broadcastChat(name, wxstr(chatMessage));
}
