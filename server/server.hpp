#ifndef CORECOMMON_SRC_SERVER_HPP_
#define CORECOMMON_SRC_SERVER_HPP_

#include <utility>
#include <variant>
#include <chrono>
#include <thread>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

#include "util.hpp"
#include "map.hpp"

enum class Method {
	GET,
	POST,
	HEAD,
	PUT,
	PATCH,
	DELETE
};

struct RequestHandler;
struct RequestHandlerFactory;

struct WebServerUnresolvableAddress: public std::exception {
	char const* what() const noexcept override {
		return "could not resolve address";
	}
};

struct WebServerListenerError: public std::exception {
	char const* what() const noexcept override {
		return "error starting listener";
	}
};

struct WebServerSocketError: public std::exception {
	int ev_err;
	WebServerSocketError(int ev_err): ev_err(ev_err) {}
	char const* what() const noexcept override;
};

class WebServer {
 public:
	WebServer(RequestHandlerFactory* factory, int port=80);
	~WebServer();

	std::chrono::seconds timeout = std::chrono::seconds(10);
	size_t max_content = 1024*1024*100;

	RequestHandlerFactory* handler_factory;
	std::optional<WebServerSocketError> sock_err;

	void block();

 private:
	struct event_base* event_base;
	struct evconnlistener* listener;

	static void listen_error(struct evconnlistener* listener, void* data);
	static void accept(struct evconnlistener* listener, int fd, struct sockaddr* addr, int addrlen, void* data);

	friend class Request;
};

struct URLFormData {
	std::string name;
	std::string value;
};

struct Header {
	std::string val;
	std::vector<std::pair<std::string, std::string>> extra;
};

struct MultipartFormData {
	std::vector<std::pair<std::string, Header>> headers;
	char const* name;
	char const* mime;

	std::vector<char> content;
};

struct RequestContent {
	size_t content_length;
	std::vector<URLFormData> url_formdata;
	std::vector<MultipartFormData> multipart_formdata;
};

struct Response {
	int status;
	std::vector<std::pair<std::string, Header>> headers;
	std::optional<MaybeOwnedSlice<const char>> content;

	static Response html(char const* html) {
		return {.status=200, .headers={std::make_pair("Content-Type", Header {.val="text/html"})}, .content=std::make_optional(MaybeOwnedSlice(html, strlen(html), false))};
	}
};

struct Request {
 public:
	std::mutex mtx;
	Method method;
	Map<std::string, Header> headers;
	bool read_content = false;

	struct bufferevent* bev;
	WebServer& serv;

	void handle(RequestHandlerFactory* factory);
	void respond(Response const& resp);
	~Request();

 private:
	std::unique_ptr<RequestContent> content;

	Request(WebServer& serv, struct bufferevent* bev);

	std::unique_ptr<RequestHandler> req_handler;

	enum class ParsingState {
		RequestLine,
		Headers,
		Content,
		Done
	};

	ParsingState pstate;

	static void readcb(struct bufferevent* formdata, void* data);
	static void eventcb(struct bufferevent* bev, short events, void* data);

	friend class WebServer;
};

class RequestHandler {
 public:
	Request* req;
	RequestHandler(Request* req): req(req) {}

	virtual void on_segment_recv(std::string const& seg) {}
	virtual void on_path_recv() {}
	//may be called multiple times eg. if url, formdata / multipart are given separately
	virtual void on_content_recv() {}

	virtual void request_parse_err() {
		//alternatively, respond bad req / err page
		delete req;
	}

	virtual void request_close() {
		delete req;
	}

	virtual ~RequestHandler() {}

 private:
	friend class Request;
};

struct RequestHandlerFactory {
	virtual RequestHandler* handle(Request* req) = 0;
};

struct StaticContent: public RequestHandlerFactory {
	Response resp;

	struct ContentHandler: public RequestHandler {
		Response const& resp;
		ContentHandler(Request* req, Response const& resp);
	};

	RequestHandler* handle(Request* req) override;
};

struct Router: public RequestHandlerFactory {
	struct RouterRequestHandler: public RequestHandler {
		Router& parent;
		RouterRequestHandler(Request* req, Router& parent): RequestHandler(req), parent(parent) {}

		void on_segment_recv(std::string const& seg) override;
	};

	Map<std::string, std::unique_ptr<RequestHandlerFactory>> routes;
	std::unique_ptr<RequestHandlerFactory> not_found;

	RequestHandler* handle(Request* req) override;
};

#endif //CORECOMMON_SRC_SERVER_HPP_
