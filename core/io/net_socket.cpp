#include "net_socket.h"

#include "core/io/variant_parser.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET mex_socket_t;
typedef int mex_socklen_t;
#define MEX_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int mex_socket_t;
typedef socklen_t mex_socklen_t;
#define MEX_INVALID_SOCKET (-1)
#endif

namespace {

const size_t MAX_VAR_SIZE = 4 * 1024 * 1024; // une valeur plus grosse est refusée (protection)
const size_t MAX_BUFFERED = 16 * 1024 * 1024;

void net_init() {
#ifdef _WIN32
	static bool done = false;
	if (!done) {
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
		done = true;
	}
#endif
}

void net_close(intptr_t p_sock) {
	if (p_sock == -1) {
		return;
	}
#ifdef _WIN32
	closesocket((mex_socket_t)p_sock);
#else
	::close((mex_socket_t)p_sock);
#endif
}

void net_set_non_blocking(mex_socket_t p_sock) {
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket(p_sock, FIONBIO, &mode);
#else
	fcntl(p_sock, F_SETFL, fcntl(p_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
#ifdef SO_NOSIGPIPE
	// macOS : écrire sur une connexion fermée ne doit pas arrêter le programme (signal SIGPIPE).
	const int no_signal = 1;
	setsockopt(p_sock, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&no_signal, sizeof(no_signal));
#endif
}

bool net_would_block() {
#ifdef _WIN32
	const int e = WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

// Nom d'hôte ou adresse -> adresse IPv4 (ordre réseau). « * » : toutes les interfaces.
bool net_resolve(const String &p_host, uint32_t &r_address) {
	net_init();
	if (p_host.empty() || p_host == "*") {
		r_address = htonl(INADDR_ANY);
		return true;
	}
	addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *result = nullptr;
	if (getaddrinfo(p_host.c_str(), nullptr, &hints, &result) != 0 || !result) {
		return false;
	}
	r_address = ((sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(result);
	return true;
}

sockaddr_in net_address(uint32_t p_address, int p_port) {
	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = p_address;
	address.sin_port = htons((uint16_t)p_port);
	return address;
}

String net_ip_string(const sockaddr_in &p_address) {
	const uint32_t a = ntohl(p_address.sin_addr.s_addr);
	return vformat("%d.%d.%d.%d", (int)((a >> 24) & 255), (int)((a >> 16) & 255), (int)((a >> 8) & 255), (int)(a & 255));
}

// 0 : rien, 1 : prêt, -1 : erreur.
int net_select(mex_socket_t p_sock, bool p_write) {
	fd_set set, errors;
	FD_ZERO(&set);
	FD_ZERO(&errors);
	FD_SET(p_sock, &set);
	FD_SET(p_sock, &errors);
	timeval timeout = { 0, 0 };
	const int r = select((int)p_sock + 1, p_write ? nullptr : &set, p_write ? &set : nullptr, &errors, &timeout);
	if (r < 0 || FD_ISSET(p_sock, &errors)) {
		return -1;
	}
	return r > 0 && FD_ISSET(p_sock, &set) ? 1 : 0;
}

int net_local_port(mex_socket_t p_sock) {
	sockaddr_in address;
	mex_socklen_t length = sizeof(address);
	if (getsockname(p_sock, (sockaddr *)&address, &length) != 0) {
		return 0;
	}
	return ntohs(address.sin_port);
}

// Valeur -> « longueur sur 4 octets + texte ».
String encode_var(const Variant &p_value) {
	const String text = VariantWriter::write(p_value);
	const uint32_t size = (uint32_t)text.size();
	String out;
	out += (char)(size & 255);
	out += (char)((size >> 8) & 255);
	out += (char)((size >> 16) & 255);
	out += (char)((size >> 24) & 255);
	return out + text;
}

uint32_t read_size(const String &p_data) {
	return (uint32_t)(uint8_t)p_data[0] | ((uint32_t)(uint8_t)p_data[1] << 8) | ((uint32_t)(uint8_t)p_data[2] << 16) | ((uint32_t)(uint8_t)p_data[3] << 24);
}

Variant decode_text(const String &p_text) {
	// Les objets et ressources ne sont jamais reconstruits : seules des données voyagent.
	VariantParser::Stream stream(p_text);
	Variant value;
	String error;
	if (VariantParser::parse_value(stream, value, error) != OK) {
		return Variant();
	}
	return value.get_type() == Variant::OBJECT ? Variant() : value;
}

} // namespace

// ---------------------------------------------------------------------------
// StreamPeerTCP
// ---------------------------------------------------------------------------
StreamPeerTCP::~StreamPeerTCP() {
	net_close(sock);
}

Error StreamPeerTCP::connect_to_host(const String &p_host, int p_port) {
	disconnect_from_host();
	uint32_t address = 0;
	if (p_port <= 0 || p_port > 65535 || !net_resolve(p_host == "*" ? String("127.0.0.1") : p_host, address)) {
		status = STATUS_ERROR;
		return ERR_CANT_CONNECT;
	}
	const mex_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == MEX_INVALID_SOCKET) {
		status = STATUS_ERROR;
		return ERR_CANT_CONNECT;
	}
	net_set_non_blocking(s);
	const sockaddr_in target = net_address(address, p_port);
	if (::connect(s, (const sockaddr *)&target, sizeof(target)) != 0 && !net_would_block()) {
		net_close((intptr_t)s);
		status = STATUS_ERROR;
		return ERR_CANT_CONNECT;
	}
	sock = (intptr_t)s;
	status = STATUS_CONNECTING;
	peer_host = net_ip_string(target);
	peer_port = p_port;
	connect_start = OS::get_singleton()->get_ticks_msec();
	poll();
	return OK;
}

void StreamPeerTCP::disconnect_from_host() {
	net_close(sock);
	sock = -1;
	status = STATUS_NONE;
	incoming.clear();
	peer_host.clear();
	peer_port = 0;
}

void StreamPeerTCP::set_no_delay(bool p_enabled) {
	if (sock != -1) {
		const int flag = p_enabled ? 1 : 0;
		setsockopt((mex_socket_t)sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
	}
}

void StreamPeerTCP::poll() {
	if (sock == -1) {
		return;
	}
	if (status == STATUS_CONNECTING) {
		const int ready = net_select((mex_socket_t)sock, true);
		int error = 0;
		mex_socklen_t length = sizeof(error);
		getsockopt((mex_socket_t)sock, SOL_SOCKET, SO_ERROR, (char *)&error, &length);
		if (ready < 0 || error != 0 || OS::get_singleton()->get_ticks_msec() - connect_start > 30000) {
			net_close(sock);
			sock = -1;
			status = STATUS_ERROR;
		} else if (ready > 0) {
			status = STATUS_CONNECTED;
		}
	}
	if (status == STATUS_CONNECTED) {
		_read_available();
	}
}

void StreamPeerTCP::_read_available() {
	char buffer[4096];
	while (sock != -1 && incoming.size() < MAX_BUFFERED) {
		const int received = (int)recv((mex_socket_t)sock, buffer, sizeof(buffer), 0);
		if (received > 0) {
			incoming.append(buffer, (size_t)received);
			continue;
		}
		if (received == 0 || !net_would_block()) {
			// Connexion fermée par l'autre côté : ce qui a déjà été reçu reste lisible.
			net_close(sock);
			sock = -1;
			status = STATUS_NONE;
		}
		break;
	}
}

Error StreamPeerTCP::put_data(const String &p_data) {
	poll();
	if (status != STATUS_CONNECTED) {
		return ERR_UNCONFIGURED;
	}
	size_t sent = 0;
	const uint64_t start = OS::get_singleton()->get_ticks_msec();
	while (sent < p_data.size()) {
#ifdef MSG_NOSIGNAL
		const int flags = MSG_NOSIGNAL; // pas de signal SIGPIPE si l'autre côté a fermé
#else
		const int flags = 0;
#endif
		const int r = (int)send((mex_socket_t)sock, p_data.data() + sent, (int)std::min(p_data.size() - sent, (size_t)65536), flags);
		if (r > 0) {
			sent += (size_t)r;
			continue;
		}
		if (net_would_block() && OS::get_singleton()->get_ticks_msec() - start < 5000) {
			// Tampon d'envoi plein : courte attente que le système le vide.
			fd_set set;
			FD_ZERO(&set);
			FD_SET((mex_socket_t)sock, &set);
			timeval timeout = { 0, 20000 };
			select((int)sock + 1, nullptr, &set, nullptr, &timeout);
			continue;
		}
		net_close(sock);
		sock = -1;
		status = STATUS_ERROR;
		return ERR_CONNECTION_ERROR;
	}
	return OK;
}

int StreamPeerTCP::get_available_bytes() {
	poll();
	return (int)incoming.size();
}

String StreamPeerTCP::get_data(int p_bytes) {
	poll();
	const size_t count = p_bytes < 0 ? incoming.size() : std::min((size_t)p_bytes, incoming.size());
	const String out = incoming.substr(0, count);
	incoming.erase(0, count);
	return out;
}

Error StreamPeerTCP::put_var(const Variant &p_value) {
	return put_data(encode_var(p_value));
}

bool StreamPeerTCP::has_var() {
	poll();
	if (incoming.size() < 4) {
		return false;
	}
	const uint32_t size = read_size(incoming);
	if (size > MAX_VAR_SIZE) {
		// Ce qui suit n'est pas une valeur envoyée par put_var (données brutes, flux incohérent
		// ou hostile) : rien n'est lu, et la mémoire reste bornée par le tampon de réception.
		return false;
	}
	return incoming.size() >= 4 + (size_t)size;
}

Variant StreamPeerTCP::get_var() {
	if (!has_var()) {
		return Variant();
	}
	const uint32_t size = read_size(incoming);
	const String text = incoming.substr(4, size);
	incoming.erase(0, 4 + (size_t)size);
	return decode_text(text);
}

void StreamPeerTCP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("connect_to_host", "host", "port"), &StreamPeerTCP::connect_to_host);
	ClassDB::bind_method(D_METHOD("disconnect_from_host"), &StreamPeerTCP::disconnect_from_host);
	ClassDB::bind_method(D_METHOD("poll"), &StreamPeerTCP::poll);
	ClassDB::bind_method(D_METHOD("get_status"), &StreamPeerTCP::get_status);
	ClassDB::bind_method(D_METHOD("get_connected_host"), &StreamPeerTCP::get_connected_host);
	ClassDB::bind_method(D_METHOD("get_connected_port"), &StreamPeerTCP::get_connected_port);
	ClassDB::bind_method(D_METHOD("set_no_delay", "enabled"), &StreamPeerTCP::set_no_delay);
	ClassDB::bind_method(D_METHOD("put_data", "data"), &StreamPeerTCP::put_data);
	ClassDB::bind_method(D_METHOD("get_available_bytes"), &StreamPeerTCP::get_available_bytes);
	ClassDB::bind_method(D_METHOD("get_data", "bytes"), &StreamPeerTCP::get_data, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("put_var", "value"), &StreamPeerTCP::put_var);
	ClassDB::bind_method(D_METHOD("has_var"), &StreamPeerTCP::has_var);
	ClassDB::bind_method(D_METHOD("get_var"), &StreamPeerTCP::get_var);
	BIND_ENUM_CONSTANT(STATUS_NONE);
	BIND_ENUM_CONSTANT(STATUS_CONNECTING);
	BIND_ENUM_CONSTANT(STATUS_CONNECTED);
	BIND_ENUM_CONSTANT(STATUS_ERROR);
}

// ---------------------------------------------------------------------------
// TCPServer
// ---------------------------------------------------------------------------
TCPServer::~TCPServer() {
	net_close(sock);
}

Error TCPServer::listen(int p_port, const String &p_bind_address) {
	stop();
	uint32_t address = 0;
	if (p_port < 0 || p_port > 65535 || !net_resolve(p_bind_address, address)) {
		return ERR_INVALID_PARAMETER;
	}
	const mex_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == MEX_INVALID_SOCKET) {
		return ERR_CANT_CREATE;
	}
#ifndef _WIN32
	// Le port est réutilisable dès l'arrêt du serveur (sous Windows, l'option a un autre sens).
	const int reuse = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
#endif
	const sockaddr_in local = net_address(address, p_port);
	if (::bind(s, (const sockaddr *)&local, sizeof(local)) != 0 || ::listen(s, 16) != 0) {
		net_close((intptr_t)s);
		return ERR_ALREADY_IN_USE;
	}
	net_set_non_blocking(s);
	sock = (intptr_t)s;
	port = net_local_port(s); // port 0 demandé : le système en a choisi un
	return OK;
}

bool TCPServer::is_connection_available() const {
	return sock != -1 && net_select((mex_socket_t)sock, false) > 0;
}

Ref<StreamPeerTCP> TCPServer::take_connection() {
	Ref<StreamPeerTCP> peer;
	if (sock == -1) {
		return peer;
	}
	sockaddr_in remote;
	mex_socklen_t length = sizeof(remote);
	const mex_socket_t s = accept((mex_socket_t)sock, (sockaddr *)&remote, &length);
	if (s == MEX_INVALID_SOCKET) {
		return peer;
	}
	net_set_non_blocking(s);
	peer.instantiate();
	peer->sock = (intptr_t)s;
	peer->status = StreamPeerTCP::STATUS_CONNECTED;
	peer->peer_host = net_ip_string(remote);
	peer->peer_port = ntohs(remote.sin_port);
	return peer;
}

void TCPServer::stop() {
	net_close(sock);
	sock = -1;
	port = 0;
}

void TCPServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("listen", "port", "bind_address"), &TCPServer::listen, DEFVAL(String("*")));
	ClassDB::bind_method(D_METHOD("is_listening"), &TCPServer::is_listening);
	ClassDB::bind_method(D_METHOD("is_connection_available"), &TCPServer::is_connection_available);
	ClassDB::bind_method(D_METHOD("take_connection"), &TCPServer::take_connection);
	ClassDB::bind_method(D_METHOD("get_local_port"), &TCPServer::get_local_port);
	ClassDB::bind_method(D_METHOD("stop"), &TCPServer::stop);
}

