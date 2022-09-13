#include <new>

#include "graphics.hpp"
#include "arrayset.hpp"
#include "btree.hpp"

#include <algorithm>
#include <queue>

LazyInitialize<FillShader> fill_shader([](){return FillShader();});

void sdl_reporterr() {
	const char* err = SDL_GetError();
	if (err) throw SDLException(err);
}

void gl_checkerr() {
	GLenum err;
	while ((err = glGetError())) {
		switch (err) {
			case GL_INVALID_ENUM: throw GLException("GL_INVALID_ENUM");
			case GL_INVALID_VALUE: throw GLException("GL_INVALID_VALUE");
			case GL_INVALID_OPERATION: throw GLException("GL_INVALID_OPERATION");
			case GL_INVALID_FRAMEBUFFER_OPERATION: throw GLException("GL_INVALID_FRAMEBUFFER_OPERATION");
			case GL_OUT_OF_MEMORY: throw GLException("GL_OUT_OF_MEMORY");
			default:continue;
		}
	}
}

Window::Window(const char *name, Options& opts): swapped(false), opts(opts), name(name), in_use(nullptr) {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) sdl_reporterr();

	if (!(window = SDL_CreateWindow(
					name, opts.x, opts.y, opts.w, opts.h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN)))
					sdl_reporterr();

#ifndef GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

#ifdef MULTISAMPLE
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif
#endif

	if (!(ctx = SDL_GL_CreateContext(window))) sdl_reporterr();

	fps_ticks = SDL_GetPerformanceFrequency()/opts.fps;
	last_swap = 0;

#ifndef GLES
	SDL_GL_SetSwapInterval(opts.vsync ? 1 : 0);  // VSYNC

	// enable
#ifdef MULTISAMPLE
	glEnable(GL_MULTISAMPLE);
#endif
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	SDL_SetWindowSize(window, opts.w, opts.h);
	bounds = {(float)opts.w, (float)opts.h};

	glGenFramebuffers(1, &tex_fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, tex_fbo);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	passthrough.emplace(std::shared_ptr<VertShader>(new VertShader(
#include "./include/passthrough.vert"
	)), std::shared_ptr<FragShader>(new FragShader(
#include "./include/tex.frag"
	)), (char const*[]){"tex_transform"});

	full_rect.emplace(Path {.points={{-1,1}, {1,1}, {1,-1}, {-1,-1}}, .fill=true, .closed=true});

	object_ubo.emplace();
	object_ubo->set_data(std::tuple(Mat4(1), Mat4(1)));
	object_ubo->use(static_cast<GLint>(BlockIndices::Object));

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0,0, static_cast<int>(bounds[0]),static_cast<int>(bounds[1]));
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gl_checkerr();
}

void Window::clear() {
	cleared = swapped;
	if (in_use!=nullptr) use();

	glViewport(0,0, static_cast<int>(bounds[0]),static_cast<int>(bounds[1]));
	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl_checkerr();
}

void Window::delay() {
	Uint64 tick = SDL_GetPerformanceCounter();
	if (!opts.vsync && tick < fps_ticks + last_swap) {
		SDL_Delay((1000*(fps_ticks + last_swap - tick))/SDL_GetPerformanceFrequency());
	}

	last_swap = SDL_GetPerformanceCounter();
}

void Window::swap(bool do_delay) {
	if (do_delay) delay();

	use();
	glFinish();
	SDL_GL_SwapWindow(window);
	gl_checkerr();

	swapped = !swapped;
}

Window::~Window() {
//	if (!wrapper.moved) return;

	SDL_GetWindowPosition(window, &opts.x, &opts.y);
	SDL_GetWindowSize(window, &opts.w, &opts.h);

	opts.cfg.map.insert(std::string("x"), {.is_default=false, .var=Config::Variant(opts.x)});
	opts.cfg.map.insert(std::string("y"), {.is_default=false, .var=Config::Variant(opts.y)});
	opts.cfg.map.insert(std::string("w"), {.is_default=false, .var=Config::Variant(opts.w)});
	opts.cfg.map.insert(std::string("h"), {.is_default=false, .var=Config::Variant(opts.h)});

	glDeleteFramebuffers(1, &tex_fbo);

	SDL_GL_DeleteContext(ctx);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void Window::use() {
	if (in_use) {
		in_use->finish();
		glViewport(0,0, static_cast<int>(bounds[0]),static_cast<int>(bounds[1]));
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		object_ubo->use(static_cast<GLint>(BlockIndices::Object));
		in_use = nullptr;
	}

	if (cleared!=swapped) clear();
}

void Window::update_transform_camera(Mat4 const& trans, Mat4 const& cam) {
	object_ubo->set_data(std::tuple(trans, cam));
}

#ifndef GLES
Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data, bool multisample): wind(wind), format(format), size(size), multisample(multisample) {
#else
Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data, bool multisample): wind(wind), format(format), size(size) {
#endif
	glGenTextures(1, &idx);

#ifndef GLES
	if (multisample) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, idx);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, wind.opts.samples, format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), GL_FALSE);
	} else {
#endif
		glBindTexture(GL_TEXTURE_2D, idx);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, format.format, format.type, data);
#ifndef GLES
	}
#endif

	gl_checkerr();
}

Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data):
		Texture(wind, format, size, data, false) {}
Texture::Texture(Window& wind, TexFormat format, Vec2 size, bool multisample):
		Texture(wind, format, size, nullptr, multisample) {}

