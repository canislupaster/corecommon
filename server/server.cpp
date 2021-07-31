#include "server.hpp"
#include "parser.hpp"

#include <sys/socket.h>
#include <sys/types.h>

#include <string>
#include <sstream>

void WebServer::listen_error(struct evconnlistener* listener, void* data) {
	struct event_base *base = evconnlistener_get_base(listener);

	int err = EVUTIL_SOCKET_ERROR();
	event_base_loopexit(base, nullptr);

	auto serv = static_cast<WebServer*>(data);
	serv->sock_err = std::optional(WebServerSocketError(err));
}

Request::Request(WebServer& serv, struct bufferevent* bev): serv(serv), bev(bev), pstate(ParsingState::RequestLine), content(nullptr) {}

void WebServer::accept(struct evconnlistener* listener, int fd, struct sockaddr* addr, int addrlen, void* data) {
	auto serv = static_cast<WebServer*>(data);

	auto req = new Request(*serv, bufferevent_socket_new(serv->event_base, fd, BEV_OPT_CLOSE_ON_FREE));
	bufferevent_enable(req->bev, EV_READ | EV_WRITE);

	bufferevent_setcb(req->bev, &Request::readcb, nullptr, &Request::eventcb, static_cast<void*>(req));

	struct timeval tout = {.tv_sec=serv->timeout.count(), .tv_usec=0};
	bufferevent_set_timeouts(req->bev, &tout, nullptr);

	req->handle(serv->handler_factory);
}

WebServer::WebServer(RequestHandlerFactory* factory, int port): event_base(event_base_new()), handler_factory(factory) {
	struct addrinfo hints {
		.ai_flags=AI_PASSIVE | AI_NUMERICSERV | AI_ADDRCONFIG,
		.ai_family=AF_INET,
		.ai_socktype=SOCK_STREAM,
		.ai_protocol=IPPROTO_TCP
	};

	struct addrinfo* res;
	std::string port_str = (std::stringstream() << port).str();
	int rv = getaddrinfo(nullptr, port_str.c_str(), &hints, &res);

	if (rv != 0) {
		throw WebServerUnresolvableAddress();
	}

	//search for viable address
	for (struct addrinfo* cur = res; cur; cur = cur->ai_next) {
		struct evconnlistener *listener;

		listener = evconnlistener_new_bind(
				event_base, accept, this, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 16,
				cur->ai_addr, (int)cur->ai_addrlen);

		if (listener) {
			evconnlistener_set_error_cb(listener, listen_error);
			freeaddrinfo(res);

			return;
		}
	}

	throw WebServerListenerError();
}

char const* WebServerSocketError::what() const noexcept {
	return evutil_socket_error_to_string(ev_err);
}

template<class Delimiter>
Parser<std::string> percent_decode(Delimiter delim, Parser<Unit> const& parser) {
	std::string decoded(Many<Delimiter>(delim).length(parser), 0);

	return (Many(delim + (
			(Match("+") + ResultMap<Unit, Unit>([&](Unit x){decoded.push_back(' '); return Unit();}))
					|| (Match("%") + ArrayMap<2, Char>(Char()) + ResultMap<std::array<char, 2>, char>([](std::array<char, 2> x){
				return hexchar(x[0])*16 + hexchar(x[1]);
			}) + ErrorMap<char>([](char x){return x==0;}) + Ignore<char>())
					|| (Char() + ResultMap<char, Unit>([&](char x){decoded.push_back(x); return Unit();}))))
			+ ResultMap<Unit, std::string>(decoded)).run(parser);
}

Parser<std::vector<URLFormData>> querystring_parse(Parser<Unit> const& parser) {
	auto querystring_terminator = !ParseWS() + !Match("&") + !Match("=");
	auto dec = LazyMap<Unit, std::string>([=](Parser<Unit> const& x) {return percent_decode(querystring_terminator, x);});

	return Multiple(1, std::numeric_limits<size_t>::max(), TupleMap(dec, Match("=") + dec)
			+ ResultMap<std::tuple<std::string, std::string>, URLFormData>([](auto x) {return URLFormData {.name=std::get<0>(x), .value=std::get<1>(x)};})
			+ Skip<URLFormData, decltype(querystring_terminator)>(querystring_terminator)).run(parser);
}