// ---------------------------------------------------------------------------
// PacketPeerUDP
// ---------------------------------------------------------------------------
PacketPeerUDP::~PacketPeerUDP() {
	net_close(sock);
}

Error PacketPeerUDP::_open() {
	if (sock != -1) {
		return OK;
	}
	net_init();
	const mex_socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == MEX_INVALID_SOCKET) {
		return ERR_CANT_CREATE;
	}
	net_set_non_blocking(s);
	sock = (intptr_t)s;
	return OK;
}

Error PacketPeerUDP::bind(int p_port, const String &p_bind_address) {
	close();
	uint32_t address = 0;
	if (p_port < 0 || p_port > 65535 || !net_resolve(p_bind_address, address)) {
		return ERR_INVALID_PARAMETER;
	}
	if (_open() != OK) {
		return ERR_CANT_CREATE;
	}
	const sockaddr_in local = net_address(address, p_port);
	if (::bind((mex_socket_t)sock, (const sockaddr *)&local, sizeof(local)) != 0) {
		close();
		return ERR_ALREADY_IN_USE;
	}
	local_port = net_local_port((mex_socket_t)sock);
	return OK;
}

Error PacketPeerUDP::set_dest_address(const String &p_host, int p_port) {
	uint32_t address = 0;
	if (p_port <= 0 || p_port > 65535 || !net_resolve(p_host, address)) {
		return ERR_CANT_RESOLVE;
	}
	dest_host = p_host;
	dest_port = p_port;
	dest_address = address;
	return OK;
}

