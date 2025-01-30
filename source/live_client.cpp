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

#include "live_client.h"
#include "live_tab.h"
#include "live_action.h"
#include "editor.h"

#include <wx/event.h>

LiveClient::LiveClient() :
	LiveSocket(),
	readMessage(), queryNodeList(), currentOperation(),
	resolver(nullptr), socket(nullptr), editor(nullptr), stopped(false) {
	//
}

LiveClient::~LiveClient() {
	//
}

bool LiveClient::connect(const std::string& address, uint16_t port) {
	OutputDebugStringA(wxString::Format("[LiveClient::connect] Attempting to connect to %s:%d\n", 
		address.c_str(), port).c_str());

	NetworkConnection& connection = NetworkConnection::getInstance();
	if (!connection.start()) {
		wxString error = "The previous connection has not been terminated yet.";
		OutputDebugStringA(wxString::Format("[LiveClient::connect] Error: %s\n", error).c_str());
		setLastError(error);
		return false;
	}

	auto& service = connection.get_service();
	if (!resolver) {
		OutputDebugStringA("[LiveClient::connect] Creating new resolver\n");
		resolver = std::make_shared<boost::asio::ip::tcp::resolver>(service);
	}

	if (!socket) {
		OutputDebugStringA("[LiveClient::connect] Creating new socket\n");
		socket = std::make_shared<boost::asio::ip::tcp::socket>(service);
	}

	boost::asio::ip::tcp::resolver::query query(address, std::to_string(port));
	OutputDebugStringA("[LiveClient::connect] Starting async resolve\n");
	
	resolver->async_resolve(query, [this](const boost::system::error_code& error, 
		boost::asio::ip::tcp::resolver::iterator endpoint_iterator) -> void {
		if (error) {
			OutputDebugStringA(wxString::Format("[LiveClient::connect] Resolve error: %s\n", 
				error.message()).c_str());
			logMessage("Error: " + error.message());
		} else {
			OutputDebugStringA("[LiveClient::connect] Resolve successful, attempting connection\n");
			tryConnect(endpoint_iterator);
		}
	});

	return true;
}

void LiveClient::tryConnect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
	if (stopped) {
		OutputDebugStringA("[LiveClient::tryConnect] Connection stopped\n");
		return;
	}

	if (endpoint_iterator == boost::asio::ip::tcp::resolver::iterator()) {
		OutputDebugStringA("[LiveClient::tryConnect] No more endpoints to try\n");
		return;
	}

	OutputDebugStringA(wxString::Format("[LiveClient::tryConnect] Trying endpoint %s:%s\n",
		endpoint_iterator->host_name().c_str(),
		endpoint_iterator->service_name().c_str()).c_str());

	logMessage("Joining server " + endpoint_iterator->host_name() + ":" + 
		endpoint_iterator->service_name() + "...");

	boost::asio::async_connect(*socket, endpoint_iterator, 
		[this](boost::system::error_code error, 
			boost::asio::ip::tcp::resolver::iterator endpoint_iterator) -> void {
		if (!socket->is_open()) {
			OutputDebugStringA("[LiveClient::tryConnect] Socket not open, trying next endpoint\n");
			tryConnect(++endpoint_iterator);
		} else if (error) {
			OutputDebugStringA(wxString::Format("[LiveClient::tryConnect] Connection error: %s\n",
				error.message()).c_str());
			
			if (handleError(error)) {
				OutputDebugStringA("[LiveClient::tryConnect] Recoverable error, trying next endpoint\n");
				tryConnect(++endpoint_iterator);
			} else {
				OutputDebugStringA("[LiveClient::tryConnect] Unrecoverable error, closing connection\n");
				wxTheApp->CallAfter([this]() {
					close();
					g_gui.CloseLiveEditors(this);
				});
			}
		} else {
			OutputDebugStringA("[LiveClient::tryConnect] Connection successful, setting up socket\n");
			
			socket->set_option(boost::asio::ip::tcp::no_delay(true), error);
			if (error) {
				OutputDebugStringA(wxString::Format("[LiveClient::tryConnect] Failed to set TCP_NODELAY: %s\n",
					error.message()).c_str());
				wxTheApp->CallAfter([this]() {
					close();
				});
				return;
			}

			OutputDebugStringA("[LiveClient::tryConnect] Sending hello packet\n");
			sendHello();
			receiveHeader();
		}
	});
}

