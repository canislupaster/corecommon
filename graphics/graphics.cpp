#include "graphics.hpp"

Window::Window(const char *name, Options opts) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) errx("sdlinit failed");

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	int x = *cfg_get(&cfg, CFG_X);
	int y = *cfg_get(&cfg, CFG_Y);
	int width = *cfg_get(&cfg, CFG_WIDTH);
	int height = *cfg_get(&cfg, CFG_HEIGHT);
	int vsync = *cfg_get(&cfg, CFG_VSYNC);
	int mouse_sensitivity = *cfg_get(&cfg, CFG_MOUSE_SENSITIVITY);

	SDL_Window* window = SDL_CreateWindow(
					"fps", x, y, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
}
