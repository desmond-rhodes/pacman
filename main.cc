#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <png.h>
#include <zlib.h>
#include <cstdio>
#include <iostream>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <chrono>

GLuint shader(size_t, GLenum const[], char const* const[]);

void tick_reset(double);
void tick_modify(double);
double tick();

GLFWwindow* window;
std::atomic<bool> terminate;

GLfloat view[16];
int winfo_w;
int winfo_h;
std::shared_mutex winfo_lock;

namespace key { enum { right, left, up, down, w, a, s, d, zero }; }

namespace data {
	size_t constexpr key_press_size {9};
	bool key_press[key_press_size];
	std::chrono::time_point<std::chrono::steady_clock> key_press_time;
	std::shared_mutex key_press_lock;

	size_t constexpr instance_size {4};
	GLfloat instance_old[instance_size];
	GLfloat instance_new[instance_size];
	double instance_time_old;
	double instance_time_new;
	std::shared_mutex instance_lock;
}

void simulate();
void render();

int main() {
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	std::cout << "Compiled with libpng " << PNG_LIBPNG_VER_STRING << "; using libpng " << png_libpng_ver << ".\n" << std::flush;
	std::cout << "Compiled with zlib " << ZLIB_VERSION << "; using zlib " << zlib_version << ".\n" << std::flush;

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow(1280, 960, "Pacman", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	gl3wInit();
	std::cout << "OpenGL " << glGetString(GL_VERSION) << ", GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << '\n' << std::flush;
	glfwMakeContextCurrent(nullptr);

	glfwSetWindowRefreshCallback(window, [](GLFWwindow* window) {
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		GLfloat r {1.0f}, l {-1.0f}, t {1.0f}, b {-1.0f};
		if (w > h)
			l = -(r =  static_cast<GLfloat>(w) / h);
		else
			b = -(t =  static_cast<GLfloat>(h) / w);
		GLfloat const n {-1.0f};
		GLfloat const f { 1.0f};
		GLfloat orthographic[] {
			2.0f/(r-l),       0.0f,        0.0f, -(r+l)/(r-l),
			      0.0f, 2.0f/(t-b),        0.0f, -(t+b)/(t-b),
			      0.0f,       0.0f, -2.0f/(f-n), -(f+n)/(f-n),
			      0.0f,       0.0f,        0.0f,         1.0f
		};
		std::unique_lock winfo_write {winfo_lock};
		memcpy(view, orthographic, sizeof(orthographic));
		winfo_w = w;
		winfo_h = h;
	});

	glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
		static double tick_rate {1.0};
		if (key == GLFW_KEY_T && action == GLFW_PRESS) {
			tick_rate += (mods == GLFW_MOD_SHIFT) ? -0.5 : 0.5;
			if (tick_rate > 5.0) tick_rate = 0.0;
			if (tick_rate < 0.0) tick_rate = 5.0;
			tick_modify(tick_rate);
			std::cout << "TICK RATE " << tick_rate << "\n" << std::flush;
		}

		if (action != GLFW_PRESS && action != GLFW_RELEASE)
			return;
		bool const state {action == GLFW_PRESS};
		int id;
		switch (key) {
		case GLFW_KEY_RIGHT: id = key::right; break;
		case GLFW_KEY_LEFT : id = key::left ; break;
		case GLFW_KEY_UP   : id = key::up   ; break;
		case GLFW_KEY_DOWN : id = key::down ; break;
		case GLFW_KEY_W    : id = key::w    ; break;
		case GLFW_KEY_A    : id = key::a    ; break;
		case GLFW_KEY_S    : id = key::s    ; break;
		case GLFW_KEY_D    : id = key::d    ; break;
		case GLFW_KEY_0    : id = key::zero ; break;
		default: return;
		}
		std::unique_lock key_press_write {data::key_press_lock};
		data::key_press[id] = state;
		data::key_press_time = std::chrono::steady_clock::now();
	});

	tick_reset(1.0);

	{
		std::unique_lock key_press_write {data::key_press_lock};
		for (size_t i {0}; i < data::key_press_size; ++i)
			data::key_press[i] = false;
		data::key_press_time = std::chrono::steady_clock::now();
	}

	{
		GLfloat const instance[] {0.25f,  0.0f,  0.0f, 0.0f};
		auto const tick_now {tick()};
		std::unique_lock instance_write {data::instance_lock};
		memcpy(data::instance_old, instance, sizeof(instance));
		memcpy(data::instance_new, instance, sizeof(instance));
		data::instance_time_old = tick_now;
		data::instance_time_new = tick_now;
	}

	terminate.store(false, std::memory_order_relaxed);
	std::thread thread_simulate {simulate};
	std::thread thread_render {render};
	while (!glfwWindowShouldClose(window)) {
		glfwWaitEvents();
	}
	terminate.store(true, std::memory_order_relaxed);
	thread_simulate.join();
	thread_render.join();

	return 0;
}

