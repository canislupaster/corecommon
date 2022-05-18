#include "server.hpp"
#include "mime.hpp"
#include <iostream>

struct FileServer: public RequestHandlerFactory {
	std::string path;
	FileServer(std::string path): path(path) {}

	struct FileServerHandler: public RequestHandler {
		std::string path;
		FileServerHandler(std::string path, Request* req): path(path), RequestHandler(req) {}

		void on_segment_recv(std::string const& seg) override {
//			if (std::find_if_not(seg.begin(), seg.end(), [](auto c){return std::isalnum(c);})!=seg.end()) {
//				request_parse_err();
//				return;
//			}

			path.append(seg);
		}

		void on_path_recv() override {
			FILE* f = fopen(path.c_str(), "r");
			if (!f) {
				req->respond(Response::html("not found lmao"));
				return;
			}

			auto dot = path.find_last_of(".");
			std::string ext(path.begin()+dot+1, path.end());

			auto mime = std::find_if(mime_types, mime_types+num_mime_types, [&](char const* ty [2]){return strcmp(ty[0], ext.c_str())==0;});

			Response resp {.status=200, .headers={}, .content=decltype(Response::content)(f)};
			if (mime!=mime_types+num_mime_types) {
				resp.headers.emplace_back("Content-Type", Header {.val=std::string((*mime)[1])});
			}

			req->respond(resp);
		}
	};

	RequestHandler* handle(Request* req) override {
		return new FileServerHandler(path, req);
	}
};

int main(int argc, char** argv) {
	if (argc<3) {
		std::cout<<"args"<<std::endl;
		return 1;
	}

	WebServer serv(new FileServer(std::string(argv[1])), static_cast<int>(strtol(argv[2], nullptr, 10)));
	serv.block();
	if (serv.sock_err) throw *serv.sock_err;
}