void LiveClient::close() {
	if (resolver) {
		resolver->cancel();
	}

	if (socket) {
		socket->close();
	}

	if (log) {
		log->Message("Disconnected from server.");
		log->Disconnect();
		log = nullptr;
	}

	stopped = true;
}

bool LiveClient::handleError(const boost::system::error_code& error) {
	if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) {
		wxTheApp->CallAfter([this]() {
			log->Message(wxString() + getHostName() + ": disconnected.");
			close();
		});
		return true;
	} else if (error == boost::asio::error::connection_aborted) {
		logMessage("You have left the server.");
		return true;
	}
	return false;
}

std::string LiveClient::getHostName() const {
	if (!socket) {
		return "not connected";
	}
	return socket->remote_endpoint().address().to_string();
}

void LiveClient::receiveHeader() {
	OutputDebugStringA("[LiveClient::receiveHeader] Waiting for packet header\n");
	
	readMessage.position = 0;
	boost::asio::async_read(*socket, boost::asio::buffer(readMessage.buffer, 4),
		[this](const boost::system::error_code& error, size_t bytesReceived) -> void {
			if (error) {
				OutputDebugStringA(wxString::Format("[LiveClient::receiveHeader] Error: %s\n",
					error.message()).c_str());
				if (!handleError(error)) {
					logMessage(wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < 4) {
				OutputDebugStringA(wxString::Format("[LiveClient::receiveHeader] Incomplete header: %zu/4 bytes\n",
					bytesReceived).c_str());
				logMessage(wxString() + getHostName() + ": Could not receive header[size: " + 
					std::to_string(bytesReceived) + "], disconnecting client.");
			} else {
				uint32_t packetSize = readMessage.read<uint32_t>();
				OutputDebugStringA(wxString::Format("[LiveClient::receiveHeader] Header received, expecting packet size: %u\n",
					packetSize).c_str());
					
				if (packetSize == 0 || packetSize > 1024 * 1024) { // 1MB max
					OutputDebugStringA(wxString::Format("[LiveClient::receiveHeader] Invalid packet size: %u\n",
						packetSize).c_str());
					close();
					return;
				}
				receive(packetSize);
			}
		}
	);
}

void LiveClient::receive(uint32_t packetSize) {
	OutputDebugStringA(wxString::Format("[LiveClient::receive] Receiving packet of size %u\n", 
		packetSize).c_str());
	readMessage.buffer.resize(readMessage.position + packetSize);
	boost::asio::async_read(*socket, 
		boost::asio::buffer(&readMessage.buffer[readMessage.position], packetSize),
		[this](const boost::system::error_code& error, size_t bytesReceived) -> void {
			if (error) {
				OutputDebugStringA(wxString::Format("[LiveClient::receive] Receive error: %s\n",
					error.message()).c_str());
				if (!handleError(error)) {
					logMessage(wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < readMessage.buffer.size() - 4) {
				OutputDebugStringA(wxString::Format("[LiveClient::receive] Incomplete packet: %zu/%zu bytes\n",
					bytesReceived, readMessage.buffer.size() - 4).c_str());
				logMessage(wxString() + getHostName() + ": Could not receive packet[size: " + 
					std::to_string(bytesReceived) + "], disconnecting client.");
			} else {
				OutputDebugStringA(wxString::Format("[LiveClient::receive] Full packet received (%zu bytes)\n",
					bytesReceived).c_str());
				wxTheApp->CallAfter([this]() {
					parsePacket(std::move(readMessage));
					receiveHeader();
				});
			}
		}
	);
}

void LiveClient::send(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LiveClient::send] Sending packet size: %zu bytes\n", 
		message.getSize()).c_str());

	// Write size at start of buffer
	uint32_t size = message.getSize();
	memcpy(&message.buffer[0], &size, 4);

	// Send entire buffer including size header
	boost::asio::async_write(*socket, boost::asio::buffer(message.buffer),
		[this](const boost::system::error_code& error, size_t bytesTransferred) -> void {
			if (error) {
				OutputDebugStringA(wxString::Format("[LiveClient::send] Send error: %s\n",
					error.message()).c_str());
				logMessage(wxString() + getHostName() + ": " + error.message());
			} else {
				OutputDebugStringA(wxString::Format("[LiveClient::send] Successfully sent %zu bytes\n",
					bytesTransferred).c_str());
			}
		}
	);
}

void LiveClient::updateCursor(const Position& position) {
	LiveCursor cursor;
	cursor.id = 77; // Unimportant, server fixes it for us
	cursor.pos = position;
	cursor.color = wxColor(
		g_settings.getInteger(Config::CURSOR_RED),
		g_settings.getInteger(Config::CURSOR_GREEN),
		g_settings.getInteger(Config::CURSOR_BLUE),
		g_settings.getInteger(Config::CURSOR_ALPHA)
	);

	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_UPDATE_CURSOR);
	writeCursor(message, cursor);

	send(message);
}