#ifndef GLES
Texture::Texture(Texture const& other): Texture(other.wind, other.format, other.size, other.multisample) {
#else
Texture::Texture(Texture const& other): Texture(other.wind, other.format, other.size, false) {
#endif
	other.proc(*wind.passthrough, *this, std::tuple(Mat3(1)));
}

#ifndef GLES
Texture::Texture(Texture&& other): wind(other.wind), multisample(other.multisample), format(other.format), size(other.size), idx(other.idx) {
#else
Texture::Texture(Texture&& other): wind(other.wind), format(other.format), size(other.size), idx(other.idx) {
#endif
	other.idx=-1;
}

Texture::~Texture() {
	if (idx!=-1) glDeleteTextures(1, &idx);
}

Layer::Layer(Window& wind, Vec2 size, Mat4 layer_trans, Mat4 cam, bool multisample):
		wind(wind), cleared(!wind.swapped), size(size), depth(false), layer_transform(layer_trans), cam(cam), object_ubo()
#ifdef MULTISAMPLE
	, multisample(multisample) {

	if (multisample) {
		glGenFramebuffers(1, &multisamp_fbo);

		glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glCullFace(GL_BACK);
	}
#else
	{
#endif

	glGenFramebuffers(1, &fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glCullFace(GL_BACK);

	object_ubo.set_data(std::tuple(layer_trans, cam));

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	gl_checkerr();
}

Layer::Layer(Window& wind, Mat4 layer_trans, Mat4 cam, bool multisample):
	Layer(wind, wind.bounds, layer_trans, cam, multisample) {}

void Layer::add_channel(TexFormat format) {
	GLenum attach;
	if (format.format==GL_DEPTH_COMPONENT) {
		attach = GL_DEPTH_ATTACHMENT;
		depth=true;
	} else {
		attach = GL_COLOR_ATTACHMENT0 + channels.size();
		color_attachments.push_back(attach);
	}

#ifdef MULTISAMPLE
	if (multisample) {
		GLuint renderbuf;
		glGenRenderbuffers(1, &renderbuf);
		glBindRenderbuffer(GL_RENDERBUFFER, renderbuf);
		multisample_renderbufs.push_back(renderbuf);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, wind.opts.samples, format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]));

		glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attach, GL_RENDERBUFFER, renderbuf);

		if (attach!=GL_DEPTH_ATTACHMENT) {
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				throw std::runtime_error("framebuffer not complete");
		}
	}
#endif

	channels.emplace_back(wind, format, size, false);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, channels.back().idx, 0);

	if (attach!=GL_DEPTH_ATTACHMENT) {
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			throw std::runtime_error("framebuffer not complete");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	gl_checkerr();
}

void Layer::update_transform_camera() {
	object_ubo.set_data(std::tuple(layer_transform, cam));
}

void Layer::clear() {
	cleared = wind.swapped;
	if (wind.in_use!=this) use();
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Layer::use() {
	if (wind.in_use != this) {
		if (wind.in_use) {
			wind.in_use->finish();
		}

		wind.in_use = this;

#ifdef MULTISAMPLE
		if (multisample) glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo); else
#endif
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		object_ubo.use(static_cast<GLint>(BlockIndices::Object));

		glDrawBuffers(color_attachments.size(), color_attachments.data());

		glViewport(0,0, static_cast<int>(size[0]),static_cast<int>(size[1]));

		if (cleared!=wind.swapped) clear();
	}
}

void Layer::update_size() {
#ifdef MULTISAMPLE
	for (unsigned i=0; i<channels.size(); i++) {
		glBindRenderbuffer(GL_RENDERBUFFER, multisample_renderbufs[i]);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, wind.opts.samples, channels[i].format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]));
	}
#endif

	for (auto& chan: channels) {
		glBindTexture(GL_TEXTURE_2D, chan.idx);
		glTexImage2D(GL_TEXTURE_2D, 0, chan.format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, chan.format.format, chan.format.type, nullptr);
	}
}

void Layer::finish() {
#ifdef MULTISAMPLE
	if (multisample) {
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, multisamp_fbo);

		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		for (GLenum attach: color_attachments) {
			glReadBuffer(attach);
			glDrawBuffers(1, (GLenum[]){attach});

			glBlitFramebuffer(0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), 0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}

		if (depth) {
			glBlitFramebuffer(0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), 0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
	}
#endif

	gl_checkerr();
}

Texture& Layer::channel(size_t i) {
	if (wind.in_use==this) wind.use();
	return channels[i];
}

void Geometry::init(bool update_buf) {
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	if (update_buf) glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);

	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	if (update_buf) glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(elements.size() * sizeof(GLuint)), elements.data(), GL_DYNAMIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texpos));
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
	glEnableVertexAttribArray(3);

	gl_checkerr();
}

Geometry::Geometry(std::vector<Vertex> verts, std::vector<GLuint> elems) : vertices(std::move(verts)), elements(std::move(elems)) {
	init(true);
}

void Geometry::update_buffers() {
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(elements.size() * sizeof(GLuint)), elements.data(), GL_DYNAMIC_DRAW);
}

void Geometry::render() const {
	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, elements.size(), GL_UNSIGNED_INT, nullptr);
}

Geometry::Geometry(Geometry const& other): Geometry(other.vertices, other.elements) {}

Geometry::Geometry(Geometry&& other): vertices(other.vertices), elements(other.elements), vao(other.vao), vbo(other.vbo), ebo(other.ebo) {
	other.vao=-1;
}

Geometry::Geometry(Path const& path): vertices(), elements() {
	init(false);
	triangulate(path);
}

