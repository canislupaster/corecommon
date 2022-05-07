#ifndef SRC_GL_HPP_
#define SRC_GL_HPP_

#define GL_SILENCE_DEPRECATION
#include <SDL.h>
#include "OpenGL/gl3.h"

#include <iostream>
#include <fstream>
#include <string>
#include <utility>

#include "config.hpp"
#include "field.hpp"
#include "util.hpp"

#include "nanosvg.h"

struct GLException: std::runtime_error {
	using std::runtime_error::runtime_error;
};

struct SDLException: std::runtime_error {
	using std::runtime_error::runtime_error;

	~SDLException() override {
		SDL_ClearError();
	}
};

void sdl_reporterr();
void gl_checkerr();

template<bool is_frag>
struct VertFragShader {
	GLuint idx;

	explicit VertFragShader(const char* src): idx(glCreateShader(is_frag ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER)) {
		glShaderSource(idx, 1, (const GLchar* const*)&src, nullptr);
		glCompileShader(idx);

		int succ;
		std::string err(1024, 0);

		glGetShaderiv(idx, GL_COMPILE_STATUS, &succ);
		if (!succ) {
			glGetShaderInfoLog(idx, 1024, nullptr, err.data());
			throw GLException(err);
		}
	}

	~VertFragShader() {
		glDeleteShader(idx);
	}
};

using VertShader = VertFragShader<false>;
using FragShader = VertFragShader<true>;

//kinda like a c++ trait? i guess
template<class UniformType>
struct Uniform {};

template<>
struct Uniform<int> {
	static void use(int x, GLint location) {
		glUniform1i(location, x);
	}
};

template<>
struct Uniform<float> {
	static void use(float x, GLint location) {
		glUniform1f(location, x);
	}
};

template<>
struct Uniform<Vec2> {
	static void use(Vec2 const& x, GLint location) {
		glUniform2fv(location, 1, x.begin());
	}
};

template<>
struct Uniform<Vec3> {
	static void use(Vec3 const& x, GLint location) {
		glUniform3fv(location, 1, x.begin());
	}
};

template<>
struct Uniform<Mat2> {
	static void use(Mat2 const& x, GLint location) {
		glUniformMatrix2fv(location, 1, false, x.cols->begin());
	}
};

template<>
struct Uniform<Mat3> {
	static void use(Mat3 const& x, GLint location) {
		glUniformMatrix3fv(location, 1, false, x.cols->begin());
	}
};

template<>
struct Uniform<Mat4> {
	static void use(Mat4 const& x, GLint location) {
		glUniformMatrix4fv(location, 1, false, x.cols->begin());
	}
};

template<>
struct Uniform<Vec4> {
	static void use(Vec4 const& x, GLint location) {
		glUniform4fv(location, 1, x.begin());
	}
};

enum class BlockIndices {
	Object,
	Lighting
};

template<class ...Uniforms>
struct UniformBuffer {
	GLuint idx;

	UniformBuffer() {
		glGenBuffers(1, &idx);
		glBindBuffer(GL_UNIFORM_BUFFER, idx);
		GLintptr size = (sizeof(Uniforms) + ... + 0);
		glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
	}

	UniformBuffer(UniformBuffer&& other): idx(other.idx) {
		other.idx=-1;
	}

	UniformBuffer(UniformBuffer const& other) = delete;

	using Tuple = std::tuple<Uniforms const&...>;

	template<size_t i, class ...UseUniforms>
	struct UniformBufferDataIterator {
		void set_data(size_t offset, Tuple tup) { }
	};

	template<size_t i, class Uniform, class ...UseUniforms>
	struct UniformBufferDataIterator<i, Uniform, UseUniforms...> {
		void set_data(size_t offset, Tuple tup) {
			glBufferSubData(GL_UNIFORM_BUFFER, offset, sizeof(Uniform), &std::get<i>(tup));
			UniformBufferDataIterator<i+1, UseUniforms...>().set_data(offset + sizeof(Uniform), tup);
		}
	};

	void set_data(Tuple uniforms) {
		glBindBuffer(GL_UNIFORM_BUFFER, idx);
		UniformBufferDataIterator<0, Uniforms...>().set_data(0, uniforms);
	}

