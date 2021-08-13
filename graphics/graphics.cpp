#include "graphics.hpp"

#include "arrayset.hpp"

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
					name, opts.x, opts.y, opts.w, opts.h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)))
					sdl_reporterr();

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	if (!(ctx = SDL_GL_CreateContext(window))) sdl_reporterr();

	SDL_GL_SetSwapInterval(opts.vsync ? 1 : 0);  // VSYNC

	fps_ticks = SDL_GetPerformanceFrequency()/opts.fps;
	last_swap = 0;

	// enable
	glEnable(GL_MULTISAMPLE);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	bounds = {(float)w, (float)h};

	glGenFramebuffers(1, &tex_fbo);

	glBindFramebuffer(GL_FRAMEBUFFER, tex_fbo);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	passthrough = std::move(TexShader<>(std::shared_ptr<VertShader>(new VertShader(
#include "./include/passthrough.vert"
	)), std::shared_ptr<FragShader>(new FragShader(
#include "./include/tex.frag"
	)), nullptr));

	full_rect = std::move(Geometry(Path {.points={{-1,1}, {1,1}, {1,-1}, {-1,-1}}, .fill=true, .closed=true}));

	object_ubo = UniformBuffer<Mat4, Mat4>();
	object_ubo->set_data(std::tuple(Mat4(1), Mat4(1)));
	object_ubo->use(static_cast<GLint>(BlockIndices::Object));

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0,0, static_cast<int>(bounds[0]),static_cast<int>(bounds[1]));
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	gl_checkerr();
}

void Window::swap() {
	Uint64 tick = SDL_GetPerformanceCounter();
	if (!opts.vsync && tick < fps_ticks + last_swap) {
		SDL_Delay((1000*(fps_ticks + last_swap - tick))/SDL_GetPerformanceFrequency());
	}

	last_swap = SDL_GetPerformanceCounter();
	swapped = !swapped;

	use();
	glFinish();
	SDL_GL_SwapWindow(window);
	gl_checkerr();

	glViewport(0,0, static_cast<int>(bounds[0]),static_cast<int>(bounds[1]));
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

Window::~Window() {
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
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		object_ubo->use(static_cast<GLint>(BlockIndices::Object));
		in_use = nullptr;
	}
}

void Window::update_transform_camera(Mat4 const& trans, Mat4 const& cam) {
	object_ubo->set_data(std::tuple(trans, cam));
}

Texture::Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, float* data, bool multisample): wind(wind), format(format), internalformat(internalformat), size(size), multisample(multisample) {
	glGenTextures(1, &idx);

	if (!multisample) {
		glBindTexture(GL_TEXTURE_2D, idx);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, internalformat, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, format, GL_FLOAT, data);
	} else {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, idx);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, wind.opts.samples, internalformat, static_cast<int>(size[0]), static_cast<int>(size[1]), GL_FALSE);
	}

	gl_checkerr();
}

GLint Texture::default_internalformat(GLenum format) {
	switch (format) {
		case GL_RGBA: return GL_RGBA8;
		case GL_DEPTH_COMPONENT: return GL_DEPTH_COMPONENT32;
		default: throw std::runtime_error("no default for texture format");
	}
}

Texture::Texture(Window& wind, GLenum format, Vec2 size, float* data):
	Texture(wind, format, default_internalformat(format), size, data, false) {}
Texture::Texture(Window& wind, GLenum format, Vec2 size, bool multisample):
	Texture(wind, format, default_internalformat(format), size, nullptr, multisample) {}

Texture::Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, float* data):
		Texture(wind, format, internalformat, size, data, false) {}
Texture::Texture(Window& wind, GLenum format, GLint internalformat, Vec2 size, bool multisample):
		Texture(wind, format, internalformat, size, nullptr, multisample) {}

Texture::Texture(Texture const& other): Texture(other.wind, other.format, other.size, other.multisample) {
	other.proc(*wind.passthrough, *this, std::tuple());
}

Texture::Texture(Texture&& other): wind(other.wind), multisample(other.multisample), format(other.format), internalformat(other.internalformat), size(other.size), idx(other.idx) {
	other.idx=-1;
}

Texture::~Texture() {
	if (idx!=-1) glDeleteTextures(1, &idx);
}

