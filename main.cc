#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <png.h>
#include <zlib.h>
#include <cstdio>
#include <iostream>

GLuint shader(size_t, GLenum const[], char const* const[]);

GLfloat view[16];

int main() {
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	std::cout << "Compiled with libpng " << PNG_LIBPNG_VER_STRING << "; using libpng " << png_libpng_ver << ".\n" << std::flush;
	std::cout << "Compiled with zlib " << ZLIB_VERSION << "; using zlib " << zlib_version << ".\n" << std::flush;

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

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* window {glfwCreateWindow(1280, 960, "Pacman", nullptr, nullptr)};
	glfwSetWindowRefreshCallback(window, [](GLFWwindow* window) {
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		glViewport(0, 0, w, h);
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
		for (size_t i {0}; i < 16; ++i)
			view[i] = orthographic[i];
	});
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	gl3wInit();
	std::cout << "OpenGL " << glGetString(GL_VERSION) << ", GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << '\n' << std::flush;
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
		return -1;

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
	GLfloat const instance[] {
		0.25f,  0.5f,  0.5f, 0.0f,
		0.25f,  0.0f,  0.5f, 0.0f,
		0.25f, -0.5f,  0.5f, 0.0f,
		0.25f, -0.5f,  0.0f, 0.0f,
		0.25f, -0.5f, -0.5f, 0.0f,
		0.25f,  0.0f, -0.5f, 0.0f,
		0.25f,  0.5f, -0.5f, 0.0f,
		0.25f,  0.5f,  0.0f, 0.0f,
		0.25f,  0.0f,  0.0f, 0.0f
	};
	glCreateBuffers(1, &abo);
	glNamedBufferStorage(abo, sizeof(instance), nullptr, GL_DYNAMIC_STORAGE_BIT);

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

	while (!glfwWindowShouldClose(window)) {
		glNamedBufferSubData(abo, 0, sizeof(instance), instance);
		memcpy(u_buffer+u_offset[0], &view, sizeof(view));
		glNamedBufferSubData(ubo, 0, u_size, u_buffer);
		glClearBufferfv(GL_COLOR, 0, color);
		glClearBufferfv(GL_DEPTH, 0, depth);
		glDrawElementsInstanced(GL_TRIANGLES, sizeof(element)/sizeof(GLuint), GL_UNSIGNED_INT, 0, sizeof(instance)/sizeof(GLfloat)/4);
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	delete[] u_buffer;
	return 0;
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