	void use(GLuint base) {
		glBindBufferBase(GL_UNIFORM_BUFFER, base, idx);
	}

	UniformBuffer(GLint base, Tuple uniforms): UniformBuffer(base) {
		UniformBufferDataIterator<sizeof...(Uniforms)-1, Uniforms...>().set_data(0, uniforms);
	}

	~UniformBuffer() {
		if (idx!=-1) glDeleteBuffers(1, &idx);
	}
};

template<class ...Uniforms>
struct Shader {
	GLuint prog;
	std::shared_ptr<const VertShader> vert;
	std::shared_ptr<const FragShader> frag;

	std::array<GLint, sizeof...(Uniforms)> uniform_locations;

	Shader(std::shared_ptr<const VertShader> vert, std::shared_ptr<const FragShader> frag, char const* uniform_names[]): vert(vert), frag(frag), prog(glCreateProgram()) {
		glAttachShader(prog, vert->idx);
		glAttachShader(prog, frag->idx);
		glLinkProgram(prog);

		for (size_t i = 0; i < sizeof...(Uniforms); i++) {
			uniform_locations[i] = glGetUniformLocation(prog, uniform_names[i]);
		}

		gl_checkerr();
	}

	Shader(Shader const& other) = delete;
	Shader(Shader&& other): prog(other.prog), vert(other.vert), frag(other.frag), uniform_locations(other.uniform_locations) {
		other.prog=-1;
	}

	Shader& operator=(Shader&& other) {
		prog=other.prog; vert=other.vert; frag=other.frag; uniform_locations=other.uniform_locations;
		other.prog=-1;
		return *this;
	}

	using Tuple = std::tuple<Uniforms...>;

	template<size_t i, class ...UseUniforms>
	struct UseUniformIterator {
		void use(GLint const* const uniform_location, Tuple tup) { }
	};

	template<size_t i, class UniformType, class ...UseUniforms>
	struct UseUniformIterator<i, UniformType, UseUniforms...> {
		void use(GLint const* const uniform_location, Tuple tup) {
			Uniform<UniformType>::use(std::get<i>(tup), *uniform_location);
			UseUniformIterator<i-1, UseUniforms...>().use(uniform_location+1, tup);
		}
	};

	void use(std::tuple<Uniforms...> const& uniforms) const {
		glUseProgram(prog);
		UseUniformIterator<sizeof...(Uniforms)-1, Uniforms...>().use(uniform_locations.begin(), uniforms);
	}

	~Shader() {
		if (prog!=-1) glDeleteProgram(prog);
	}
};

template<class ...Uniforms>
struct ObjectShader: public Shader<Uniforms...> {
	GLint object_block;
	GLint transform_mat;

	ObjectShader(std::shared_ptr<const VertShader> vert, std::shared_ptr<const FragShader> frag, const char* uniform_names[]):
		Shader<Uniforms...>(vert, frag, uniform_names), transform_mat(glGetUniformLocation(this->prog, "transform")) {

		object_block = glGetUniformBlockIndex(this->prog, "object");
		glUniformBlockBinding(this->prog, object_block, static_cast<GLuint>(BlockIndices::Object));
	}

	void use(Mat4 const& transform, std::tuple<Uniforms...> const& uniforms) const {
		Shader<Uniforms...>::use(uniforms);
		glUniformMatrix4fv(transform_mat, 1, false, transform.cols->begin());
	}
};

template<class ...Uniforms>
struct TexShader;
struct Window;
struct Layer;

using RenderTarget = std::variant<std::reference_wrapper<Window>, std::reference_wrapper<Layer>>;

class Texture {
 private:
	Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, float* data, bool multisample);

 public:
	GLuint idx;
	bool multisample;
	GLint internalformat;
	GLenum format;

	Vec2 size;

	Window& wind;

	template<class TexShader>
	void proc(TexShader const& shad, Texture& out, typename TexShader::Tuple const& params) const;

	template<class TexShader>
	void render(RenderTarget layer, TexShader const& shad, typename TexShader::Tuple const& params) const;

	static GLint default_internalformat(GLenum format);

	Texture(Window& wind, GLenum format, Vec2 size, float* data);
	Texture(Window& wind, GLenum format, Vec2 size, bool multisample=false);
	Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, float* data);
	Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, bool multisample=false);
	Texture(Texture const& other);
	Texture(Texture&& other);
	~Texture();
};

