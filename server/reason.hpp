#ifndef CORECOMMON_SERVER_REASON_HPP_
#define CORECOMMON_SERVER_REASON_HPP_

char* reason(int code) {
	switch (code) {
		case 100: return "Section 10.1.1: Continue";
		case 101: return "Section 10.1.2: Switching Protocols";
		case 200: return "Section 10.2.1: OK";
		case 201: return "Section 10.2.2: Created";
		case 202: return "Section 10.2.3: Accepted";
		case 203: return "Section 10.2.4: Non-Authoritative Information";
		case 204: return "Section 10.2.5: No Content";
		case 205: return "Section 10.2.6: Reset Content";
		case 206: return "Section 10.2.7: Partial Content";
		case 300: return "Section 10.3.1: Multiple Choices";
		case 301: return "Section 10.3.2: Moved Permanently";
		case 302: return "Section 10.3.3: Found";
		case 303: return "Section 10.3.4: See Other";
		case 304: return "Section 10.3.5: Not Modified";
		case 305: return "Section 10.3.6: Use Proxy";
		case 307: return "Section 10.3.8: Temporary Redirect";
		case 400: return "Section 10.4.1: Bad Request";
		case 401: return "Section 10.4.2: Unauthorized";
		case 402: return "Section 10.4.3: Payment Required";
		case 403: return "Section 10.4.4: Forbidden";
		case 404: return "Section 10.4.5: Not Found";
		case 405: return "Section 10.4.6: Method Not Allowed";
		case 406: return "Section 10.4.7: Not Acceptable";
		case 407: return "Section 10.4.8: Proxy Authentication Required";
		case 408: return "Section 10.4.9: Request Time-out";
		case 409: return "Section 10.4.10: Conflict";
		case 410: return "Section 10.4.11: Gone";
		case 411: return "Section 10.4.12: Length Required";
		case 412: return "Section 10.4.13: Precondition Failed";
		case 413: return "Section 10.4.14: Request Entity Too Large";
		case 414: return "Section 10.4.15: Request-URI Too Large";
		case 415: return "Section 10.4.16: Unsupported Media Type";
		case 416: return "Section 10.4.17: Requested range not satisfiable";
		case 417: return "Section 10.4.18: Expectation Failed";
		case 500: return "Section 10.5.1: Internal Server Error";
		case 501: return "Section 10.5.2: Not Implemented";
		case 502: return "Section 10.5.3: Bad Gateway";
		case 503: return "Section 10.5.4: Service Unavailable";
		case 504: return "Section 10.5.5: Gateway Time-out";
		case 505: return "Section 10.5.6: HTTP Version not supported";
		default: return "???";
	}
}

#endif //CORECOMMON_SERVER_REASON_HPP_