void simulate() {
	double move_cooldown_duration {0.5};

	bool key_press[data::key_press_size];
	{
		std::shared_lock key_press_read {data::key_press_lock};
		memcpy(key_press, data::key_press, sizeof(data::key_press));
	}
	bool key_press_deferred {false};
	auto key_press_deferred_time {std::chrono::steady_clock::now()};
	std::chrono::microseconds key_press_deferred_duration {33333};

	auto tick_last {tick()};

	while (!terminate.load(std::memory_order_relaxed)) {
		auto const time_now {std::chrono::steady_clock::now()};
		auto const tick_now {tick()};

		if (key_press_deferred) {
			if (key_press_deferred_time < time_now) {
				key_press_deferred = false;
				std::shared_lock key_press_read {data::key_press_lock};
				memcpy(key_press, data::key_press, sizeof(data::key_press));
			}
		}
		else {
			std::shared_lock key_press_read {data::key_press_lock};
			if (key_press_deferred_time < data::key_press_time) {
				key_press_deferred = true;
				key_press_deferred_time = time_now + key_press_deferred_duration;
			}
		}

		if (tick_now == tick_last)
			continue;
		tick_last = tick_now;

		if (!(
			key_press[key::right] || key_press[key::d] ||
			key_press[key::left ] || key_press[key::a] ||
			key_press[key::up   ] || key_press[key::w] ||
			key_press[key::down ] || key_press[key::s] ||
			key_press[key::zero ]
		))
			continue;

		GLfloat instance_old[data::instance_size];
		GLfloat instance_new[data::instance_size];
		double instance_time_old;
		double instance_time_new;
		{
			std::shared_lock instance_read {data::instance_lock};
			if (tick_now < data::instance_time_new && !key_press[key::zero])
				continue;
			memcpy(instance_old, data::instance_old, sizeof(data::instance_old));
			memcpy(instance_new, data::instance_new, sizeof(data::instance_new));
			instance_time_old = data::instance_time_old;
			instance_time_new = data::instance_time_new;
		}

		if (key_press[key::zero]) {
			instance_new[1] = 0.0f;
			instance_new[2] = 0.0f;
			memcpy(instance_old, instance_new, sizeof(instance_new));
			instance_time_new = tick_now;
		}
		else {
			memcpy(instance_old, instance_new, sizeof(instance_new));
			instance_time_old = tick_now;
			if (key_press[key::right] || key_press[key::d]) instance_new[1] += 0.5f;
			if (key_press[key::left ] || key_press[key::a]) instance_new[1] -= 0.5f;
			if (key_press[key::up   ] || key_press[key::w]) instance_new[2] += 0.5f;
			if (key_press[key::down ] || key_press[key::s]) instance_new[2] -= 0.5f;
			instance_time_new = tick_now + move_cooldown_duration;
		}

		{
			std::unique_lock instance_write {data::instance_lock};
			memcpy(data::instance_old, instance_old, sizeof(instance_old));
			memcpy(data::instance_new, instance_new, sizeof(instance_new));
			data::instance_time_old = instance_time_old;
			data::instance_time_new = instance_time_new;
		}
	}
}