bool is_crossing(Vec2 const& p1, Vec2 const& p2, std::vector<std::reference_wrapper<const Path>> const& others) {
	Vec2 off = p2-p1;

	size_t num_crossings=0;
	for (auto other: others) {
		Path const& other_ref = other.get();

		for (size_t part=0; part<other_ref.parts.size(); part++) {
			size_t lim = part+1==other_ref.parts.size() ? other_ref.points.size() : other_ref.parts[part+1];

			for (size_t i=other_ref.parts[part]; i<lim; i++) {
				Vec2 off2 = other_ref.points[i+1==lim ? other_ref.parts[part] : i+1] - other_ref.points[i];
				if (off2.abs()<Epsilon) continue;

				std::optional<Vec2::Intersection> crossing = Vec2::intersect(p1, off, other_ref.points[i], off2);

				if (crossing && crossing->in_segment_no_endpoints()) {
					 return true;
				} else if (crossing && crossing->c1>0 && crossing->c2==0) {
					 Vec2 off_before = other_ref.points[i==other_ref.parts[part] ? lim : i-1]-other_ref.points[i];
					 for (size_t i_minus=2; off_before.abs()<Epsilon && i_minus<other_ref.points.size(); i_minus++)
							 off_before = other_ref.points[i] - other_ref.points[i_minus+other_ref.parts[part]>=i ? lim-i_minus+i : i-i_minus];

					 if (std::signbit(off_before.determinant(off))!=std::signbit(off2.determinant(off))) {
							 if (crossing->c1<1) return true;
							 num_crossings++;
					 }
				} else if (crossing && crossing->c1>0 && crossing->c2>0 && crossing->c2<1) {
					 num_crossings++;
				}
			}
		}
	}

	return num_crossings%2==0;
}

struct SweepEdge;
using EdgeNode = Node<float, SweepEdge, std::greater<float>>;

struct Containment {
	EdgeNode* edge;
	unsigned chain_i;
};

struct MonotonePolygon {
	std::vector<std::vector<GLuint>> chain;
	unsigned left, right;

	std::vector<Containment> container;
};

struct SweepEdge {
	GLuint from;
	GLuint to;
	bool left;

	std::unique_ptr<MonotonePolygon> poly;
	EdgeNode* other;
};

