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

	passthrough = std::make_shared<TexShader<Mat3>>(std::shared_ptr<VertShader>(new VertShader(
#include "./include/passthrough.vert"
	)), std::shared_ptr<FragShader>(new FragShader(
#include "./include/tex.frag"
	)), (char const*[]){"tex_transform"});

	full_rect = std::make_shared<Geometry>(Path {.points={{-1,1}, {1,1}, {1,-1}, {-1,-1}}, .fill=true, .closed=true});

	object_ubo = std::make_shared<UniformBuffer<Mat4, Mat4>>();
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

struct MonotonePolygon {
	std::vector<GLuint> left;
	std::vector<GLuint> right;
	std::unique_ptr<MonotonePolygon> merging;
};

struct SweepEdge {
	bool right;
	std::unique_ptr<MonotonePolygon> poly;
};

using EdgeNode = Node<std::pair<GLuint, GLuint>, SweepEdge>;

struct SweepEvent {
	std::vector<EdgeNode*> intersecting;
};

using EventNode = Node<GLuint, SweepEvent>;

void Geometry::triangulate(Path const& path) {
	Path stroked;
	if (!path.fill) stroked = path.stroke();
	Path const& path_ref = path.fill ? path : stroked;
	std::vector<Vec2> path_fill_points;
	if (path.fill) path_fill_points = path.points;
	std::vector<Vec2>& p = path.fill ? path_fill_points : stroked.points;

	auto elem_offset = static_cast<GLuint>(vertices.size());
//	std::vector<GLuint> sorted;
//	sorted.reserve(p.size());
//	for (GLuint i = 0; i<p.size(); i++) {
//		if (std::isnan(p[i][0]) || std::isnan(p[i][1])) continue;
//		sorted.push_back(i);
//	}
	
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
	
	auto cmp_pt = [&](GLuint const& i1, GLuint const& i2){
		return (p[i1][1] > p[i2][1])
				|| (p[i1][1]==p[i2][1] && p[i1][0] > p[i2][0]);
	};

//	std::sort(sorted.begin(), sorted.end(), cmp_pt);

	//so i can copy-paste into geogebra 😜
//	std::cout<<std::endl;
//	for (Vec2 const& pt: p) {
//		std::cout << "(" << pt[0] << ", " << pt[1] << "), ";
//	}
//	std::cout<<std::endl;

	auto getx = [&](std::pair<GLuint, GLuint> const& edge, float x, float y) -> float {
		float dy = p[edge.first][1]-p[edge.second][1];
		if (dy==0 && (x<=p[edge.first][0]) && (x>=p[edge.second][0])) return x;
		
		if (y==p[edge.first][1]) return p[edge.first][0];
		else if (y==p[edge.second][1]) return p[edge.second][0];

		return p[edge.first][0]+(p[edge.second][0]-p[edge.first][0])*(p[edge.first][1]-y)/dy;
	};
	
	EdgeNode::Root edges;
	EventNode::Root event;
	
	for (GLuint i=0; i<p.size(); i++) event.insert(std::forward<GLuint>(i), SweepEvent {}, cmp_pt);

	GLuint intersections_i = p.size();
	std::vector<std::unique_ptr<EventNode>> removed_evs;
	while (event.ptr) {
		EventNode* e = &*event.begin();
		GLuint i = e->x;
		
		auto cmp_x = [&](std::pair<GLuint, GLuint> const& edge1, std::pair<GLuint, GLuint> const& edge2) -> float {
			return getx(edge1, p[i][0], p[i][1])>getx(edge2, p[i][0], p[i][1]);
		};

		auto rec_intersection = [&](EdgeNode::Iterator iter, bool left) {
			EdgeNode* ref = &*iter;
			if (left) --iter; else ++iter;
			if (iter!=edges.end()) {
				GLuint endpt = cmp_pt(ref->x.second,iter->x.second) ? ref->x.second : iter->x.second;
				float diff = getx(iter->x, p[endpt][0], p[endpt][1])-getx(ref->x, p[endpt][0], p[endpt][1]);
				if (diff==0 || diff<0 != left) return;
				
				std::optional<Vec2::Intersection> intersection = Vec2::intersect(p[ref->x.first],p[ref->x.second]-p[ref->x.first],p[iter->x.first], p[iter->x.second]-p[iter->x.first]);
				if (!intersection) return;
				
				p.emplace_back(intersection->pos);
				
				EventNode* eres = event.find(p.size()-1, cmp_pt);

				for (EventNode* up = eres; up; up=up->parent) {
					if (std::find(up->v.intersecting.begin(), up->v.intersecting.end(), &*iter)!=up->v.intersecting.end()
							&& std::find(up->v.intersecting.begin(), up->v.intersecting.end(), ref)!=up->v.intersecting.end()) return;

					if (p[up->x]==intersection->pos) { //gee i hope this never happens bc all my polys are healthy and all their lines are in general position like good tall growing children...
						p.pop_back();
						auto eiter = up->v.intersecting.begin();
						for (;eiter!=up->v.intersecting.end(); eiter++) {
							if ((p[(*eiter)->x.first]-intersection->pos)
									.determinant(p[(left ? &*iter : ref)->x.first]-intersection->pos)<=0) {
								break;
							}
						}

						if (eiter==up->v.intersecting.end() || *eiter==(left ? &*iter : ref))
							eiter = up->v.intersecting.insert(eiter, left ? ref : &*iter);
						else if (eiter==up->v.intersecting.end() && *eiter==(left ? ref : &*iter))
							return;

						up->v.intersecting.insert(eiter, left ? &*iter : ref);
						return;
					}
				}

				eres->insert(p.size()-1, SweepEvent {.intersecting={left ? &*iter : ref, left ? ref : &*iter}}, cmp_pt);
			}
		};

		auto merge_left_poly = [&](std::unique_ptr<MonotonePolygon>& poly) {
				//poly->right.push_back(i);
				triangulate_monotone(*poly);
				MonotonePolygon* merging = poly->merging.release();
				merging->left.push_back(i);
				poly=std::unique_ptr<MonotonePolygon>(merging);
		};
		
		auto merge_right_poly = [&](std::unique_ptr<MonotonePolygon>& poly) {
				poly->merging->right.push_back(i);
				triangulate_monotone(*poly->merging);
				poly->merging.reset();
		};

		if (!e->v.intersecting.empty()) {
//			EdgeNode::Iterator iter1 = edges.iter_ref(e->v.intersecting[0]);
//			EdgeNode::Iterator iter2 = edges.iter_ref(e->v.intersecting[1]);
//			assert(iter2==iter1+1);
			for (size_t off=0; off<=(e->v.intersecting.size()-1)/2; off++) {
				size_t ioff = e->v.intersecting.size()-1-off;
				e->v.intersecting[off]->swap_positions(e->v.intersecting[ioff]);
				std::swap(e->v.intersecting[off]->v, e->v.intersecting[ioff]->v);
			}

//			iter1 = edges.iter_ref(e->v.intersecting[0]);
//			iter2 = edges.iter_ref(e->v.intersecting[1]);
//			assert(iter2+1==iter1);
			for (auto node: e->v.intersecting) {
				EdgeNode::Iterator insiter = edges.iter_ref(node);
				if (insiter->v.right) {
					auto& poly = (insiter-1)->v.poly;
					poly->right.push_back(i);
					if (poly->merging) merge_right_poly(poly);
				} else {
					auto& poly = insiter->v.poly;
					poly->left.push_back(i);
					if (poly->merging) merge_left_poly(poly);
				}

				if (node==e->v.intersecting.front()) {
					rec_intersection(insiter, false);
				} else if (node==e->v.intersecting.back()) {
					rec_intersection(insiter, true);
				}
			}

			if (i>=intersections_i) {
				removed_evs.push_back(e->remove());
				continue;
			}
		}

		auto part = path_ref.part(i);
		std::array<GLuint, 2> adj = {path_ref.prev(part, i), path_ref.next(part, i)};

		float d = (p[adj[0]]-p[i]).determinant(p[adj[1]]-p[i]);
		if (d>0) std::swap(adj[0], adj[1]);

		bool l1 = p[adj[0]][1] > p[i][1] || (p[adj[0]][1]==p[i][1] && p[adj[0]][0] > p[i][0]);
		bool l2 = p[adj[1]][1] > p[i][1] || (p[adj[1]][1]==p[i][1] && p[adj[1]][0] > p[i][0]);

		if (!l1 && !l2) {
			EdgeNode::Iterator pos = edges.iter_ref(edges.find(std::make_pair(i,adj[0]), cmp_x));

			SweepEdge l,r;
			if (pos!=edges.end() && cmp_x(std::make_pair(i,adj[0]),pos->x)==pos->v.right) {
				auto& poly = pos->v.right ? (pos-1)->v.poly : pos->v.poly;

				l.right=true;
				r.right=false;

				if (poly->merging) {
					poly->right.push_back(i);
					poly->merging->left.push_back(i);

					r.poly.swap(poly->merging);
				} else if ((p[poly->left.back()][1] < p[poly->right.back()][1])
						|| (p[poly->left.back()][1]==p[poly->right.back()][1] && p[poly->left.back()][0] < p[poly->right.back()][0])) {
					r.poly = std::make_unique<MonotonePolygon>(MonotonePolygon {.left={poly->left.back()}, .right={poly->left.back(), i}});

					poly->left.push_back(i);
					poly.swap(r.poly);
				} else {
					r.poly = std::make_unique<MonotonePolygon>(MonotonePolygon {.left={poly->right.back(), i}, .right={poly->right.back()}});
					poly->right.push_back(i);
				}
			} else {
				l.right=false;
				r.right=true;
				l.poly = std::make_unique<MonotonePolygon>(MonotonePolygon {.left={i}, .right={i}});
			}

			EdgeNode::Iterator iter1 = edges.iter_ref(edges.insert(std::pair(i, adj[0]), std::move(l), cmp_x));
			EdgeNode::Iterator iter2 = edges.iter_ref(edges.insert(std::pair(i, adj[1]), std::move(r), cmp_x));

			rec_intersection(iter1,true);
			rec_intersection(iter2,false);
		} else if (!l1 || !l2) {
			size_t up = l1 ? adj[0] : adj[1];
			size_t down = l1 ? adj[1] : adj[0];

			auto edge_iter = edges.iter_ref(edges.find(std::pair(up,i), cmp_x));
			for (; edge_iter->x.second!=i; ++edge_iter);
//			auto rem = edge_iter->remove();
			edge_iter->x = std::make_pair(i,down);
//
//			EdgeNode* ref = rem.get();
//			edges.insert_node(std::move(rem), cmp_x);
//			EdgeNode::Iterator iter = edges.iter_ref(ref);
//

			if (edge_iter->v.right) {
				auto& poly = (edge_iter-1)->v.poly;
				poly->right.push_back(i);
				if (poly->merging) merge_right_poly(poly);
			} else {
				auto& poly = edge_iter->v.poly;
				poly->left.push_back(i);
				if (poly->merging) merge_left_poly(poly);
			}

			rec_intersection(edge_iter,true);
			rec_intersection(edge_iter,false);
		} else {
			auto edge_iter = edges.iter_ref(edges.find(std::pair(adj[1],i), cmp_x));
			for (; edge_iter->x.second!=i; ++edge_iter);
			auto edge_iter2 = edge_iter+1;
			for (; edge_iter2->x.second!=i; ++edge_iter2);
			
			if (edge_iter->v.right) {
				auto& poly = (edge_iter-1)->v.poly;

				poly->right.push_back(i);
				if (poly->merging) merge_right_poly(poly);

				auto& poly2 = edge_iter2->v.poly;
				poly2->left.push_back(i);
				if (poly2->merging) merge_left_poly(poly2);

				poly->merging = std::unique_ptr<MonotonePolygon>(new MonotonePolygon(std::move(*poly2)));
			} else {
				auto& poly = edge_iter->v.poly;
				poly->left.push_back(i);

				if (poly->merging) {
					poly->merging->left.push_back(i);
					//poly->merging->right.push_back(i);
					triangulate_monotone(*poly->merging);
				}

				triangulate_monotone(*poly);
			}

			EdgeNode* edge1 = &*edge_iter;
			--edge_iter;
			edge1->remove();
			edge_iter2->remove();

			if (edge_iter!=edges.end()) rec_intersection(edge_iter,false);
		}
		
		removed_evs.push_back(e->remove());
	}

	vertices.reserve(vertices.size()+p.size());

	for (Vec2 const& point: p) {
		vertices.push_back(Vertex {.pos=Vec3({point[0], point[1], 0}), .normal={0,0,1}});
	}

	update_buffers();
}

Geometry::~Geometry() {
	if (vao==-1) return;
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(2, (GLuint[]){vbo, ebo});
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
