#include <new>

#include "graphics.hpp"
#include "arrayset.hpp"

#include <algorithm>

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

void Window::swap(bool delay) {
	if (delay) {
		Uint64 tick = SDL_GetPerformanceCounter();
		if (!opts.vsync && tick < fps_ticks + last_swap) {
			SDL_Delay((1000*(fps_ticks + last_swap - tick))/SDL_GetPerformanceFrequency());
		}
	}

	last_swap = SDL_GetPerformanceCounter();

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

#ifdef MULTISAMPLE
Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data, bool multisample): wind(wind), format(format), size(size), multisample(multisample) {
#else
Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data, bool multisample): wind(wind), format(format), size(size) {
#endif
	glGenTextures(1, &idx);

#ifdef MULTISAMPLE
	if (multisample) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, idx);
		glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, wind.opts.samples, format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), GL_FALSE);
	} else {
#endif
		glBindTexture(GL_TEXTURE_2D, idx);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, format.format, format.type, data);
#ifdef MULTISAMPLE
	}
#endif

	gl_checkerr();
}

Texture::Texture(Window& wind, TexFormat format, Vec2 size, float* data):
		Texture(wind, format, size, data, false) {}
Texture::Texture(Window& wind, TexFormat format, Vec2 size, bool multisample):
		Texture(wind, format, size, nullptr, multisample) {}

#ifdef MULTISAMPLE
Texture::Texture(Texture const& other): Texture(other.wind, other.format, other.size, other.multisample) {
#else
Texture::Texture(Texture const& other): Texture(other.wind, other.format, other.size, false) {
#endif
	other.proc(*wind.passthrough, *this, std::tuple(Mat3(1)));
}

#ifdef MULTISAMPLE
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
		multisample_channels.emplace_back(wind, format, size, true);
		glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D_MULTISAMPLE, multisample_channels.back().idx, 0);

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
	for (auto& multisamp_chan: multisample_channels) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, multisamp_chan.idx);
		glBindTexture(GL_TEXTURE_2D, multisamp_chan.idx);
		glTexImage2D(GL_TEXTURE_2D, 0, multisamp_chan.format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, multisamp_chan.format.format, multisamp_chan.format.type, nullptr);
		glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, wind.opts.samples, multisamp_chan.format.internal, static_cast<int>(size[0]), static_cast<int>(size[1]), GL_FALSE);
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

struct MonotonePolygon {
	std::vector<GLuint> left;
	std::vector<GLuint> right;
	std::unique_ptr<MonotonePolygon> merging;
};