void Geometry::triangulate(Path const& path) {
	Path stroked;
	if (!path.fill) stroked = path.stroke();
	Path const& path_ref = path.fill ? path : stroked;
	std::vector<Vec2> const& p = path_ref.points;

	auto elem_offset = static_cast<GLuint>(vertices.size());

	vertices.reserve(vertices.size()+p.size());

	for (Vec2 const& point: p) {
		vertices.push_back(Vertex {.pos=Vec3({point[0], point[1], 0}), .normal={0,0,1}});
	}

	std::vector<GLuint> sorted;
	sorted.reserve(p.size());
	for (GLuint i = 0; i<p.size(); i++) {
		if (std::isnan(p[i][0]) || std::isnan(p[i][1])) continue;
		sorted.push_back(i);
	}

	std::sort(sorted.begin(), sorted.end(), [&](GLuint const& i1, GLuint const& i2){
		return (p[i1][1] > p[i2][1])
				|| (p[i1][1]==p[i2][1] && p[i1][0] > p[i2][0]);
	});

	EdgeNode::Root edges;
	EdgeNode::Root edges_new;

	//so i can copy-paste into geogebra 😜
	std::cout<<std::endl;
	for (Vec2 const& pt: p) {
		std::cout << "(" << pt[0] << ", " << pt[1] << "), ";
	}
	std::cout<<std::endl;

	auto triangulate_chain = [&](std::vector<GLuint>* chain, std::vector<GLuint>* other_chain, GLuint* split) -> std::pair<std::vector<GLuint>*, std::vector<GLuint>::iterator>  {
		auto iter = chain->rbegin();
		auto other_iter = chain->rbegin();
		bool iter_left=true;
		bool switched=false;

		while (iter!=chain->rend() || other_iter!=other_chain->rend()) {
			//...
			while (iter!=chain->rend() &&
					((other_iter==other_chain->rend()) || (p[*iter][1]<p[*other_iter][1]
							|| (p[*iter][1]==p[*other_iter][1] && p[*iter][0]<=p[*other_iter][0])))) {

				if (split && (p[*split][1]<p[*iter][1]
							|| (p[*split][1]==p[*iter][1] && p[*split][0]<=p[*iter][0]))) {
					return std::make_pair(chain, iter.base()-1);
				} else if (iter-chain->rbegin()>1 || (!switched && iter!=chain->rbegin())) {
					auto iter_cpy = iter-1;
					while (iter_cpy!=chain->rbegin()) {
						float d = (p[*iter_cpy]-p[*(iter_cpy-1)]).determinant(p[*iter]-p[*(iter_cpy-1)]);

						//not worth adding lmao why tf u give me such bad polys
//						if (std::abs(d)<Epsilon) {
//							long diff = iter-iter_cpy;
//							iter_cpy = std::make_reverse_iterator(chain->erase(iter_cpy.base()-1))-1;
//							iter = iter_cpy+diff;
//						} else
						if ((iter_left && d<0) || (!iter_left && d>0)) {
							elements.insert(elements.end(), {elem_offset+*iter, elem_offset+*iter_cpy, elem_offset+*(iter_cpy-1)});
							long diff = iter-iter_cpy;
							iter_cpy = std::make_reverse_iterator(chain->erase(iter_cpy.base()-1))-1;
							iter = iter_cpy+diff;
						} else {
							break;
						}
					}

					if (iter_cpy==chain->rbegin()) {
						float d = (p[*(iter_cpy+1)]-p[*(iter_cpy)]).determinant(p[*iter]-p[other_chain->back()]);
						if ((iter_left && d<0) || (!iter_left && d>0)) {
							elements.insert(elements.end(), {elem_offset+chain->back(), elem_offset+*iter, elem_offset+other_chain->back()});
							chain->pop_back();
							iter = chain->rbegin();
						}
					}
				} else if (switched) {
					//clunky but needed conditionals (i thought about this shit for an hour or something, don't try it again!) for handling start/end conditions when the vertex that should be handled next is not part of a triangle here
					if (*iter!=chain->back() && other_chain->back()!=*iter)
						elements.insert(elements.end(), {elem_offset+chain->back(), elem_offset+*iter, elem_offset+other_chain->back()});

					for (auto other_iter_cpy=other_chain->rbegin()+1; other_iter_cpy<other_iter && other_iter_cpy<other_chain->rend()-1; other_iter_cpy++) {
						elements.insert(elements.end(), {elem_offset+*other_iter_cpy, elem_offset+*iter, elem_offset+*(other_iter_cpy-1)});
					}

					if (other_iter>other_chain->rbegin()+1) {
						other_chain->erase(other_iter.base()+1, other_chain->end());
						other_iter = other_chain->rbegin()+1;
					}

					if (*iter!=chain->back()) chain->pop_back();
					iter = chain->rbegin();
					switched=false;
				}

				iter++;
			}

			std::swap(other_iter, iter);
			std::swap(chain, other_chain);
			iter_left = !iter_left;
			switched=true;
		}

		return std::make_pair(nullptr, chain->begin());
	};

	const auto cmp_chain = [&](std::vector<GLuint>* const& chain1, std::vector<GLuint>* const& chain2) {
		return p[chain2->back()][1]<p[chain1->back()][1] || (p[chain2->back()][1]==p[chain1->back()][1] && p[chain2->back()][0]<=p[chain1->back()][0]);
	};

	auto triangulate_monotone = [&](MonotonePolygon& poly) {
		std::priority_queue<std::vector<GLuint>*, std::vector<std::vector<GLuint>*>, decltype(cmp_chain)> sorted_chains(cmp_chain);
		std::vector<std::vector<GLuint>*> other_chain;
		other_chain.insert(other_chain.end(),poly.chain.size(),nullptr);

		for (auto chain=poly.chain.begin(); chain!=poly.chain.end(); chain++) {
			sorted_chains.push(&*chain);
		}

		std::vector<GLuint>* cbegin = poly.chain.data();

		while (!sorted_chains.empty()) {
			auto top = sorted_chains.top();
			sorted_chains.pop();

			auto& other = other_chain[top-cbegin];
			if (!other) {
				other = sorted_chains.top();
				other_chain[other-cbegin] = top;
			}

			bool reins_other = sorted_chains.top()==other;
			if (reins_other) sorted_chains.pop();
			GLuint* split = sorted_chains.empty() ? nullptr : &sorted_chains.top()->back();

			if (other->size()==0 || top->size()==0) {

			}
			
			auto ret = triangulate_chain(top, other, split);
			auto split_chain = ret.first;

			if (split_chain) {
				auto schain = sorted_chains.top();

				std::vector<GLuint>* min = std::min(top, other);
				std::vector<GLuint>* max = std::max(top, other);

				if (schain<max && schain>min) {
					sorted_chains.pop();
					auto other_sc = sorted_chains.top();
					sorted_chains.pop();

					bool par = (schain-cbegin) % 2 == 0;
					bool is_sc = (split_chain==max && par) || (split_chain==min && !par);
					auto c = is_sc ? schain : other_sc;
					auto oc = is_sc ? other_sc : schain;

					c->insert(c->end(), ret.second, split_chain->end());
					split_chain->erase(ret.second+1, split_chain->end());
					oc->push_back(*ret.second);

					//i dont even know
					other_chain[c-cbegin] = split_chain;
					other_chain[oc-cbegin] = other_chain[split_chain-cbegin];
					other_chain[split_chain-cbegin] = c;
					other_chain[other_chain[split_chain-cbegin]-cbegin] = oc;
					
					sorted_chains.push(c);
					sorted_chains.push(oc);
				}

				sorted_chains.push(top);
				if (reins_other) sorted_chains.push(other);
			}
		}
	};

	auto getx = [&](SweepEdge& edge, size_t i) -> float {
		float y = p[sorted[i]][1];
		float dy = p[edge.from][1]-p[edge.to][1];

		if ((std::abs(dy)>0 || p[sorted[i]][0]>p[edge.to][0]) && y==p[edge.from][1]) return p[edge.from][0];
		else if (y==p[edge.to][1]) return p[edge.to][0];

		return dy==0 ? p[edge.to][0] : p[edge.from][0]+(p[edge.to][0]-p[edge.from][0])*(p[edge.from][1]-y)/dy;
	};

	for (size_t i=0; i<sorted.size(); i++) {
		auto part = path_ref.part(sorted[i]);
		std::array<GLuint, 2> adj = {path_ref.prev(part, sorted[i]), path_ref.next(part, sorted[i])};

		int v=0;
		for (EdgeNode::Iterator iter=edges.begin(); iter!=edges.end();) {
			EdgeNode& edge = *iter;

			if (edge.v.left) {
				v++;
				std::cout << "(";
			} else {
				v--;
				std::cout << ")";
			}

			assert(v>=0);

			float new_k = getx(edge.v, i);

			std::unique_ptr<EdgeNode> node = iter.consume();
			node->x = new_k;
			edges_new.insert_node(std::move(node));
		}

		edges.swap(edges_new);

		assert(v==0);
		std::cout << std::endl;

		std::cout << " -> ";
		Map<EdgeNode*, std::monostate> containing;
		for (EdgeNode& edge: edges) {
			if (edge.v.poly) {
				std::vector<Containment>& container = edge.v.poly->container;
				for (auto iter=container.begin(); iter!=container.end(); iter++) {
					if (!containing[iter->edge]) iter = container.erase(iter)-1;
				}
			}

			if (edge.v.left) {
				v++;
				containing.insert(&edge, std::monostate());
				std::cout << "(";
			} else {
				v--;
				EdgeNode* ptr = &edge;
				containing.remove(ptr);
				std::cout << ")";
			}

			assert(v>=0);
		}

		assert(v==0);
		std::cout << std::endl;

		EdgeNode* node = edges.find(p[sorted[i]][0]);

		{
			EdgeNode::Iterator iter=edges.iter_ref(node);
			for (; iter!=edges.end() && iter->v.to!=sorted[i]; ++iter);
			node=iter==edges.end() ? nullptr : &*iter;
		}

		if (adj[1]==adj[0]) continue; //sanity check, in case not a triangle i guess

		bool l1 = p[adj[0]][1] < p[sorted[i]][1] || (p[adj[0]][1]==p[sorted[i]][1] && p[adj[0]][0] < p[sorted[i]][0]);
		bool l2 = p[adj[1]][1] < p[sorted[i]][1] || (p[adj[1]][1]==p[sorted[i]][1] && p[adj[1]][0] < p[sorted[i]][0]);

		if (!node) {
			bool swap = p[adj[1]][1]==p[adj[0]][1] ? p[adj[0]][0] < p[adj[1]][0] : (p[adj[0]]-p[sorted[i]]).determinant(p[adj[1]]-p[sorted[i]])>0;

			EdgeNode::Iterator iter = edges.iter_ref(node);
			std::vector<Containment> container;

			if (iter!=edges.end()) {
				 int v=0;
				 do {
					 if (iter->v.left && v==0) {
						 container.push_back(Containment {.edge=&*iter, .chain_i=iter->v.poly->left+1});
					 } else if (iter->v.left) {
						 v--;
					 } else {
						 v++;
					 }
				 } while (iter!=edges.begin());
			}

			auto poly = new MonotonePolygon {.chain={{sorted[i]}, {sorted[i]}}, .left=0, .right=1, .container=std::move(container)};

			GLuint first = swap ? adj[1] : adj[0], second=swap ? adj[0] : adj[1];

			EdgeNode* np = edges.insert(static_cast<float&&>(p[sorted[i]][0]), SweepEdge {
					.from=sorted[i], .to=first, .left=true, .poly=std::unique_ptr<MonotonePolygon>(poly)
			});

			np->v.other = edges.insert(static_cast<float&&>(p[sorted[i]][0]), SweepEdge {
					.from=sorted[i], .to=second, .left=false, .other=np
			});
		} else if (node) {
			SweepEdge& edge = node->v;

			if (l1 || l2) {
				if (edge.left) {
					edge.poly->chain[edge.poly->left].push_back(sorted[i]);
				} else {
					auto& poly = *edge.other->v.poly;
					poly.chain[poly.right].push_back(sorted[i]);
				}

				if (l1) {
					edge.from = sorted[i];
					edge.to = adj[0];
				} else {
					edge.from = sorted[i];
					edge.to = adj[1];
				}
			} else {
				auto& poly = edge.poly ? *edge.poly : *edge.other->v.poly;
				EdgeNode::Iterator iter = edges.iter_ref(node);
				do {++iter;} while (iter->v.to!=sorted[i]);

				auto update_containers = [&](EdgeNode* contained_in, EdgeNode::Iterator iter2, EdgeNode* end, EdgeNode* new_edge, unsigned chain_off) {
					for (; iter2.current!=end; ++iter2) {
						if (iter2->v.left) {
							auto contains = std::find_if(iter2->v.poly->container.begin(), iter2->v.poly->container.end(),
							                             [&](Containment const& c) {return c.edge==contained_in;});
							if (contains==iter2->v.poly->container.end()) continue;

							if (new_edge) {
								contains->edge = new_edge;
								contains->chain_i += chain_off;
							} else {
								iter2->v.poly->container.erase(contains);
							}
						}
					}
				};

				//)(
				if (!edge.left && iter->v.left && iter->v.poly.get()!=&poly) {
					auto& poly2 = *iter->v.poly;

					poly.chain[poly.right].push_back(sorted[i]);
					poly2.chain[poly2.left].push_back(sorted[i]);

					unsigned nr = poly.chain.size() + poly2.right;
					unsigned prev_r = poly.right+1;
					poly.chain.insert(poly.chain.begin()+poly.right+1, std::move_iterator(poly2.chain.begin()), std::move_iterator(poly2.chain.end()));
					poly.right = nr;

					auto& poly2_edge = *iter;
					++iter;
					update_containers(&poly2_edge, iter, poly2_edge.v.other, edge.other, prev_r);

					poly2_edge.v.other->v.other = edge.other;
					edge.other->v.other = poly2_edge.v.other;

					for (auto const& containment: poly2.container) {
						containing.insert(containment.edge, std::monostate());
					}

					for (auto containment=poly.container.begin(); containment!=poly.container.end(); containment++) {
						if (!containing[containment->edge]) containment = poly.container.erase(containment)-1;
					}

					--iter;
					//(( or ))
				} else if ((iter->v.poly && iter->v.poly.get()!=&poly) || (iter->v.other->v.poly.get()!=&poly)) {
					if (!iter->v.poly) iter = edges.iter_ref(iter->v.other);

					auto& poly2 = *iter->v.poly;
					auto containment = std::find_if(poly2.container.begin(), poly2.container.end(),
					                                [&](Containment const& c) {return c.edge==node;});

					//((
					if (edge.left && containment!=poly2.container.end()) {
						poly.chain[poly.left].push_back(sorted[i]);
						poly2.chain[poly2.left].push_back(sorted[i]);

						poly.chain.insert(poly.chain.begin()+containment->chain_i, std::move_iterator(poly2.chain.begin()), std::move_iterator(poly2.chain.end()));

						EdgeNode::Iterator poly2_other = edges.iter_ref(iter->v.other);
						poly2_other->v.poly.swap(edge.poly);
						poly2_other->v.left = true;

						++poly2_other;

						update_containers(node, poly2_other, edge.other, &*poly2_other, poly2.chain.size());
						//shouldnt be anything here unless degenerate, but i dont want this to crash bc of ur terrible polys

						auto& poly2_edge = *iter;
						update_containers(node, edges.iter_ref(node)+1, &poly2_edge, &*poly2_other, 0);

						++iter;
						update_containers(&poly2_edge, iter, poly2_edge.v.other, nullptr, 0);

						poly.left = containment->chain_i+poly2.right;
					//))
					} else {
						containment = std::find_if(poly.container.begin(), poly.container.end(),
						                           [&](Containment const& c) {return c.edge==&*iter;});

						if (!edge.left && containment!=poly.container.end()) {
							poly.chain[poly.right].push_back(sorted[i]);
							poly2.chain[poly2.right].push_back(sorted[i]);

							poly2.chain.insert(poly2.chain.begin()+containment->chain_i, std::move_iterator(poly.chain.begin()), std::move_iterator(poly.chain.end()));

							auto& poly2_edge = *iter;
							EdgeNode::Iterator poly2_other = edges.iter_ref(poly2_edge.v.other);

							update_containers(node, edges.iter_ref(node)+1, &poly2_edge, &*poly2_other, poly.chain.size());

							update_containers(edge.other, edges.iter_ref(edge.other)+1, node, nullptr, 0);

							edge.other->v.left = false;
							edge.other->v.other=&*poly2_other;

							poly2.right = containment->chain_i+poly.left;

						//fuked up / self-intersecting
						} else {
							//discard both, for now

							auto pleft = edge.left ? node : edge.other;
							update_containers(pleft, edges.iter_ref(pleft)+1, pleft->v.other, nullptr, 0);
							auto p2left = iter->v.left ? &*iter : iter->v.other;
							update_containers(p2left, edges.iter_ref(p2left)+1, p2left->v.other, nullptr, 0);

							iter->v.other->remove();
							edge.other->remove();
						}
					}
					//normal end
				} else {
					poly.chain[poly.left].push_back(sorted[i]);
					poly.chain[poly.right].push_back(sorted[i]);

					auto pleft = edge.left ? node : edge.other;
					update_containers(pleft, edges.iter_ref(pleft)+1, pleft->v.other, nullptr, 0);

					if (poly.container.empty()) {
						triangulate_monotone(poly);
					} else {
						auto containment = poly.container.front();
						auto& outside_poly = *containment.edge->v.poly;

						outside_poly.chain.insert(outside_poly.chain.begin()+containment.chain_i, std::move_iterator(poly.chain.begin()), std::move_iterator(poly.chain.end()));
					}
				}

				node->remove();
				iter->remove();
			}
		}
	}

	update_buffers();
}