LiveLogTab* LiveClient::createLogWindow(wxWindow* parent) {
	MapTabbook* mtb = dynamic_cast<MapTabbook*>(parent);
	ASSERT(mtb);

	log = newd LiveLogTab(mtb, this);
	log->Message("New Live mapping session started.");

	return log;
}

MapTab* LiveClient::createEditorWindow() {
	MapTabbook* mtb = dynamic_cast<MapTabbook*>(g_gui.tabbook);
	ASSERT(mtb);

	MapTab* edit = newd MapTab(mtb, editor);
	edit->OnSwitchEditorMode(g_gui.IsSelectionMode() ? SELECTION_MODE : DRAWING_MODE);

	return edit;
}

void LiveClient::sendHello() {
	OutputDebugStringA("[LiveClient::sendHello] Preparing hello packet\n");
	
	NetworkMessage message;
	message.write<uint8_t>(PACKET_HELLO_FROM_CLIENT);
	message.write<uint32_t>(__RME_VERSION_ID__);
	message.write<uint32_t>(__LIVE_NET_VERSION__);
	message.write<uint32_t>(g_gui.GetCurrentVersionID());
	message.write<std::string>(nstr(name));
	message.write<std::string>(nstr(password));

	OutputDebugStringA(wxString::Format("[LiveClient::sendHello] Sending hello packet: "
		"Version: %d, NetVersion: %d, ClientVersion: %d, Name: %s\n",
		__RME_VERSION_ID__, __LIVE_NET_VERSION__, g_gui.GetCurrentVersionID(), 
		name).c_str());

	send(message);
}

void LiveClient::sendNodeRequests() {
	if (queryNodeList.empty()) {
		return;
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_REQUEST_NODES);

	message.write<uint32_t>(queryNodeList.size());
	for (uint32_t node : queryNodeList) {
		message.write<uint32_t>(node);
	}

	send(message);
	queryNodeList.clear();
}

void LiveClient::sendChanges(DirtyList& dirtyList) {
	ChangeList& changeList = dirtyList.GetChanges();
	if (changeList.empty()) {
		return;
	}

	mapWriter.reset();
	for (Change* change : changeList) {
		switch (change->getType()) {
			case CHANGE_TILE: {
				const Position& position = static_cast<Tile*>(change->getData())->getPosition();
				sendTile(mapWriter, editor->map.getTile(position), &position);
				break;
			}
			default:
				break;
		}
	}
	mapWriter.endNode();

	NetworkMessage message;
	message.write<uint8_t>(PACKET_CHANGE_LIST);

	std::string data(reinterpret_cast<const char*>(mapWriter.getMemory()), mapWriter.getSize());
	message.write<std::string>(data);

	send(message);
}