void PacketPeerUDP::set_broadcast_enabled(bool p_enabled) {
	if (_open() == OK) {
		const int flag = p_enabled ? 1 : 0;
		setsockopt((mex_socket_t)sock, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(flag));
	}
}

Error PacketPeerUDP::put_packet(const String &p_data) {
	if (dest_port == 0) {
		return ERR_UNCONFIGURED;
	}
	if (p_data.size() > 65507) {
		return ERR_INVALID_PARAMETER; // taille maximale d'un paquet UDP
	}
	if (_open() != OK) {
		return ERR_CANT_CREATE;
	}
	const sockaddr_in target = net_address(dest_address, dest_port);
	const int sent = (int)sendto((mex_socket_t)sock, p_data.data(), (int)p_data.size(), 0, (const sockaddr *)&target, sizeof(target));
	if (sent < 0) {
		return ERR_CONNECTION_ERROR;
	}
	if (local_port == 0) {
		local_port = net_local_port((mex_socket_t)sock); // port attribué par le système au premier envoi
	}
	return OK;
}

Error PacketPeerUDP::put_var(const Variant &p_value) {
	return put_packet(VariantWriter::write(p_value));
}

void PacketPeerUDP::_receive() {
	if (sock == -1) {
		return;
	}
	static char buffer[65536];
	while (queue.size() < 1024) {
		sockaddr_in remote;
		mex_socklen_t length = sizeof(remote);
		const int received = (int)recvfrom((mex_socket_t)sock, buffer, sizeof(buffer), 0, (sockaddr *)&remote, &length);
		if (received < 0) {
			break;
		}
		queue.push_back({ String(buffer, (size_t)received), { net_ip_string(remote), (int)ntohs(remote.sin_port) } });
	}
}