Geometry::~Geometry() {
	if (vao==-1) return;
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(2, (GLuint[]){vbo, ebo});
}

void Geometry::render_stencil() const {

}

void Path::merge(Path const& other) {
	if (!points.empty()) {
		auto part = parts.insert(parts.end(), other.parts.begin(), other.parts.end());
		for (;part!=parts.end(); part++) {
			*part += points.size();
		}
	}

	points.insert(points.end(), other.points.begin(), other.points.end());
}

void Path::split(size_t part, GLuint i1, GLuint i2) {
	if (i1>i2) std::swap(i1, i2);

	size_t rest = parts[part]-i2;

	auto iter = points.insert(points.begin()+i1+1, points[i2]);
	std::rotate(iter+1, points.begin()+i2+2, points.begin() + parts[part] + 1);

	points.insert(iter+rest-1, points[i1]);
	for (size_t i=part; i<parts.size(); i++) parts[part]+=2;
	parts.insert(parts.begin()+part, i1+rest);
}

void Path::arc(float angle, Vec2 to, size_t divisions) {
	Vec2 offset = to-points.back();

	Vec2 rot_mat {std::cosf(angle/static_cast<float>(divisions)), std::sinf(angle/static_cast<float>(divisions))};
	float h = std::abs(std::sinf(angle/2)*2);
	float d = std::abs(std::cosf(angle/2));
	float onorm = offset.norm();
	float r = onorm/h;

	Vec2 center = points.back() + offset/2 + (angle>0 ? offset.perpendicular(1,0) : offset.perpendicular(0,1))*(d*r/onorm);
	Vec2 center_offset = points.back() - center;

	for (size_t i=0; i<divisions; i++) {
		center_offset = center_offset.rotate_by(rot_mat);
		center_offset = center_offset.normalize(r);
		points.emplace_back(center + center_offset);
	}
}

