#ifdef _WINDOWS
#	include <SDKDDKVer.h>
#
#	ifdef LUAASIO_EXPORTS
#		define DLL_EXPORT __declspec(dllexport)
#	endif
#else
#	define DLL_EXPORT
#endif

#include <cstdlib>
#include <deque>
#include <map>
#include <iostream>

#include <asio.hpp>
using asio::ip::tcp;
using namespace std;

//--------------------------event----------------------------

const char EVT_ACCEPT = 1;
const char EVT_CONTINUE = 2;

const size_t MAX_EVT_MSG = 10240;

struct event_message {
	char type;
	int dest_id;
	void* source;
	string data;
};
typedef deque<event_message> event_message_queue;
event_message_queue g_evt_queue;

void push_event(char type, int id, void* source, const string &data) {
	event_message evt;
	evt.type = type;
	evt.dest_id = id;
	evt.source = source;
	evt.data = std::move(data);
	g_evt_queue.push_back(std::move(evt));

	while (g_evt_queue.size() > MAX_EVT_MSG)
		g_evt_queue.pop_front();
}

//--------------------------client--------------------------

class connection {
private:
	tcp::socket _socket;

private:
	void do_connect(const tcp::resolver::results_type& endpoints, int dest_id)
	{
		asio::async_connect(_socket, endpoints,
			[this, dest_id](std::error_code ec, tcp::endpoint)
		{
			if (!ec)
			{
				connected = true;
				push_event(EVT_CONTINUE, dest_id, this, "");
			}
			else {
				push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
				delete this;
			}
		});
	}

public:
	bool connected;

	connection(asio::io_context& io_context, 
		const tcp::resolver::results_type& endpoints, int dest_id)
		: _socket(io_context), connected(false)
	{
		do_connect(endpoints, dest_id);
	}

	connection(tcp::socket socket)
		: _socket(std::move(socket)), connected(true)
	{
	}

	void write(const string& data, int dest_id) {
		asio::async_write(_socket,
			asio::buffer(data, data.length()),
			[this, dest_id](std::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				push_event(EVT_CONTINUE, dest_id, this, "");
			}
			else
			{
				_socket.close();
				connected = false;
				push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
			}
		});
	}

	void read(size_t size, int dest_id) {
		string data;
		asio::async_read(_socket, asio::buffer(data, size),
			[this, dest_id, &data](std::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				push_event(EVT_CONTINUE, dest_id, this, data);
			}
			else
			{
				_socket.close();
				connected = false;
				push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
			}
		});
	}

	void close() {
		_socket.close();
		connected = false;
	}


};

//--------------------------server--------------------------

class server{
private:

	tcp::acceptor _acceptor;

private:
	void do_accept() {
		_acceptor.async_accept(
			[this](std::error_code ec, tcp::socket socket)
		{
			if (!ec) {
				auto con = new connection(std::move(socket));
				push_event(EVT_ACCEPT, port, con, "");
			}

			do_accept();
		});
	}

public:

	int port;

	server(asio::io_context& io_context, 
		const asio::ip::address &ip, int port)
		: 
		_acceptor(io_context, tcp::endpoint(ip, port)),
		port(port)
	{
		do_accept();
	}

};


//--------------------------api--------------------------

//----------------------------------------------------

asio::io_context io_context;

extern "C"
DLL_EXPORT void asio_delete_server(void* p) {
	server* svr = (server*)p;
	delete svr;
}

extern "C"
DLL_EXPORT void* asio_new_server(const char* ip, int port) {

	auto svr = new server(io_context, asio::ip::address::from_string(ip), port);
	return svr;
}


extern "C"
DLL_EXPORT void asio_delete_connection(void* p) {
	connection* conn = (connection*)p;
	delete conn;
}

extern "C"
DLL_EXPORT void* asio_new_connect(const char* host, const char* port, int dest_id) {
	tcp::resolver resolver(io_context);
	auto endpoints = resolver.resolve(host, port);
	auto conn = new connection(io_context, endpoints, dest_id);
	return conn;
}

extern "C"
DLL_EXPORT void asio_conn_read(void* p, size_t size, int dest_id) {
	connection* conn = (connection*)p;
	conn->read(size, dest_id);
}

extern "C"
DLL_EXPORT void asio_conn_write(void* p, const char* data, size_t size, int dest_id) {
	connection* conn = (connection*)p;
	conn->write(std::move(string(data, size)), dest_id);
}


struct event_message_for_ffi {
	char type;
	int dest_id;
	void* source;
	const char* data;
	size_t data_len;
};

extern "C"
DLL_EXPORT event_message_for_ffi asio_get() {
	try {
		while(g_evt_queue.empty())
			io_context.run_one();
			
		static event_message evt;
		evt = g_evt_queue.front();
		g_evt_queue.pop_front();

		event_message_for_ffi rtn;
		rtn.type = evt.type;
		rtn.dest_id = evt.dest_id;
		rtn.source = evt.source;
		rtn.data = evt.data.c_str();
		rtn.data_len = evt.data.size();

		return rtn;
	}
	catch (std::exception& e)
	{
		std::cerr << "LuaAsio Exception: " << e.what() << "\n";
		return event_message_for_ffi();
	}
}

//---------------------------------------------------