Layer::Layer(Window& wind, Vec2 size, Mat4 layer_trans, Mat4 cam, bool multisample):
	wind(wind), cleared(!wind.swapped), size(size), depth(false), layer_transform(layer_trans), cam(cam), multisample(true), object_ubo() {

	if (multisample) {
		glGenFramebuffers(1, &multisamp_fbo);

		glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glCullFace(GL_BACK);
	}

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

void Layer::add_channel(GLenum format, GLint internalformat) {
	GLenum attach;
	if (format==GL_DEPTH_COMPONENT) {
		attach = GL_DEPTH_ATTACHMENT;
		depth=true;
	} else {
		attach = GL_COLOR_ATTACHMENT0 + channels.size();
		color_attachments.push_back(attach);
	}

	if (multisample) {
		multisample_channels.emplace_back(wind, format, internalformat, size, true);
		glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D_MULTISAMPLE, multisample_channels.back().idx, 0);

		if (format!=GL_DEPTH_ATTACHMENT) {
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				throw std::runtime_error("framebuffer not complete");
		}
	}

	channels.emplace_back(wind, format, internalformat, size, false);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, channels.back().idx, 0);

	if (format!=GL_DEPTH_ATTACHMENT) {
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			throw std::runtime_error("framebuffer not complete");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	gl_checkerr();
}

void Layer::add_channel(GLenum format) {
	add_channel(Texture::default_internalformat(format), format);
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

		if (multisample) glBindFramebuffer(GL_FRAMEBUFFER, multisamp_fbo);
		else glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		object_ubo.use(static_cast<GLint>(BlockIndices::Object));

		glDrawBuffers(color_attachments.size(), color_attachments.data());

		glViewport(0,0, static_cast<int>(size[0]),static_cast<int>(size[1]));

		if (cleared!=wind.swapped) clear();
	}
}

void Layer::update_size() {
	for (auto& multisamp_chan: multisample_channels) {
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, multisamp_chan.idx);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, wind.opts.samples, multisamp_chan.internalformat, static_cast<int>(size[0]), static_cast<int>(size[1]), GL_FALSE);
	}

	for (auto& chan: channels) {
		glBindTexture(GL_TEXTURE_2D, chan.idx);
		glTexImage2D(GL_TEXTURE_2D, 0, chan.internalformat, static_cast<int>(size[0]), static_cast<int>(size[1]), 0, chan.format, GL_FLOAT, nullptr);
	}
}