void Path::fan(Vec2 to, Vec2 center, size_t divisions) {
	Vec2 offset = to-center;
	Vec2 prev_offset = points.back()-center;

	float onorm = offset.norm();
	float pnorm = prev_offset.norm();
	float dot = prev_offset.dot(offset)/pnorm;
	float cos = dot/onorm;
	float sin = offset.determinant(prev_offset)/(onorm*pnorm);

	Vec2 perp = (offset - prev_offset*dot/pnorm)/sin;

	float angle = std::asin(sin);
	if (sin<0 != cos<0) angle = M_PI - angle;

	Vec2 center_offset {1,0};

	Vec2 rot_mat {std::cosf(angle/static_cast<float>(divisions)), std::sinf(angle/static_cast<float>(divisions))};
	for (size_t i=0; i<divisions; i++) {
		center_offset = center_offset.rotate_by(rot_mat).normalize(1);
		points.emplace_back(center + prev_offset*center_offset[0] + perp*center_offset[1]);
	}
}

void Path::cubic(Vec2 p2, Vec2 p3, Vec2 p4, float res) {
	Vec2 start = points.back();

	float d3 = (-start*6-p2*2+p3*2+p4*6).norm();

	float d;
	for (float x=0; x<1; x+=d) {
		float xsq = x*x, ixsq=(1-x)*(1-x);
		float w1=(1-x)*ixsq, w2=3*x*ixsq, w3=3*(1-x)*xsq, w4=xsq*x;

		points.emplace_back(start*w1+p2*w2+p3*w3+p4*w4);

		float dw1=6-6*x, dw2=18*x-12, dw3=6-18*x, dw4=6*x;
		float curvature_sq = (start*dw1+p2*dw2+p3*dw3+p4*dw4).norm_sq();
		d = 1.0f/(res*(d3+curvature_sq));
	}

	points.push_back(p4);
}