void Request::readcb(struct bufferevent* bev, void* data) {
	auto req = static_cast<Request*>(data);

	struct evbuffer* evbuf = bufferevent_get_input(bev);
	if (req->pstate == ParsingState::Done) {
		evbuffer_drain(evbuf, evbuffer_get_length(evbuf)); //:P
		return;
	} else if (req->pstate == ParsingState::Content) {
		size_t len = evbuffer_get_length(evbuf);
		if (len > req->content->content_length) {
			req->req_handler->request_parse_err();
			return;
		} else if (len == req->content->content_length) {
			std::unique_ptr<char[]> content = std::make_unique<char[]>(len+1);
			if (evbuffer_remove(evbuf, content.get(), req->content->content_length)==-1) {
				req->req_handler->request_parse_err();
				return;
			}

			//handle supported formats
			std::string* ctype = req->headers["Content-Type"];
			if (!ctype || (*ctype!="application/x-www-form-urlencoded" && *ctype!="multipart/form-data")) {
				req->req_handler->request_parse_err();
			} else if (*ctype=="application/x-www-form-urlencoded") {
				if (req->read_content) {
					content[len] = 0;

					char* x = content.get();
					(LazyMap<Unit, std::vector<URLFormData>>(querystring_parse)
							+ ResultMap<std::vector<URLFormData>, Unit>([&](std::vector<URLFormData> const& formdata) {
						req->content = std::make_unique<RequestContent>((RequestContent){.url_formdata = formdata});
						req->req_handler->on_content_recv();
						return Unit();
					})).run(x);
				}
			} else if (*ctype=="multipart/form-data") {
				while (!skip_word(&content, session->parser.multipart_boundary) && *content) content++;
				skip_newline(&content);

				while (*content) {
					//parse preamble
					char* disposition=NULL, *mime=NULL;
					while (*content && !skip_newline(&content)) {
						if (skip_word(&content, "Content-Type:")) {
							parse_ws(&content);
							if (!mime) mime = parse_name(&content, "\r\n");
							skip_newline(&content);
						} else if (skip_word(&content, "Content-Disposition:")) {
							parse_ws(&content);
							if (!disposition) disposition = parse_name(&content, "\r\n");
							skip_newline(&content);
						} else {
							skip_until(&content, "\r\n");
							skip_newline(&content);
						}
					}

					if (!disposition) {
						if (disposition) drop(disposition);
						if (mime) drop(mime);

						respond_error(session, 500, "No disposition or mime");
						terminate(session);
						return 0;
					}

					vector_t val = parse_header_value(disposition);
					char* disposition_name = query_extract_value(&val, "name");
					drop(disposition);

					char* data_start = content;

					unsigned long len = strlen(session->parser.multipart_boundary);
					while (strncmp(content, session->parser.multipart_boundary, len)!=0
							&& content - session->parser.req.content < session->parser.req.content_length)
						content++;

					unsigned long content_len = content-data_start;

					//shrink non-mime content
					if (!mime && *(content-1) == '\n') {
						content_len--;
						if (*(content-2) == '\r') content_len--;
					}

					multipart_data data = {.name=disposition_name, .mime=mime,
							.content=heapcpy(content_len, data_start), .len=content_len};
					vector_pushcpy(&session->parser.req.files, &data);

					if (*content) {
						content += strlen(session->parser.multipart_boundary);
						if (skip_word(&content, "--")) break;
						skip_newline(&content);
					}
				}

				drop(session->parser.multipart_boundary);
			}

			if (session->parser.req.ctype == url_formdata) {
			} else if (session->parser.req.ctype == multipart_formdata) {
			}

			req->pstate = ParsingState::Done;
		}

		return;
	}

	char* line;
	while ((line = evbuffer_readln(evbuf, nullptr, EVBUFFER_EOL_CRLF))) {
		Parser<Unit> parser(line);

		switch (req->pstate) {
			case ParsingState::RequestLine: {
				Parser<Method> method = (Many(ParseWS()) + (Match("GET") + ResultMap<Unit, Method>([](auto x) {return Method::GET;}))
					|| (Match("POST") + ResultMap<Unit, Method>([](auto x) {return Method::POST;}))
					|| (Match("PATCH") + ResultMap<Unit, Method>([](auto x) {return Method::PATCH;}))
					|| (Match("DELETE") + ResultMap<Unit, Method>([](auto x) {return Method::DELETE;}))
					|| (Match("PUT") + ResultMap<Unit, Method>([](auto x) {return Method::PUT;}))
					|| (Match("HEAD") + ResultMap<Unit, Method>([](auto x) {return Method::HEAD;}))).run(parser);

				if (method.err) {
					req->req_handler->request_parse_err();
					return;
				}

				auto path_terminator = Match("/") || ParseWS() || Match("?");

				Parser<Unit> path = (Ignore<Method>() + Many(ParseWS())
					+ Many(!ParseWS() + ParseString(!path_terminator) + ResultMap<std::string, Unit>([&](const std::string& x) {
						req->req_handler->on_segment_recv(x);
						return Unit();
					}))).run(method);

				req->req_handler->on_path_recv();

				if (req->read_content) {
					(Match("?")
							+ LazyMap<Unit, std::vector<URLFormData>>(querystring_parse)
							+ ResultMap<std::vector<URLFormData>, Unit>([&](std::vector<URLFormData> const& x) {
						req->content = std::make_unique<RequestContent>(RequestContent {.url_formdata = x});
						req->req_handler->on_content_recv();
						return Unit();
					})).run(path);
				}

				req->pstate = ParsingState::Headers;
				break;
			}
			case ParsingState::Headers: {
				if (strlen(line)==0) {
					req->pstate = req->content ? ParsingState::Content : ParsingState::Done;
					break;
				}

				if (req->read_content) {
					Parser<long> clength = (Match("Content-Length:") + ParseWS() + ParseInt()).run(parser);

					if (!clength.err) {
						if (!req->content) req->content = std::make_unique<RequestContent>(RequestContent {.content_length=static_cast<size_t>(clength.res)});
						else req->content->content_length = static_cast<size_t>(clength.res);

						break;
					}
				}

				Parser<std::tuple<std::string, std::string>> hdr = TupleMap(ParseString(!Match(":")), Match(":") + ParseWS() + ParseString(!ParseWS())).run(parser);
				if (hdr.err) {
					req->req_handler->request_parse_err();
					return;
				}

				req->headers.insert(std::get<0>(hdr.res), std::get<1>(hdr.res));
				break;
			}
			default:;
		}
	}
}

void Request::eventcb(struct bufferevent* bev, short events, void* data) {

}

void Request::handle(RequestHandlerFactory* factory) {
	req_handler = factory->handle(this);
}

Request::~Request() {
	bufferevent_disable(bev, EV_READ);
	//just in case
	bufferevent_free(bev);
}
