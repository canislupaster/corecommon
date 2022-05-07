#include "server.hpp"

int main(int argc, char** argv) {
	std::string txt = "hi der";
	StaticContent* cont = new StaticContent;
	cont->resp = Response::html(txt.c_str());
	WebServer serv(cont, 8080);
	serv.block();
	if (serv.sock_err) throw *serv.sock_err;
}