void Path::stroke_side(size_t part, Path& expanded, bool cw) const {
	Vec2 off;

	GLuint lim = part+1==parts.size() ? points.size() : parts[part + 1];
	GLuint n = lim-parts[part];
	if (n==1) return;

	for (GLuint i = cw ? parts[part] : lim-1; cw ? i< lim : i!=parts[part]-1; cw ? i++ : i--) {
		 if ((i>parts[part] && i<lim-1) || closed) {
			 Vec2 diff = (points[i]-points[i==parts[part] ? lim-1 : i-1]);
			 for (GLuint i_minus=2; diff>-Epsilon && diff<Epsilon && i_minus<n; i_minus++)
					diff = points[i] - points[i_minus+parts[part]>=i ? lim-i_minus+i : i-i_minus];
			 diff=diff.normalize(1);

			 Vec2 diff2 = points[i+1==lim ? parts[part] : i+1] - points[i];
			 for (GLuint i_plus=2; diff2>-Epsilon && diff2<Epsilon && i_plus<n; i_plus++)
					diff2 = points[parts[part] + ((i+i_plus-parts[part])%n)] - points[i];
			 diff2=diff2.normalize(1);

			 Vec2 avg = (diff2-diff).normalize(1);
			 float slope = diff.determinant(avg);

			 bool off_sign = cw ? slope<=0 : slope>=0;

			 if (join==Join::Round && off_sign) {
				 expanded.points.emplace_back(points[i] + (cw ? diff : diff2).perpendicular(cw, !cw)*stroke_width);
				 off = (cw ? diff2 : diff).perpendicular(cw, !cw)*stroke_width;
				 expanded.fan(points[i] + off, points[i], 15);
				 expanded.points.emplace_back(points[i] + off);
			 } else if ((join==Join::Bevel || std::abs(1/slope)>miter_limit) && off_sign) {
				 expanded.points.emplace_back(points[i] + (cw ? diff : diff2).perpendicular(cw, !cw)*stroke_width);
				 off = (cw ? diff2 : diff).perpendicular(cw, !cw)*stroke_width;
				 expanded.points.emplace_back(points[i] + off);
			 } else {
				 off = avg*(off_sign ? -std::abs(stroke_width/slope) : std::abs(stroke_width/slope));
				 expanded.points.emplace_back(points[i] + off);
			 }
		 } else if (i==parts[part]) {
			 if (cap==Cap::Round && !cw) continue;

			 Vec2 diff = points[i+1]-points[i];
			 off = diff.perpendicular(cw, !cw).normalize(stroke_width);
			 if (cap==Cap::Square) off -= diff.normalize(stroke_width);

			 if (cap==Cap::Round) {
				 expanded.points.emplace_back(points[i]-off);
				 expanded.arc(M_PI, points[i] + off, 15);
			 } else {
				 expanded.points.emplace_back(points[i] + off);
			 }
		 } else {
			 if (cap==Cap::Round && !cw) continue;

			 Vec2 diff = points[i]-points[i-1];
			 off = diff.perpendicular(cw, !cw).normalize(stroke_width);
			 if (cap==Cap::Square) off += diff.normalize(stroke_width);

			 if (cap==Cap::Round) {
				 expanded.points.emplace_back(points[i]+off);
				 expanded.arc(M_PI, points[i]-off, 15);
			 } else {
				 expanded.points.emplace_back(points[i] + off);
			 }
		 }
	}
}

