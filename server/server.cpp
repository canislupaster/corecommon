#include "server.hpp"
#include "parser.hpp"
#include "reason.hpp"

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

WebServer::~WebServer() {
	evconnlistener_free(listener);
	event_base_free(event_base);
}

Request::Request(WebServer& serv, struct bufferevent* bev): serv(serv), bev(bev), pstate(ParsingState::RequestLine), content(nullptr), req_handler() {}

void WebServer::accept(struct evconnlistener* listener, int fd, struct sockaddr* addr, int addrlen, void* data) {
	auto serv = static_cast<WebServer*>(data);

	auto req = new Request(*serv, bufferevent_socket_new(serv->event_base, fd, BEV_OPT_CLOSE_ON_FREE));
	req->mtx.lock();
	bufferevent_enable(req->bev, EV_READ | EV_WRITE);

	bufferevent_setcb(req->bev, &Request::readcb, &Request::writecb, &Request::eventcb, static_cast<void*>(req));

	struct timeval tout = {.tv_sec=serv->timeout.count(), .tv_usec=0};
	bufferevent_set_timeouts(req->bev, &tout, nullptr);

	req->handle(serv->handler_factory);
	req->mtx.unlock();
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

void WebServer::block() {
	event_base_loop(event_base, 0);
}

char const* WebServerSocketError::what() const noexcept {
	return evutil_socket_error_to_string(ev_err);
}

template<class Delimiter>
Parser<std::string> percent_decode(Delimiter delim, Parser<Unit> const& parser) {
	std::string decoded(Many<Chain<Delimiter, Any>>(delim + Any()).length(parser), 0);

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

	return Multiple(1, std::numeric_limits<size_t>::max(), (TupleMap(dec, Ignore<std::string>() + Match("=") + dec)
			+ ResultMap<std::tuple<std::string, std::string>, URLFormData>([](auto x) {return URLFormData {.name=std::get<0>(x), .value=std::get<1>(x)};})
			).skip(querystring_terminator)).run(parser);
}

Parser<std::pair<std::string, Header>> parse_header(Parser<Unit> const& parser) {
	auto val_sep = ParseWS() || Match(",") || Match(";");
	return (TupleMap(ParseString(Many(!Match(":") + Any())), Ignore<std::string>() + Match(":") + ParseWS() +
			ParseString(Many(!val_sep + Any()))) + LazyMap<std::tuple<std::string, std::string>, std::pair<std::string, Header>>([&](Parser<std::tuple<std::string, std::string>> const& parser) {
		auto const& a = parser.res;
		auto hdr = std::make_pair(std::get<0>(parser.res), Header {.val = std::get<1>(parser.res)});

		auto parse_val = [&](auto str) {return ((Match("\"") + ParseString(Many(Match("\\") + Any()) || (!Match("\"") + Any())).skip(Match("\""))) || ParseString(Many(1, std::numeric_limits<size_t>::max(), !val_sep + Any()))) + ResultMap<std::string, Unit>([&](auto from) {
			hdr.second.extra.push_back(std::make_pair(str, from));
			return Unit();
		});};

		auto parse_vals_sep = (LazyMap<std::string, Unit>([&](auto key) {return (Ignore<std::string>() + parse_val(key.res)).run(key);}) + Many(Match(" "))).separated(Match(","));
		auto parse_init_vals_sep = Many(Many(Match(" ")) + Match(",") + Many(Match(" ")) + parse_val(hdr.first));

		return Parser((Ignore<std::tuple<std::string, std::string>>() + parse_init_vals_sep.maybe() + Many(Match(";") + Many(Match(" ")) + ParseString(Many(!ParseWS() + !Match("=") + Any())).skip(ParseWS() + Match("=") + ParseWS()) + parse_vals_sep)).run(parser), hdr);
	})).run(parser);
}

void Request::parse_err() {
	req_handler->request_parse_err();
	delete this;
}

void Request::readcb(struct bufferevent* bev, void* data) {
	auto req = static_cast<Request*>(data);
	req->mtx.lock();

	struct evbuffer* evbuf = bufferevent_get_input(bev);
	if (req->pstate == ParsingState::Done) {
		evbuffer_drain(evbuf, evbuffer_get_length(evbuf)); //:P
	} else if (req->pstate == ParsingState::Content) {
		size_t len = evbuffer_get_length(evbuf);
		if (len > req->content->content_length) {
			req->parse_err();
			return;
		} else if (len == req->content->content_length) {
			std::unique_ptr<char[]> content = std::make_unique<char[]>(len+1);
			if (evbuffer_remove(evbuf, content.get(), req->content->content_length)==-1) {
				req->parse_err();
				return;
			}

			//handle supported formats
			Header* ctype = req->headers["Content-Type"];
			if (!ctype) {
				req->parse_err();
				return;
			} else if (ctype->val=="application/x-www-form-urlencoded") {
				if (req->read_content) {
					content[len] = 0;

					char* x = content.get();
					(LazyMap<Unit, std::vector<URLFormData>>(querystring_parse)
							+ ResultMap<std::vector<URLFormData>, Unit>([&](std::vector<URLFormData> const& formdata) {
						req->content->url_formdata = formdata;
						req->req_handler->on_content_recv();
						return Unit();
					})).run(x);
				}
			} else if (ctype->val=="multipart/form-data") {
				auto ctype_iter = std::find_if(ctype->extra.begin(), ctype->extra.end(), [](auto x){return x.first=="boundary";});
				if (ctype_iter==ctype->extra.end()) {
					req->parse_err();
					return;
				}

				char* x = content.get();
				char const* boundary = ctype_iter->second.c_str();

				auto newl = Match("\n") || Match("\r\n");
				auto parsed = (Many(Many(!Match(boundary) + Any()) + Match(boundary).skip(Match("--")) + newl + LazyMap<Unit, Unit>([=](Parser<Unit> const& x) -> Parser<Unit> {
					MultipartFormData data;
					auto res = Multiple(LazyMap<Unit, std::pair<std::string, Header>>(parse_header).skip(newl)).run(x);

					data.headers = res.res;
					for (auto& hd: data.headers) {
						if (hd.first == "Content-Type") {
							data.mime = hd.second.val.c_str();
						} else if (hd.first == "Content-Disposition") {
							auto iter = std::find_if(hd.second.extra.begin(), hd.second.extra.end(), [](auto x){return x.first=="name";});
							if (iter!=hd.second.extra.end()) data.name = iter->second.c_str();
						}
					}

					char const* start = res.span.text;

					unsigned long len = ctype_iter->second.size();
					while (strncmp(res.span.text, boundary, len)!=0 && len<=res.span.length) {
						res.span.text++;
						res.span.length--;
					}

					data.content.insert(data.content.begin(), start, res.span.text);
					req->content->multipart_formdata.push_back(data);

					return Parser(res, Unit());
				}))).run(x);

				if (parsed.err) {
					req->parse_err();
					return;
				}

				req->req_handler->on_content_recv();
			} else {
				req->parse_err();
				return;
			}

			req->pstate = ParsingState::Done;
		}
	} else {
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
							req->parse_err();
							return;
					 }

					 auto path_terminator = Match("/") || ParseWS() || Match("?");

					 Parser<Unit> path = (Ignore<Method>() + Many(Match("/") || ParseWS())
							+ (ParseString(Many(1, std::numeric_limits<size_t>::max(), !path_terminator + Any())) + ResultMap<std::string, Unit>([&](const std::string& x) {
								req->req_handler->on_segment_recv(x);
								return Unit();
							})).separated(Match("/")).maybe()).run(method);

					 req->req_handler->on_path_recv();

					 if (req->read_content) {
							path = (Match("?")
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

					auto hdr = parse_header(parser);

					if (hdr.err) {
						req->parse_err();
						return;
					}

					req->headers.insert(hdr.res.first, hdr.res.second);

					break;
				}
				default:;
			}
		}
	}

	req->mtx.unlock();
}