void Layer::finish() {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, multisamp_fbo);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (GLenum attach: color_attachments) {
		glReadBuffer(attach);
		glDrawBuffer(attach);

		glBlitFramebuffer(0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), 0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	if (depth) {
		glBlitFramebuffer(0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), 0, 0, static_cast<int>(size[0]),static_cast<int>(size[1]), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

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

void Geometry::triangulate(Path const& path, unsigned amt) {
	Path stroked;
	if (!path.fill) stroked = path.stroke();
	Path const& path_ref = path.fill ? path : stroked;

	ArraySet<bool> crossings(2, path_ref.points.size());

	//accurate, assuming no colinear points
	bool convex=true;

	for (auto x: crossings) {
		Vec2 const& p1 = path_ref.points[x.first[0]];
		Vec2 const& p2 = path_ref.points[x.first[1]];

		if (x.first[0]==x.first[1]+1 || (x.first[0]==path_ref.points.size()-1 && x.first[1]==0)) {
			x.second=true;
			continue;
		}

		x.second=false;

		Vec2 off = p2-p1;

		size_t num_crossings=0;
		for (size_t i=0; i<path_ref.points.size(); i++) {
			Vec2 off2 = path_ref.points[i == path_ref.points.size()-1 ? 0 : i+1] - path_ref.points[i];
			if (off2>-Epsilon && off2<Epsilon) continue;

			std::optional<Vec2::Intersection> crossing = Vec2::intersect(p1, off, path_ref.points[i], off2);

			if (crossing && crossing->in_segment_no_endpoints()) {
				x.second=true;
				break;
			} else if (crossing && crossing->c1>0 && crossing->c2==0) {
				Vec2 off_before = path_ref.points[i==0 ? path_ref.points.size()-1 : i-1]-path_ref.points[i];
				for (size_t i_minus=2; off_before>-Epsilon && off_before<Epsilon && i_minus<path_ref.points.size(); i_minus++)
					off_before = path_ref.points[i_minus>=i ? path_ref.points.size()-i_minus+i : i-i_minus] - path_ref.points[i];

				if (std::signbit(off_before.determinant(off))!=std::signbit(off2.determinant(off))) {
					if (crossing->c1<1) {
						x.second=true;
						break;
					} else {
						num_crossings++;
					}
				}
			} else if (crossing && crossing->c1>0 && crossing->c2>0 && crossing->c2<1) {
				num_crossings++;
			}
		}

		if (num_crossings%2==0) {
			x.second=true;
		}

		if (x.second) convex=false;
	}

	auto elem_offset = static_cast<GLuint>(vertices.size());
	vertices.reserve(vertices.size()+path_ref.points.size());

	for (Vec2 const& point: path_ref.points) {
		vertices.push_back(Vertex {.pos=Vec3({point[0], point[1], 0}), .normal={0,0,1}});
	}

	if (convex) {
		for (size_t i=1; i<path_ref.points.size()-1; i++) {
			elements.insert(elements.end(), {elem_offset, elem_offset+static_cast<GLuint>(i), elem_offset+static_cast<GLuint>(i+1)});
		}
	} else {
		std::vector<GLuint> intermediate_verts(vertices.size());
		for (GLuint i = 0; i<vertices.size(); i++) {
			intermediate_verts[i]=i;
		}

		bool left;

		do {
			left=false;
			for (size_t x = 0; x < intermediate_verts.size(); x++) {
				GLuint i = intermediate_verts[x];
				GLuint i1;
				GLuint i2;
				size_t mid_idx;

				if (x+1==intermediate_verts.size()) {
					mid_idx=0;
					i1=intermediate_verts[0];
					i2=intermediate_verts[1];
				} else {
					mid_idx=x+1;
					i1=intermediate_verts[x+1];
					i2=x+2==intermediate_verts.size() ? intermediate_verts[0] : intermediate_verts[x+2];
				}

				if (!(path_ref.points[i1]-path_ref.points[i]>-Epsilon && path_ref.points[i1]-path_ref.points[i]<Epsilon)) {
					if (crossings[(size_t[]){static_cast<size_t>(i), static_cast<size_t>(i2)}]) continue;
					elements.insert(elements.end(), {elem_offset+i, elem_offset+i1, elem_offset+i2});
				}

				left=true;
				intermediate_verts.erase(intermediate_verts.begin()+mid_idx);
				break;
			}

			amt--;
		} while (left && amt>0);

		if (intermediate_verts.size()==3)
			elements.insert(elements.end(), {elem_offset+intermediate_verts[0], elem_offset+intermediate_verts[1], elem_offset+intermediate_verts[2]});
	}

	update_buffers();
}

Geometry::~Geometry() {
	if (vao==-1) return;
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(2, (GLuint[]){vbo, ebo});
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

void Path::fan(Vec2 to, size_t divisions) {
	Vec2 offset = to-points.back();
	Vec2 prev_offset = *(points.end() - 2) - points.back();

	float onorm = offset.norm();
	float norm_factor = onorm*prev_offset.norm();
	float cos = prev_offset.dot(offset)/norm_factor;
	float sin = prev_offset.determinant(offset)/norm_factor;

	float fan_sin = 2*(offset - prev_offset*cos).norm();
	float r = onorm/fan_sin;
	float angle = std::asin(fan_sin);

	Vec2 center_offset = (sin>0 ? offset.perpendicular(0,1) : offset.perpendicular(1,0))*r;
	Vec2 center = points.back() - center_offset;

	Vec2 rot_mat {std::cosf(angle/static_cast<float>(divisions)), std::sinf(angle/static_cast<float>(divisions))};
	for (size_t i=0; i<divisions; i++) {
		center_offset = center_offset.rotate_by(rot_mat);
		center_offset = center_offset.normalize(r);
		points.emplace_back(center + center_offset);
	}
}

void Path::cubic(Vec2 p2, Vec2 p3, Vec2 p4, float res) {
	Vec2& start = points.back();

	float d;
	for (float x=0; x<1; x+=d) {
		float xsq = x*x, xcb = xsq*x, ixsq=(1-x)*(1-x);
		float w1=1-xcb, w2=3*(1-x)*xsq, w3=3*x*ixsq, w4=xcb;

		points.emplace_back(start*w1+p2*w2+p3*w3+p4*w4);

		float dw1=6*x, dw2=2*x;
		float curvature_sq = (start*dw1+p2*dw2+p3*dw2+p4*dw1).norm_sq();
		d = 1/(res*curvature_sq);
	}

	points.push_back(p4);
}

void Path::stroke_side(Path& expanded, bool cw) const {
	Vec2 off;

	for (size_t i = cw ? 0 : points.size()-1; cw ? i<points.size() : i!=-1; cw ? i++ : i--) {
		if ((i>0 && i<points.size()-1) || closed) {
			Vec2 diff = (points[i]-points[i==0 ? points.size()-1 : i-1]);
			for (size_t i_minus=2; diff>-Epsilon && diff<Epsilon && i_minus<points.size(); i_minus++)
				diff = points[i] - points[i_minus>=i ? points.size()-i_minus+i : i-i_minus];
			diff=diff.normalize(1);

			Vec2 diff2 = points[i+1==points.size() ? 0 : i+1] - points[i];
			for (size_t i_plus=2; diff2>-Epsilon && diff2<Epsilon && i_plus<points.size(); i_plus++)
				diff2 = points[(i+i_plus)%points.size()] - points[i];
			diff2=diff2.normalize(1);

			//> you could have used the half angle formula instead!
			// - me

			Vec2 avg = (diff2-diff).normalize(1);
			float slope = diff.determinant(avg);
			float off_det_edge;

			if (i==0 && cw) off_det_edge = 1;
			else if (i==points.size()-1 && !cw) off_det_edge = -1;
			else off_det_edge = diff.determinant(off);

			bool off_sign = std::signbit(slope)!=std::signbit(off_det_edge);
			if (join==Join::Round && off_sign) {
				expanded.points.emplace_back(points[i] + diff.perpendicular(cw, !cw)*stroke_width);
				off = diff2.perpendicular(cw, !cw)*stroke_width;
				expanded.fan(points[i] + off, 15);
				expanded.points.emplace_back(points[i] + off);
			} else if ((join==Join::Bevel || std::abs(1/slope)>miter_limit) && off_sign) {
				expanded.points.emplace_back(points[i] + diff.perpendicular(cw, !cw)*stroke_width);
				off = diff2.perpendicular(cw, !cw)*stroke_width;
				expanded.points.emplace_back(points[i] + off);
			} else {
				off = avg*(off_sign ? -std::abs(stroke_width/slope) : std::abs(stroke_width/slope));
				expanded.points.emplace_back(points[i] + off);
			}
		} else if (i==0) {
			Vec2 diff = points[i+1]-points[i];
			off = (cap==Cap::Square ? -diff.normalize(stroke_width) : Vec2(0.0f)) + diff.perpendicular(cw, !cw).normalize(stroke_width);

			if (cap==Cap::Round) expanded.arc(M_PI, points[i] + off, 15);
			else expanded.points.emplace_back(points[i] + off);
		} else {
			Vec2 diff = points[i]-points[i-1];
			off = (cap==Cap::Square ? diff.normalize(stroke_width) : Vec2(0.0f)) + diff.perpendicular(cw, !cw).normalize(stroke_width);

			if (cap==Cap::Round) expanded.arc(M_PI, points[i] + off, 25);
			else expanded.points.emplace_back(points[i] + off);
		}
	}
}

Path Path::stroke() const {
	Path expanded = {.points=std::vector<Vec2>(), .fill=true, .closed=true, .stroke_width=0};
	stroke_side(expanded, true);
	stroke_side(expanded, false);
	return expanded;
}

SVGObject::SVGObject(Window& wind, char const* svg_data, float res) : objs() {
	char* str_copy = new char[strlen(svg_data)+1];
	std::copy(svg_data, svg_data+strlen(svg_data)+1, str_copy);

	NSVGimage* image = nsvgParse(str_copy, "px", 96);

	// Use...
	for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
		std::shared_ptr<Geometry> g;
		Vec2 center = Vec2 {shape->bounds[0]+shape->bounds[2], shape->bounds[1]+shape->bounds[3]}/2;
		objs.push_back(Object<FillShader> {.wind=wind, .geo=g, .transform=Mat4(1), .shad=*fill_shader});
		Object<FillShader>& obj = objs.back();
		obj.transform[3][0] += center[0];
		obj.transform[3][1] += center[1];
		obj.shader_params = Vec4 {static_cast<float>(shape->fill.color & UCHAR_MAX)/UCHAR_MAX, static_cast<float>((shape->fill.color>>8) & UCHAR_MAX)/UCHAR_MAX, static_cast<float>((shape->fill.color>>16) & UCHAR_MAX)/UCHAR_MAX, shape->opacity};

		for (NSVGpath* svgpath = shape->paths; svgpath != nullptr; svgpath = svgpath->next) {
			Path path;
			for (int i = 0; i < svgpath->npts-1; i += 3) {
				float* p = &svgpath->pts[i*2];
				if (i==0) {
					path.points.emplace_back(Vec2 {p[0], p[1]}-center);
					i++; continue;
				}

				path.cubic(Vec2 {p[0], p[1]}-center, Vec2 {p[2],p[3]}-center, Vec2 {p[4], p[5]}-center, res);
			}

			path.closed = static_cast<bool>(svgpath->closed);
			if (shape->opacity>0 && shape->fill.type!=NSVG_PAINT_NONE) {
				path.fill=true;
				g->triangulate(path);
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

				g->triangulate(path);
			}
		}
	}

	nsvgDelete(image);

	delete[] str_copy;
}

void SVGObject::render(RenderTarget target) {
	for (Object<FillShader> const& obj: objs) {
		obj.render(target);
	}
}

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