template<class ...Uniforms>
struct TexShader: public Shader<Uniforms...> {
	GLint tex;

	TexShader(std::shared_ptr<const VertShader> vert, std::shared_ptr<const FragShader> frag, const char* uniform_names[]):
			Shader<Uniforms...>(vert, frag, uniform_names), tex(glGetUniformLocation(this->prog, "tex")) {}

	void use(Texture const& texture, std::tuple<Uniforms...> uniforms) const {
		Shader<Uniforms...>::use(uniforms);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(texture.multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, texture.idx);
		glUniform1i(tex, 0);
	}
};

struct FillShader: public ObjectShader<Vec4> {
	FillShader(): ObjectShader<Vec4>(std::shared_ptr<VertShader>(new VertShader(
#include "./include/fill.vert"
	)), std::shared_ptr<FragShader>(new FragShader(
#include "./include/fill.frag"
	)), (char const*[]){"color"}) {}
};

extern LazyInitialize<FillShader> fill_shader;

struct Layer;

struct Vertex {
	Vec3 pos;
	Vec3 normal;
	Vec2 texpos;
	Vec3 tangent;
};

struct Path {
	std::vector<Vec2> points;
	std::vector<GLuint> parts = {0};
	bool fill;
	bool closed;

	enum class Cap {
		Flat,
		Round,
		Square
	};

	enum class Join {
		Bevel,
		Miter,
		Round
	};

	Cap cap;
	Join join;
	float stroke_width;
	float miter_limit=4;

	void arc(float angle, Vec2 to, size_t divisions);
	void fan(Vec2 to, Vec2 center, size_t divisions);
	void cubic(Vec2 p2, Vec2 p3, Vec2 p4, float res);
	//merges other path into this path
	void merge(Path const& other);
	//expands path to stroke_width, sets fill=true and stroke_width=0
	Path stroke() const;

	std::vector<GLuint>::const_iterator part(GLuint i) const;
	GLuint next(std::vector<GLuint>::const_iterator part, GLuint i) const;
	GLuint prev(std::vector<GLuint>::const_iterator part, GLuint i) const;

 private:
	void stroke_side(Path& expanded, bool cw) const;
};

struct Geometry {
	std::vector<Vertex> vertices;
	std::vector<GLuint> elements;

	GLuint vbo;
	GLuint ebo;
	GLuint vao;

	Geometry(std::vector<Vertex> verts=std::vector<Vertex>(), std::vector<GLuint> elems=std::vector<GLuint>());
	Geometry(Geometry const& other);
	Geometry(Geometry&& other);
	explicit Geometry(Path const& path);
	void init(bool update_buf);
	void update_buffers();
	void triangulate(Path const& path);
	~Geometry();

	void render() const;
	//xor stencil buffer
	void render_stencil() const;
};

class Window {
 public:
	const char* name;
	SDL_Window* window;
	SDL_GLContext ctx;

	Vec2 bounds;

	struct Options {
		int x, y;
		int w, h;
		int fps;
		bool vsync;
		float mouse_sensitivity;
		int samples;

		Config cfg;

		Options(): x(SDL_WINDOWPOS_UNDEFINED), y(SDL_WINDOWPOS_UNDEFINED),
							 w(1024), h(512), fps(60), vsync(true), mouse_sensitivity(0.5), samples(4), cfg() {}