void Request::writecb(struct bufferevent* bev, void* data) {
	auto req = static_cast<Request*>(data);
	req->mtx.lock();

	if (req->to_close && !req->closed && bufferevent_flush(req->bev, EV_WRITE, BEV_FLUSH)!=1) {
		req->closed=true;
		req->close();
	}

	req->mtx.unlock();
}

void Request::close() {
	req_handler->request_close();

	bufferevent_disable(bev, EV_READ);
	//just in case
	bufferevent_free(bev);
}

void Request::respond(Response const& resp) {
	if (to_close) return;
	to_close=true;

	struct evbuffer* evbuf = bufferevent_get_output(bev);
	evbuffer_add_printf(evbuf, "HTTP/1.1 %i %s\r\n", resp.status, reason(resp.status));

	std::visit(overloaded {
		[&](MaybeOwnedSlice<const char> const& slice){
			evbuffer_add_printf(evbuf, "Content-Length: %lu\r\n", slice.size());
		},
		[&](FILE* file) {
			fseek(file, 0, SEEK_END);
			evbuffer_add_printf(evbuf, "Content-Length: %lu\r\n", static_cast<unsigned long>(ftell(file)));
		},
		[](auto x){}
	}, resp.content);

	for (auto const& hdr: resp.headers) {
		evbuffer_add_printf(evbuf, "%s:%s", hdr.first.c_str(), hdr.second.val.c_str());
		for (auto const& extra: hdr.second.extra) {
			evbuffer_add_printf(evbuf, ",%s=\"%s\"", extra.first.c_str(), extra.second.c_str());
		}

		evbuffer_add_printf(evbuf, hdr.second.extra.empty() ? "\r\n" : ";\r\n");
	}

	evbuffer_add_printf(evbuf, "\r\n");

	std::visit(overloaded {
			[&](MaybeOwnedSlice<const char> const& slice){
				evbuffer_add(evbuf, slice.data, slice.size());
			},
			[&](FILE* file) {
				unsigned long len = ftell(file);
				fseek(file, 0, SEEK_SET);
				evbuffer_add_file(evbuf, fileno(file), 0, len);
			},
			[](std::monostate x){}
	}, resp.content);

	bufferevent_flush(bev, EV_WRITE, BEV_FLUSH);
}

void Request::eventcb(struct bufferevent* bev, short events, void* data) {
	Request* req = static_cast<Request*>(data);

	req->mtx.lock();

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
		req->close();
		delete req;
		return;
	}

	req->mtx.unlock();
}

void Request::handle(RequestHandlerFactory* factory) {
	req_handler = std::unique_ptr<RequestHandler>(factory->handle(this));
}

Request::~Request() {
	if (!closed) close();
}

void Router::RouterRequestHandler::on_segment_recv(std::string const& seg) {
	auto fact = parent.routes[seg];
	if (!fact) fact = &parent.not_found;

	this->req->handle(fact->get());
}

RequestHandler* Router::handle(Request* req) {
	return new RouterRequestHandler(req, *this);
}

StaticContent::ContentHandler::ContentHandler(Request* req, Response const& resp): RequestHandler(req), resp(resp) {
	req->respond(resp);
}

RequestHandler* StaticContent::handle(Request* req) {
	return new ContentHandler(req, resp);
}