void Geometry::triangulate(Path const& path) {
	Path stroked;
	if (!path.fill) stroked = path.stroke();
	Path const& path_ref = path.fill ? path : stroked;
	std::vector<Vec2> const& p = path_ref.points;

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

	auto elem_offset = static_cast<GLuint>(vertices.size());
	vertices.reserve(vertices.size()+p.size());

	for (Vec2 const& point: p) {
		vertices.push_back(Vertex {.pos=Vec3({point[0], point[1], 0}), .normal={0,0,1}});
	}

	std::vector<std::pair<GLuint, GLuint>> sweep;
	std::vector<MonotonePolygon> stack;

	//so i can copy-paste into geogebra 😜
//	std::cout<<std::endl;
//	for (Vec2 const& pt: p) {
//		std::cout << "(" << pt[0] << ", " << pt[1] << "), ";
//	}
//	std::cout<<std::endl;

	auto triangulate_monotone = [&](MonotonePolygon& poly){
		auto iter = poly.left.rbegin();
		auto other_iter = poly.right.rbegin();
		std::vector<GLuint>* chain = &poly.left;
		std::vector<GLuint>* other_chain = &poly.right;
		bool iter_left=true;
		bool switched=false;

		while (iter!=chain->rend() || other_iter!=other_chain->rend()) {
			//...
			while (iter!=chain->rend() &&
					((other_iter==other_chain->rend()) || (p[*iter][1]<p[*other_iter][1]
							|| (p[*iter][1]==p[*other_iter][1] && p[*iter][0]<=p[*other_iter][0])))) {

				if (iter-chain->rbegin()>1 || (!switched && iter!=chain->rbegin())) {
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
	};

	for (size_t i=0; i<sorted.size(); i++) {
		auto part = path_ref.part(sorted[i]);
		std::array<GLuint, 2> adj = {path_ref.prev(part, sorted[i]), path_ref.next(part, sorted[i])};

		if ((p[adj[0]]-p[sorted[i]]).abs()<Epsilon) continue;

		std::vector<std::pair<GLuint, GLuint>>::iterator edge = std::find_if(sweep.begin(), sweep.end(), [&](std::pair<GLuint, GLuint> x){return x.second == sorted[i];});

		while ((p[adj[1]]-p[sorted[i]]).abs()<Epsilon) {
			auto new_edge = std::find_if(sweep.begin(), sweep.end(), [&](std::pair<GLuint, GLuint> x){return x.second == adj[1];});
			if (new_edge<edge) edge=new_edge;
			adj[1]=path_ref.next(part, adj[1]);
		}

		if (adj[1]==adj[0]) continue; //sanity check, in case not a triangle i guess

		bool l1 = p[adj[0]][1] < p[sorted[i]][1] || (p[adj[0]][1]==p[sorted[i]][1] && p[adj[0]][0] < p[sorted[i]][0]);
		bool l2 = p[adj[1]][1] < p[sorted[i]][1] || (p[adj[1]][1]==p[sorted[i]][1] && p[adj[1]][0] < p[sorted[i]][0]);

		auto merge_poly = [&](std::vector<MonotonePolygon>::iterator poly){
			if (adj[0]==poly->left.back() || adj[1]==poly->left.back()) {
				poly->left.push_back(sorted[i]);
				//poly->right.push_back(sorted[i]);
				triangulate_monotone(*poly);

				MonotonePolygon* merging = poly->merging.release();
				merging->left.push_back(sorted[i]);
				auto ins = stack.insert(stack.erase(poly), std::move(*merging));
				delete merging;

				return ins;
			} else if (adj[0]==poly->merging->right.back() || adj[1]==poly->merging->right.back()) {
				poly->merging->right.push_back(sorted[i]);
				poly->right.push_back(sorted[i]);

				triangulate_monotone(*poly->merging);

				poly->merging.reset();
			}

			return poly;
		};

		if (edge==sweep.end()) {
			auto sweep_pos = std::find_if(sweep.begin(), sweep.end(), [&](std::pair<GLuint, GLuint> x) {
				float big_dy = p[x.first][1]-p[x.second][1];
				float dy = big_dy==0 ? 0 : (p[x.first][1]-p[sorted[i]][1])/big_dy;
				return p[sorted[i]][0]>=p[x.first][0] + dy*(p[x.second][0]-p[x.first][0]);
			});

			bool swap = p[adj[1]][1]==p[adj[0]][1] ? p[adj[0]][0] < p[adj[1]][0] : (p[adj[0]]-p[sorted[i]]).determinant(p[adj[1]]-p[sorted[i]])>0;

			//split/new
			if ((sweep_pos-sweep.begin()) % 2 == 1) {
				auto poly = stack.begin() + (sweep_pos-sweep.begin())/2;

				if (poly->merging) {
					poly->right.push_back(sorted[i]);
					poly->merging->left.push_back(sorted[i]);

					MonotonePolygon* merging = poly->merging.release();
					stack.insert(poly+1, std::move(*merging));
					delete merging;
				} else if ((p[poly->left.back()][1] < p[poly->right.back()][1])
						|| (p[poly->left.back()][1]==p[poly->right.back()][1] && p[poly->left.back()][0] < p[poly->right.back()][0])) {
					auto new_poly = MonotonePolygon {.left={poly->left.back()}, .right={poly->left.back(), sorted[i]}};
					poly->left.push_back(sorted[i]);
					stack.insert(poly, std::move(new_poly));
				} else {
					auto new_poly = MonotonePolygon {.left={poly->right.back(), sorted[i]}, .right={poly->right.back()}};
					poly->right.push_back(sorted[i]);
					stack.insert(poly+1, std::move(new_poly));
				}
			} else {
				stack.emplace(stack.begin()+(sweep_pos-sweep.begin())/2, MonotonePolygon {.left={sorted[i]}, .right={sorted[i]}});
			}

			auto first = sweep.emplace(sweep_pos, sorted[i], swap ? adj[1] : adj[0]);
			sweep.emplace(first+1, sorted[i], swap ? adj[0] : adj[1]);
		} else if (l1 || l2) {
			auto poly = stack.begin() + (edge-sweep.begin())/2;
			if (poly->merging) {
				merge_poly(poly);
			} else if ((edge-sweep.begin()) % 2 == 1) {
				poly->right.push_back(sorted[i]);
			} else if ((edge-sweep.begin()) % 2 == 0) {
				poly->left.push_back(sorted[i]);
			}

			if (l1) *edge = std::pair(sorted[i], adj[0]);
			else *edge = std::pair(sorted[i], adj[1]);
		} else {
			auto poly = stack.begin() + (edge-sweep.begin())/2;

			if ((edge-sweep.begin()) % 2 == 1) {
				if (poly->merging) poly = merge_poly(poly);
				else poly->right.push_back(sorted[i]);

				auto poly2 = poly+1 == stack.end() ? poly-1 : poly+1;
				if (poly2->merging) poly2 = merge_poly(poly2);
				else poly2->left.push_back(sorted[i]);

				poly->merging = std::unique_ptr<MonotonePolygon>(new MonotonePolygon(std::move(*poly2)));
				stack.erase(poly2);
			} else {
				poly->left.push_back(sorted[i]);

				if (poly->merging) {
					poly->merging->left.push_back(sorted[i]);
					//poly->merging->right.push_back(sorted[i]);
					triangulate_monotone(*poly->merging);
				}

				triangulate_monotone(*poly);
				stack.erase(poly);
			}

			sweep.erase(sweep.erase(edge));
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

void Path::stroke_side(Path& expanded, bool cw) const {
	Vec2 off;

	for (size_t part=0; part<parts.size(); part++) {
		GLuint lim = part+1==parts.size() ? points.size() : parts[part + 1];
		GLuint n = lim-parts[part];
		if (n==1) continue;

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
}

Path Path::stroke() const {
	Path expanded = {.points=std::vector<Vec2>(), .fill=true, .closed=true, .stroke_width=0};
	stroke_side(expanded, true);
	if (closed) expanded.parts.push_back(expanded.points.size());
	stroke_side(expanded, false);
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

	if (tex.format!=GL_RGBA || tex.internalformat!=GL_RGBA8) throw std::runtime_error("only rgba8 supported for rendertofile");

	buf.resize(static_cast<size_t>(size[0]*size[1])*4);


	glBindTexture(GL_TEXTURE_2D, tex.idx);
	glGetTexImage(GL_TEXTURE_2D, 0, tex.format, GL_UNSIGNED_BYTE, buf.data());

	glFinish();

	fwrite(buf.data(), buf.size(), 1, ffmpeg);
}

RenderToFile::~RenderToFile() {
	pclose(ffmpeg);
}
#endif