		bool load(const char* fname) {
			try {
				cfg.parse(read_file(fname));
			} catch (std::exception const& e) {
				return false;
			}

			if (cfg.map["x"]) x = static_cast<int>(std::get<long>(cfg.map["x"]->var));
			if (cfg.map["y"]) y = static_cast<int>(std::get<long>(cfg.map["y"]->var));
			if (cfg.map["w"]) w = static_cast<int>(std::get<long>(cfg.map["w"]->var));
			if (cfg.map["h"]) h = static_cast<int>(std::get<long>(cfg.map["h"]->var));
			if (cfg.map["fps"]) fps = static_cast<int>(std::get<long>(cfg.map["fps"]->var));
			if (cfg.map["samples"]) samples = static_cast<int>(std::get<long>(cfg.map["samples"]->var));
			if (cfg.map["vsync"]) vsync = std::get<bool>(cfg.map["vsync"]->var);
			if (cfg.map["sensitivity"]) mouse_sensitivity = std::get<float>(cfg.map["sensitivity"]->var);
			return true;
		}

		explicit Options(const char* fname): Options() {
			load(fname);
		}

		void save(const char* fname) const {
			std::ofstream f;
			f.open(fname);
			f << cfg.save().rdbuf();
		}
	};

	Options& opts;

	std::optional<TexShader<Mat3>> passthrough;
	std::optional<Geometry> full_rect;
	//LateInitialize<Geometry> full_rect;

	GLuint tex_fbo;
	std::optional<UniformBuffer<Mat4, Mat4>> object_ubo;
	Layer* in_use;

	bool swapped;

	void use();

	void update_transform_camera(Mat4 const& transform, Mat4 const& cam);

	Window(const char* name, Options& opts);
	Window(Window const& other) = delete;
	void swap();
	~Window();

 private:
	long fps_ticks;
	long last_swap;
};

template<class TexShader>
void Texture::proc(TexShader const& shad, Texture& out, typename TexShader::Tuple const& params) const {
	glBindFramebuffer(GL_FRAMEBUFFER, wind.tex_fbo);
	glViewport(0,0,static_cast<int>(out.size[0]),static_cast<int>(out.size[1]));

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, out.multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, out.idx, 0);
	gl_checkerr();

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("framebuffer not complete");

	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	shad.use(*this, params);
	wind.full_rect->render();

	gl_checkerr();
}

struct Layer {
	Mat4 layer_transform;
	Mat4 cam;
	UniformBuffer<Mat4, Mat4> object_ubo;

	bool multisample;
	GLuint multisamp_fbo;
	GLuint fbo;

	//if multisampling, textures are rendered here and blitted onto the other vector
	std::vector<Texture> multisample_channels;
	std::vector<Texture> channels;

	std::vector<GLenum> color_attachments;
	bool depth;

	Vec2 size;
	Window& wind;

	//clear on every swap
	bool cleared;

	Layer(Window& wind, Vec2 size, Mat4 layer_trans, Mat4 cam, bool multisample=true);
	Layer(Window& wind, Mat4 layer_trans, Mat4 cam, bool multisample=true);
	Layer(Layer const& other) = delete;
	void add_channel(GLenum format, GLint internalformat);
	void add_channel(GLenum format);
	void clear();
	Texture& channel(size_t i);
	void update_transform_camera();
	void update_size();
	void use();
	void finish();
};

template<class TexShader>
void Texture::render(RenderTarget target, TexShader const& shad, typename TexShader::Tuple const& params) const {
	std::visit([](auto x){x.get().use();}, target);
	shad.use(*this, params);
	wind.full_rect->render();
}

template<class ObjectShader>
struct Object {
	Window& wind;
	std::shared_ptr<Geometry> geo;
	Mat4 transform;

	ObjectShader& shad;
	typename ObjectShader::Tuple shader_params;

	void render(RenderTarget target) const {
		std::visit([](auto x){x.get().use();}, target);
		shad.use(transform, shader_params);
		geo->render();
	}
};

struct SVGObject {
	std::vector<Object<FillShader>> objs;

	SVGObject(Window& wind, char const* svg_data, float res);

	void render(RenderTarget target);
};

struct RenderToFile: public Layer {
	FILE* ffmpeg;
	std::vector<unsigned char> buf;
	unsigned fps;

	RenderToFile(Window& wind, Vec2 size, unsigned fps, char const* file, Mat4 layer_trans, Mat4 cam, bool multisample=true);
	//supports RGBA8, and only that xd
	void render_channel(size_t i);
	~RenderToFile();
};

#endif //SRC_GL_HPP_
