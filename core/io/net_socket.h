// Réseau : connexions TCP (StreamPeerTCP, TCPServer) et paquets UDP (PacketPeerUDP).
//
// Tout est non bloquant : on interroge l'état à chaque image (poll, get_available_bytes...).
// Les données sont des chaînes d'octets ; put_var / get_var envoient n'importe quelle valeur
// (nombre, texte, tableau, dictionnaire...) précédée de sa longueur, donc sans mélange possible.
// IPv4 uniquement, sans chiffrement.
#pragma once

#include "core/object/ref_counted.h"

class StreamPeerTCP : public RefCounted {
	GDCLASS(StreamPeerTCP, RefCounted)

public:
	enum Status {
		STATUS_NONE,
		STATUS_CONNECTING,
		STATUS_CONNECTED,
		STATUS_ERROR,
	};

private:
	friend class TCPServer;
	intptr_t sock = -1;
	int status = STATUS_NONE;
	String peer_host;
	int peer_port = 0;
	String incoming; // octets reçus, pas encore lus
	uint64_t connect_start = 0;

	void _read_available();

protected:
	static void _bind_methods();

public:
	Error connect_to_host(const String &p_host, int p_port);
	void disconnect_from_host();
	void poll();
	int get_status() const { return status; }
	String get_connected_host() const { return peer_host; }
	int get_connected_port() const { return peer_port; }
	void set_no_delay(bool p_enabled);

	Error put_data(const String &p_data);
	int get_available_bytes();
	String get_data(int p_bytes = -1);
	Error put_var(const Variant &p_value);
	// Vrai si une valeur complète envoyée par put_var() attend d'être lue par get_var().
	bool has_var();
	Variant get_var();

	~StreamPeerTCP();
};

class TCPServer : public RefCounted {
	GDCLASS(TCPServer, RefCounted)

	intptr_t sock = -1;
	int port = 0;

protected:
	static void _bind_methods();

public:
	Error listen(int p_port, const String &p_bind_address = "*");
	bool is_listening() const { return sock != -1; }
	bool is_connection_available() const;
	Ref<StreamPeerTCP> take_connection();
	int get_local_port() const { return port; }
	void stop();

	~TCPServer();
};

class PacketPeerUDP : public RefCounted {
	GDCLASS(PacketPeerUDP, RefCounted)

	intptr_t sock = -1;
	int local_port = 0;
	String dest_host;
	int dest_port = 0;
	uint32_t dest_address = 0; // ordre réseau
	String packet_ip;
	int packet_port = 0;
	std::vector<std::pair<String, std::pair<String, int>>> queue; // paquets reçus (données, expéditeur)

	Error _open();
	void _receive();

protected:
	static void _bind_methods();

public:
	Error bind(int p_port, const String &p_bind_address = "*");
	bool is_bound() const { return local_port != 0; }
	int get_local_port() const { return local_port; }
	Error set_dest_address(const String &p_host, int p_port);
	void set_broadcast_enabled(bool p_enabled);
	Error put_packet(const String &p_data);
	Error put_var(const Variant &p_value);
	int get_available_packet_count();
	String get_packet();
	Variant get_var();
	String get_packet_ip() const { return packet_ip; }
	int get_packet_port() const { return packet_port; }
	void close();

	~PacketPeerUDP();
};