void render() {
	FILE* file {fopen("image.png", "rb+")};
	auto png_ptr {png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr)};
	auto info_ptr {png_create_info_struct(png_ptr)};
	png_init_io(png_ptr, file);
	png_read_info(png_ptr, info_ptr);

	png_uint_32 img_w;
	png_uint_32 img_h;
	int bit_depth;
	int color_type;
	png_get_IHDR(png_ptr, info_ptr, &img_w, &img_h, &bit_depth, &color_type, nullptr, nullptr, nullptr);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png_ptr);
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	if (bit_depth == 16)
		png_set_strip_16(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
		png_set_swap_alpha(png_ptr);
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY)
		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_BEFORE);
	double gamma;
	if (png_get_gAMA(png_ptr, info_ptr, &gamma))
		png_set_gamma(png_ptr, 2.2, gamma);
	png_read_update_info(png_ptr, info_ptr);

	auto const rowbyte {static_cast<png_uint_32>(png_get_rowbytes(png_ptr, info_ptr))};
	auto const img_data {new unsigned char[rowbyte*img_h]};
	auto const row_pointer {new png_bytep[img_h]};
	for (size_t i {0}; i < img_h; ++i)
		row_pointer[i] = img_data + i * rowbyte;
	png_read_image(png_ptr, row_pointer);

	png_read_end(png_ptr, nullptr);
	delete[] row_pointer;
	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	fclose(file);

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	glEnable(GL_DEPTH_TEST);

	GLuint tex;
	glCreateTextures(GL_TEXTURE_2D, 1, &tex);
	glTextureStorage2D(tex, 1, GL_RGBA8, img_w, img_h);
	glTextureSubImage2D(tex, 0, 0, 0, img_w, img_h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, img_data);
	delete[] img_data;

	GLuint sam;
	glCreateSamplers(1, &sam);
	glSamplerParameteri(sam, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glSamplerParameteri(sam, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glSamplerParameteri(sam, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glSamplerParameteri(sam, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	GLenum const src_t[] {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
	char const* const src[] {
		"#version 450 core\n"
		"layout (binding = 0, row_major) uniform ubo {"
		"	mat4 transform;"
		"};"
		"layout (location = 0) in vec4 v_position;"
		"layout (location = 1) in vec2 v_texcoord;"
		"layout (location = 2) in float scale;"
		"layout (location = 3) in vec3 translate;"
		"out vec2 f_texcoord;"
		"void main() {"
		"	vec4 position = v_position;"
		"	position.xy *= scale;"
		"	position.xyz += translate;"
		"	gl_Position = transform * position;"
		"	f_texcoord = v_texcoord;"
		"}"
	,
		"#version 450 core\n"
		"layout (binding = 0) uniform sampler2D tex;"
		"in vec2 f_texcoord;"
		"out vec4 color;"
		"void main() {"
		"	color = texture(tex, f_texcoord);"
		"}"
	};
	auto const sha {shader(2, src_t, src)};
	if (!sha)
		// return -1;
		return;

	GLuint vao;
	glCreateVertexArrays(1, &vao);
	glVertexArrayBindingDivisor(vao, 1, 1);
	glEnableVertexArrayAttrib(vao, 0);
	glEnableVertexArrayAttrib(vao, 1);
	glEnableVertexArrayAttrib(vao, 2);
	glEnableVertexArrayAttrib(vao, 3);
	glVertexArrayAttribBinding(vao, 0, 0);
	glVertexArrayAttribBinding(vao, 1, 0);
	glVertexArrayAttribBinding(vao, 2, 1);
	glVertexArrayAttribBinding(vao, 3, 1);
	glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 3*sizeof(GLfloat));
	glVertexArrayAttribFormat(vao, 2, 1, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(vao, 3, 3, GL_FLOAT, GL_FALSE, 1*sizeof(GLfloat));

	GLuint vbo;
	GLfloat const vertex[] {
		 1.0f,  1.0f, 0.0f, 1.0f, 1.0f - 1.0f,
		-1.0f,  1.0f, 0.0f, 0.0f, 1.0f - 1.0f,
		-1.0f, -1.0f, 0.0f, 0.0f, 1.0f - 0.0f,
		 1.0f, -1.0f, 0.0f, 1.0f, 1.0f - 0.0f
	};
	glCreateBuffers(1, &vbo);
	glNamedBufferStorage(vbo, sizeof(vertex), vertex, 0);

	GLuint ebo;
	GLuint const element[] {
		0, 1, 2, 0, 2, 3
	};
	glCreateBuffers(1, &ebo);
	glNamedBufferStorage(ebo, sizeof(element), element, 0);

	GLuint abo;
	glCreateBuffers(1, &abo);
	glNamedBufferStorage(abo, data::instance_size*sizeof(GLfloat), nullptr, GL_DYNAMIC_STORAGE_BIT);

	auto const ubo_index {glGetProgramResourceIndex(sha, GL_UNIFORM_BLOCK, "ubo")};
	GLint u_size;
	char const* const u_name[] {"transform"};
	GLuint u_indices[1];
	GLint u_offset[1];
	glGetActiveUniformBlockiv(sha, ubo_index, GL_UNIFORM_BLOCK_DATA_SIZE, &u_size);
	glGetUniformIndices(sha, 1, u_name, u_indices);
	glGetActiveUniformsiv(sha, 1, u_indices, GL_UNIFORM_OFFSET, u_offset);
	auto const u_buffer {new char[u_size]};

	GLuint ubo;
	glCreateBuffers(1, &ubo);
	glNamedBufferStorage(ubo, u_size, nullptr, GL_DYNAMIC_STORAGE_BIT);

	GLfloat const color[] {0.0f, 0.0f, 0.0f, 0.0f};
	GLfloat const depth[] {1.0f};

	glBindTextureUnit(0, tex);
	glBindSampler(0, sam);
	glUseProgram(sha);
	glBindVertexArray(vao);
	glVertexArrayVertexBuffer(vao, 0, vbo, 0, 5*sizeof(GLfloat));
	glVertexArrayElementBuffer(vao, ebo);
	glVertexArrayVertexBuffer(vao, 1, abo, 0, 4*sizeof(GLfloat));
	glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, 0, u_size);

	while (!terminate.load(std::memory_order_relaxed)) {
		auto const tick_now {tick()};

		{
			std::shared_lock winfo_read {winfo_lock};
			memcpy(u_buffer+u_offset[0], &view, sizeof(view));
			glViewport(0, 0, winfo_w, winfo_h);
		}
		GLfloat instance_old[data::instance_size];
		GLfloat instance_new[data::instance_size];
		double instance_time_old;
		double instance_time_new;
		{
			std::shared_lock instance_read {data::instance_lock};
			memcpy(instance_old, data::instance_old, sizeof(data::instance_old));
			memcpy(instance_new, data::instance_new, sizeof(data::instance_new));
			instance_time_old = data::instance_time_old;
			instance_time_new = data::instance_time_new;
		}

		GLfloat instance_range[data::instance_size];
		for (size_t i {0}; i < data::instance_size; ++i)
			instance_range[i] = instance_new[i] - instance_old[i];

		double const percent {std::max(std::min((tick_now-instance_time_old)/(instance_time_new-instance_time_old), 1.0), 0.0)};

		GLfloat instance[data::instance_size];
		for (size_t i {0}; i < data::instance_size; ++i)
			instance[i] = instance_old[i] + instance_range[i] * percent;

		glNamedBufferSubData(abo, 0, sizeof(instance), instance);
		glNamedBufferSubData(ubo, 0, u_size, u_buffer);
		glClearBufferfv(GL_COLOR, 0, color);
		glClearBufferfv(GL_DEPTH, 0, depth);
		glDrawElementsInstanced(GL_TRIANGLES, sizeof(element)/sizeof(GLuint), GL_UNSIGNED_INT, 0, data::instance_size/4);
		glfwSwapBuffers(window);
	}

	delete[] u_buffer;
}

GLuint shader(size_t num, GLenum const src_t[], char const* const src[]) {
	auto const inf {[](GLuint o, GLenum u){GLint x; glGetShaderiv(o, u, &x); return x;}};
	auto const obj {new GLuint[num]};
	bool fail {false};
	for (size_t i {0}; i < num; ++i) {
		obj[i] = glCreateShader(src_t[i]);
		glShaderSource(obj[i], 1, src+i, nullptr);
		glCompileShader(obj[i]);
		if (inf(obj[i], GL_COMPILE_STATUS))
			continue;
		fail = true;
		auto const len {inf(obj[i], GL_INFO_LOG_LENGTH)};
		auto const log {new char[len]};
		glGetShaderInfoLog(obj[i], len, nullptr, log);
		std::cerr << log << '\n' << std::flush;
		delete[] log;
	}
	GLuint pro {0};
	if (!fail) {
		pro = glCreateProgram();
		for (size_t i {0}; i < num; ++i)
			glAttachShader(pro, obj[i]);
		glLinkProgram(pro);
		for (size_t i {0}; i < num; ++i)
			glDetachShader(pro, obj[i]);
	}
	for (size_t i {0}; i < num; ++i)
		glDeleteShader(obj[i]);
	delete[] obj;
	return pro;
}

double tick_cumulative;
std::chrono::time_point<std::chrono::steady_clock> time_zero;
double tick_rate;
std::shared_mutex tick_lock;

double tick_time(double cumulative, std::chrono::time_point<std::chrono::steady_clock> zero, double rate, std::chrono::time_point<std::chrono::steady_clock> now) {
	return cumulative + std::chrono::duration_cast<std::chrono::nanoseconds>(now-zero).count() / 1000000000.0 * rate;
}

void tick_reset(double rate) {
	std::unique_lock tick_write {tick_lock};
	tick_cumulative = 0.0;
	time_zero = std::chrono::steady_clock::now();
	tick_rate = rate;
}

void tick_modify(double rate) {
	std::unique_lock tick_write {tick_lock};
	auto const time_now {std::chrono::steady_clock::now()};
	tick_cumulative = tick_time(tick_cumulative, time_zero, tick_rate, time_now);
	time_zero = time_now;
	tick_rate = rate;
}

double tick() {
	std::shared_lock tick_read {tick_lock};
	return tick_time(tick_cumulative, time_zero, tick_rate, std::chrono::steady_clock::now());
}