Path Path::stroke() const {
	Path expanded = {.points=std::vector<Vec2>(), .parts={}, .fill=true, .closed=true, .stroke_width=0};

	for (size_t part=0; part<parts.size(); part++) {
		expanded.parts.push_back(expanded.points.size());
		stroke_side(part, expanded, true);
		if (closed) expanded.parts.push_back(expanded.points.size());
		stroke_side(part, expanded, false);
	}

	return expanded;
}

//really dumb stupid head functions which don't even save me any tpying!??
std::vector<GLuint>::const_iterator Path::part(GLuint i) const {
	auto part = std::adjacent_find(parts.begin(), parts.end(), [=](size_t part1, size_t part2){return part1 <= i && i < part2;});
	if (part==parts.end()) part--;
	return part;
}

GLuint Path::next(std::vector<GLuint>::const_iterator part, GLuint i) const {
	if (i+1 == (part==parts.end()-1 ? points.size() : *(part+1))) return *part;
	else return i+1;
}

GLuint Path::prev(std::vector<GLuint>::const_iterator part, GLuint i) const {
	if (i == *part) return part==parts.end()-1 ? points.size()-1 : *(part+1)-1;
	else return i-1;
}

SVGObject::SVGObject(Window& wind, char const* svg_data, float res) : objs() {
	char* str_copy = new char[strlen(svg_data)+1];
	std::copy(svg_data, svg_data+strlen(svg_data)+1, str_copy);

	NSVGimage* image = nsvgParse(str_copy, "px", 96);

	for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
		std::shared_ptr<Geometry> g(new Geometry());
		Vec2 center {shape->bounds[0]+shape->bounds[2], shape->bounds[1]+shape->bounds[3]};
		center/=2;
		objs.push_back(Object<FillShader> {.wind=wind, .geo=g, .transform=Mat4(1), .shad=*fill_shader});
		Object<FillShader>& obj = objs.back();
		obj.transform[3][0] += center[0];
		obj.transform[3][1] += center[1];
		
		obj.shader_params = Vec4 {static_cast<float>(shape->fill.color & UCHAR_MAX)/UCHAR_MAX, static_cast<float>((shape->fill.color>>8) & UCHAR_MAX)/UCHAR_MAX, static_cast<float>((shape->fill.color>>16) & UCHAR_MAX)/UCHAR_MAX, shape->opacity};

		Path bigpath;
		bigpath.closed=true;
		bigpath.fill=true;

		Path path;
		for (NSVGpath* svgpath = shape->paths; svgpath != nullptr; svgpath = svgpath->next) {
			path.points.clear();

			for (int i = 0; i < svgpath->npts; i+=3) {
				float* p = &svgpath->pts[i*2];
				if (i==0) {
					path.points.emplace_back(Vec2 {p[0], p[1]}-center);
					i-=2; continue;
				}

				path.cubic(Vec2 {p[0], p[1]}-center, Vec2 {p[2],p[3]}-center, Vec2 {p[4], p[5]}-center, res);
			}

			path.closed = static_cast<bool>(svgpath->closed);
			if (shape->opacity>0 && shape->fill.type!=NSVG_PAINT_NONE) {
				path.fill=true;
				bigpath.merge(path);
			}

			if (shape->strokeWidth>0 && shape->stroke.type!=NSVG_PAINT_NONE) {
				path.fill=false;
				path.stroke_width = shape->strokeWidth;
				path.miter_limit = shape->miterLimit;

				switch (shape->strokeLineCap) {
					case NSVG_CAP_BUTT: path.cap = Path::Cap::Flat; break;
					case NSVG_CAP_ROUND: path.cap = Path::Cap::Round; break;
					case NSVG_CAP_SQUARE: path.cap = Path::Cap::Square; break;
				}

				switch (shape->strokeLineJoin) {
					case NSVG_JOIN_ROUND: path.join = Path::Join::Round; break;
					case NSVG_JOIN_MITER: path.join = Path::Join::Miter; break;
					case NSVG_JOIN_BEVEL: path.join = Path::Join::Bevel; break;
				}

				bigpath.merge(path.stroke());
			}
		}

		g->triangulate(bigpath);
	}

	nsvgDelete(image);

	delete[] str_copy;
}

void SVGObject::render(RenderTarget target) {
	for (Object<FillShader> const& obj: objs) {
		obj.render(target);
	}
}

#ifndef GLES
RenderToFile::RenderToFile(Window& wind, Vec2 size, unsigned fps, char const* file, Mat4 layer_trans, Mat4 cam, bool multisample):
	Layer(wind, size, layer_trans, cam, multisample), fps(fps) {

	std::string cmd = (std::stringstream() << "ffmpeg -y -f rawvideo -pixel_format bgr32 -video_size " << static_cast<int>(size[0]) << "x" << static_cast<int>(size[1]) << " -framerate " << fps << " -i - " << file).str();
	ffmpeg = popen(cmd.c_str(), "w");
}

void RenderToFile::render_channel(size_t i) {
	Texture& tex = channel(i);

	if (tex.format.format!=GL_RGBA || tex.format.internal!=GL_RGBA8) throw std::runtime_error("only rgba8 supported for rendertofile");

	buf.resize(static_cast<size_t>(size[0]*size[1])*4);

	glBindTexture(GL_TEXTURE_2D, tex.idx);
	glGetTexImage(GL_TEXTURE_2D, 0, tex.format.format, GL_UNSIGNED_BYTE, buf.data());

	glFinish();

	fwrite(buf.data(), buf.size(), 1, ffmpeg);
}

RenderToFile::~RenderToFile() {
	pclose(ffmpeg);
}
#endif