void LiveClient::sendChat(const wxString& chatMessage) {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_TALK);
	message.write<std::string>(nstr(chatMessage));
	send(message);
}

void LiveClient::sendReady() {
	OutputDebugStringA("[LiveClient::sendReady] Preparing READY packet\n");
	NetworkMessage message;
	message.write<uint8_t>(PACKET_READY_CLIENT);
	OutputDebugStringA("[LiveClient::sendReady] Sending READY packet\n");
	send(message);
}

void LiveClient::queryNode(int32_t ndx, int32_t ndy, bool underground) {
	uint32_t nd = 0;
	nd |= ((ndx >> 2) << 18);
	nd |= ((ndy >> 2) << 4);
	nd |= (underground ? 1 : 0);
	queryNodeList.insert(nd);
}

void LiveClient::parsePacket(NetworkMessage message) {
	OutputDebugStringA("[LiveClient::parsePacket] Starting to parse packet\n");
	
	uint8_t packetType;
	while (message.position < message.buffer.size()) {
		packetType = message.read<uint8_t>();
		OutputDebugStringA(wxString::Format("[LiveClient::parsePacket] Parsing packet type: 0x%02X\n", 
			packetType).c_str());
			
		switch (packetType) {
			case PACKET_HELLO_FROM_SERVER:
				OutputDebugStringA("[LiveClient::parsePacket] Handling HELLO_FROM_SERVER\n");
				parseHello(message);
				break;
			case PACKET_KICK:
				OutputDebugStringA("[LiveClient::parsePacket] Handling KICK\n");
				parseKick(message);
				break;
			case PACKET_ACCEPTED_CLIENT:
				OutputDebugStringA("[LiveClient::parsePacket] Handling ACCEPTED_CLIENT\n");
				parseClientAccepted(message);
				break;
			case PACKET_CHANGE_CLIENT_VERSION:
				OutputDebugStringA("[LiveClient::parsePacket] Handling CHANGE_CLIENT_VERSION\n");
				parseChangeClientVersion(message);
				break;
			case PACKET_SERVER_TALK:
				OutputDebugStringA("[LiveClient::parsePacket] Handling SERVER_TALK\n");
				parseServerTalk(message);
				break;
			case PACKET_NODE:
				OutputDebugStringA("[LiveClient::parsePacket] Handling NODE\n");
				parseNode(message);
				break;
			case PACKET_CURSOR_UPDATE:
				OutputDebugStringA("[LiveClient::parsePacket] Handling CURSOR_UPDATE\n");
				parseCursorUpdate(message);
				break;
			case PACKET_START_OPERATION:
				OutputDebugStringA("[LiveClient::parsePacket] Handling START_OPERATION\n");
				parseStartOperation(message);
				break;
			case PACKET_UPDATE_OPERATION:
				OutputDebugStringA("[LiveClient::parsePacket] Handling UPDATE_OPERATION\n");
				parseUpdateOperation(message);
				break;
			default:
				OutputDebugStringA(wxString::Format("[LiveClient::parsePacket] Unknown packet type: 0x%02X\n", 
					packetType).c_str());
				log->Message("Unknown packet received!");
				close();
				break;
		}
	}
	OutputDebugStringA("[LiveClient::parsePacket] Finished parsing packet\n");
}