int PacketPeerUDP::get_available_packet_count() {
	_receive();
	return (int)queue.size();
}

String PacketPeerUDP::get_packet() {
	_receive();
	if (queue.empty()) {
		return String();
	}
	const String data = queue.front().first;
	packet_ip = queue.front().second.first;
	packet_port = queue.front().second.second;
	queue.erase(queue.begin());
	return data;
}

Variant PacketPeerUDP::get_var() {
	_receive();
	if (queue.empty()) {
		return Variant();
	}
	return decode_text(get_packet());
}

void PacketPeerUDP::close() {
	net_close(sock);
	sock = -1;
	local_port = 0;
	queue.clear();
}

void PacketPeerUDP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bind", "port", "bind_address"), &PacketPeerUDP::bind, DEFVAL(String("*")));
	ClassDB::bind_method(D_METHOD("is_bound"), &PacketPeerUDP::is_bound);
	ClassDB::bind_method(D_METHOD("get_local_port"), &PacketPeerUDP::get_local_port);
	ClassDB::bind_method(D_METHOD("set_dest_address", "host", "port"), &PacketPeerUDP::set_dest_address);
	ClassDB::bind_method(D_METHOD("set_broadcast_enabled", "enabled"), &PacketPeerUDP::set_broadcast_enabled);
	ClassDB::bind_method(D_METHOD("put_packet", "data"), &PacketPeerUDP::put_packet);
	ClassDB::bind_method(D_METHOD("put_var", "value"), &PacketPeerUDP::put_var);
	ClassDB::bind_method(D_METHOD("get_available_packet_count"), &PacketPeerUDP::get_available_packet_count);
	ClassDB::bind_method(D_METHOD("get_packet"), &PacketPeerUDP::get_packet);
	ClassDB::bind_method(D_METHOD("get_var"), &PacketPeerUDP::get_var);
	ClassDB::bind_method(D_METHOD("get_packet_ip"), &PacketPeerUDP::get_packet_ip);
	ClassDB::bind_method(D_METHOD("get_packet_port"), &PacketPeerUDP::get_packet_port);
	ClassDB::bind_method(D_METHOD("close"), &PacketPeerUDP::close);
}
