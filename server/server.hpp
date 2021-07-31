#ifndef CORECOMMON_SRC_SERVER_HPP_
#define CORECOMMON_SRC_SERVER_HPP_

#include <utility>
#include <variant>
#include <chrono>

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

	std::chrono::seconds timeout = std::chrono::seconds(10);
	size_t max_content = 1024*1024*100; //100 mb

	RequestHandlerFactory* handler_factory;
	std::optional<WebServerSocketError> sock_err;

 private:
	struct event_base* event_base;

	static void listen_error(struct evconnlistener* listener, void* data);
	static void accept(struct evconnlistener* listener, int fd, struct sockaddr* addr, int addrlen, void* data);

	friend class Request;
};

struct URLFormData {
	std::string name;
	std::string value;
};

struct MultipartFormData {
	std::string name;
	std::string mime;

	std::vector<char> content;
};

struct RequestContent {
	size_t content_length;
	std::vector<URLFormData> url_formdata;
	std::vector<MultipartFormData> multipart_formdata;
};

struct Request {
 public:
	Method method;
	Map<std::string, std::string> headers;
	bool read_content = false;

	struct bufferevent* bev;
	WebServer& serv;

	void handle(RequestHandlerFactory* factory);
	~Request();

 private:
	std::unique_ptr<RequestContent> content;

	Request(WebServer& serv, struct bufferevent* bev);

	//OHGODOSAVEME
	RequestHandler* req_handler;

	enum class ParsingState {
		RequestLine,
		Headers,
		Content,
		Done
	};

	ParsingState pstate;
	std::optional<std::string> multipart_boundary;

	static void readcb(struct bufferevent* formdata, void* data);
	static void eventcb(struct bufferevent* bev, short events, void* data);

	friend class WebServer;
};

struct Response {
	int status;
	std::vector<std::pair<std::string, std::string>> headers;
	MaybeOwnedSlice<char> content;
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
		delete this;
	}

	virtual ~RequestHandler() {}

 private:
	friend class Request;
};

struct RequestHandlerFactory {
	virtual RequestHandler* handle(Request* req) = 0;
};

struct Router: public RequestHandlerFactory {
	struct RouterRequestHandler: public RequestHandler {
		Router& parent;
		RouterRequestHandler(Request* req, Router& parent): RequestHandler(req), parent(parent) {}

		void on_segment_recv(std::string const& seg) override {
			auto fact = parent.routes[seg];
			if (!fact) fact = &parent.not_found;

			this->req->handle(fact->get());
		}
	};

	Map<std::string, std::unique_ptr<RequestHandlerFactory>> routes;
	std::unique_ptr<RequestHandlerFactory> not_found;

	RequestHandler* handle(Request* req) override {
		return new RouterRequestHandler(req, *this);
	}
};

#endif //CORECOMMON_SRC_SERVER_HPP_