void LiveClient::parseHello(NetworkMessage& message) {
	OutputDebugStringA("[LiveClient::parseHello] Parsing server hello\n");
	
	ASSERT(editor == nullptr);
	editor = newd Editor(g_gui.copybuffer, this);

	Map& map = editor->map;
	std::string mapName = message.read<std::string>();
	uint16_t width = message.read<uint16_t>();
	uint16_t height = message.read<uint16_t>();
	
	OutputDebugStringA(wxString::Format("[LiveClient::parseHello] Map info - Name: %s, Size: %dx%d\n",
		mapName.c_str(), width, height).c_str());
		
	map.setName("Live Map - " + mapName);
	map.setWidth(width);
	map.setHeight(height);

	createEditorWindow();
	OutputDebugStringA("[LiveClient::parseHello] Editor window created\n");
}

void LiveClient::parseKick(NetworkMessage& message) {
	const std::string& kickMessage = message.read<std::string>();
	OutputDebugStringA(wxString::Format("[LiveClient::parseKick] Kicked from server: %s\n",
		kickMessage.c_str()).c_str());
		
	close();
	g_gui.PopupDialog("Disconnected", wxstr(kickMessage), wxOK);
}

void LiveClient::parseClientAccepted(NetworkMessage& message) {
	OutputDebugStringA(wxString::Format("[LiveClient::parseClientAccepted] Client accepted by server\n",
		getHostName()).c_str());
	
	// Make sure socket is still open
	if (!socket || !socket->is_open()) {
		OutputDebugStringA("[LiveClient::parseClientAccepted] Socket closed before sending READY\n");
		close();
		return;
	}
	
	OutputDebugStringA("[LiveClient::parseClientAccepted] Sending READY packet\n");
	
	try {
		NetworkMessage readyMsg;
		readyMsg.write<uint8_t>(PACKET_READY_CLIENT);
		send(readyMsg);
		
		OutputDebugStringA("[LiveClient::parseClientAccepted] READY packet sent\n");
	} catch (const std::exception& e) {
		OutputDebugStringA(wxString::Format("[LiveClient::parseClientAccepted] Exception sending READY: %s\n",
			e.what()).c_str());
		close();
	}
}

void LiveClient::parseChangeClientVersion(NetworkMessage& message) {
	ClientVersionID clientVersion = static_cast<ClientVersionID>(message.read<uint32_t>());
	if (!g_gui.CloseAllEditors()) {
		close();
		return;
	}

	wxString error;
	wxArrayString warnings;
	g_gui.LoadVersion(clientVersion, error, warnings);

	sendReady();
}

void LiveClient::parseServerTalk(NetworkMessage& message) {
	const std::string& speaker = message.read<std::string>();
	const std::string& chatMessage = message.read<std::string>();
	log->Chat(
		wxstr(speaker),
		wxstr(chatMessage)
	);
}

void LiveClient::parseNode(NetworkMessage& message) {
	uint32_t ind = message.read<uint32_t>();

	// Extract node position
	int32_t ndx = ind >> 18;
	int32_t ndy = (ind >> 4) & 0x3FFF;
	bool underground = ind & 1;

	Action* action = editor->actionQueue->createAction(ACTION_REMOTE);
	receiveNode(message, *editor, action, ndx, ndy, underground);
	editor->actionQueue->addAction(action);

	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void LiveClient::parseCursorUpdate(NetworkMessage& message) {
	LiveCursor cursor = readCursor(message);
	cursors[cursor.id] = cursor;

	g_gui.RefreshView();
}

void LiveClient::parseStartOperation(NetworkMessage& message) {
	const std::string& operation = message.read<std::string>();

	currentOperation = wxstr(operation);
	g_gui.SetStatusText("Server Operation in Progress: " + currentOperation + "... (0%)");
}

void LiveClient::parseUpdateOperation(NetworkMessage& message) {
	int32_t percent = message.read<uint32_t>();
	if (percent >= 100) {
		g_gui.SetStatusText("Server Operation Finished.");
	} else {
		g_gui.SetStatusText("Server Operation in Progress: " + currentOperation + "... (" + std::to_string(percent) + "%)");
	}
}
