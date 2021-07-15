#ifndef SRC_GL_HPP_
#define SRC_GL_HPP_

#define GL_SILENCE_DEPRECATION
#include "OpenGL/gl3.h"
#include <SDL.h>
#include <fstream>
#include <string>

#include "config.hpp"
#include "util.hpp"

class Window {
public:
	const char* name;
	SDL_GLContext* ctx;

	struct Options {
		int x, y;
		int w, h;
		bool vsync;
		float mouse_sensitivity;

		Options(): x(SDL_WINDOWPOS_CENTERED), y(SDL_WINDOWPOS_CENTERED),
			w(1024), h(512), vsync(true), mouse_sensitivity(0.5) {}

		static Options load(const char* fname) {
			Config cfg;
			cfg.parse(read_file(fname));

			Options opt;
			if (cfg.map["x"]) opt.x = static_cast<int>(std::get<long>(cfg.map["x"]->var));
			if (cfg.map["y"]) opt.y = static_cast<int>(std::get<long>(cfg.map["y"]->var));
			if (cfg.map["w"]) opt.w = static_cast<int>(std::get<long>(cfg.map["w"]->var));
			if (cfg.map["h"]) opt.h = static_cast<int>(std::get<long>(cfg.map["h"]->var));
			if (cfg.map["vsync"]) opt.vsync = std::get<bool>(cfg.map["vsync"]->var);
			if (cfg.map["sensitivity"]) opt.mouse_sensitivity = std::get<float>(cfg.map["sensitivity"]->var);

			return opt;
		}

		void save(const char* fname) const {

		}
	};

	Window(const char* name, Options opts);
};

#endif //SRC_GL_HPP_
