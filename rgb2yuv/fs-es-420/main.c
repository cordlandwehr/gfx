#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <fcntl.h>
#include <unistd.h>

#include <gbm.h>
#include <png.h>

#include <epoxy/gl.h>
#include <epoxy/egl.h>

EGLDisplay display;
EGLContext context;
struct gbm_device *gbm;
GLuint program;

void *readImage(const char *filename, int *width, int *height)
{
	char header[8];    // 8 is the maximum size that can be checked

        /* open file and test for it being a png */
        FILE *fp = fopen(filename, "rb");
	assert(fp);
        fread(header, 1, 8, fp);
        assert(!png_sig_cmp(header, 0, 8));

        /* initialize stuff */
        png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	assert(png_ptr);

        png_infop info_ptr = png_create_info_struct(png_ptr);
	assert(info_ptr);

        assert(!setjmp(png_jmpbuf(png_ptr)));

        png_init_io(png_ptr, fp);
        png_set_sig_bytes(png_ptr, 8);

        png_read_info(png_ptr, info_ptr);

        *width = png_get_image_width(png_ptr, info_ptr);
        *height = png_get_image_height(png_ptr, info_ptr);
        int color_type = png_get_color_type(png_ptr, info_ptr);
	assert(color_type == PNG_COLOR_TYPE_RGB_ALPHA);
        int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	assert(bit_depth == 8);
	int pitch = png_get_rowbytes(png_ptr, info_ptr);

        int number_of_passes = png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);

        /* read file */
        assert(!setjmp(png_jmpbuf(png_ptr)));

	png_bytep buffer = malloc(*height * pitch);
	void *ret = buffer;
	assert(buffer);
        png_bytep *row_pointers = malloc(sizeof(png_bytep) * *height);
	assert(row_pointers);
        for (int i = 0; i < *height; i++) {
                row_pointers[i] = buffer;
		buffer += pitch;
	}

        png_read_image(png_ptr, row_pointers);

        fclose(fp);
	free(row_pointers);
	return ret;
}

int writeImage(char* filename, int width, int height, int stride, void *buffer, char* title)
{
	int code = 0;
	FILE *fp = NULL;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;

	// Open file for writing (binary mode)
	fp = fopen(filename, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file %s for writing\n", filename);
		code = 1;
		goto finalise;
	}

	// Initialize write structure
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		fprintf(stderr, "Could not allocate write struct\n");
		code = 1;
		goto finalise;
	}

	// Initialize info structure
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fprintf(stderr, "Could not allocate info struct\n");
		code = 1;
		goto finalise;
	}

	// Setup Exception handling
	if (setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "Error during png creation\n");
		code = 1;
		goto finalise;
	}

	png_init_io(png_ptr, fp);

	// Write header (8 bit colour depth)
	png_set_IHDR(png_ptr, info_ptr, width, height,
		     8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	// Set title
	if (title != NULL) {
		png_text title_text;
		title_text.compression = PNG_TEXT_COMPRESSION_NONE;
		title_text.key = "Title";
		title_text.text = title;
		png_set_text(png_ptr, info_ptr, &title_text, 1);
	}

	png_write_info(png_ptr, info_ptr);

	// Write image data
	int i;
	for (i = 0; i < height; i++)
		png_write_row(png_ptr, (png_bytep)buffer + i * stride);

	// End write
	png_write_end(png_ptr, NULL);

finalise:
	if (fp != NULL) fclose(fp);
	if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);

	return code;
}

void init_context(void)
{
	assert(epoxy_has_egl_extension(EGL_NO_DISPLAY, "EGL_MESA_platform_gbm"));

	int fd = open("/dev/dri/renderD128", O_RDWR);
	assert(fd >= 0);

	gbm = gbm_create_device(fd);
	assert(gbm != NULL);

	assert((display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, gbm, NULL)) != EGL_NO_DISPLAY);

	EGLint majorVersion;
	EGLint minorVersion;
	assert(eglInitialize(display, &majorVersion, &minorVersion) == EGL_TRUE);
	assert(epoxy_has_egl_extension(display, "EGL_KHR_surfaceless_context"));

	assert(eglBindAPI(EGL_OPENGL_ES_API) == EGL_TRUE);

	const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
		EGL_NONE
	};
	assert((context = eglCreateContext(display, NULL, EGL_NO_CONTEXT, contextAttribs)) != EGL_NO_CONTEXT);

	assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_TRUE);

	assert(epoxy_has_gl_extension("GL_OES_viewport_array"));
}

GLuint LoadShader(const char *name, GLenum type)
{
	FILE *f;
	int size;
	char *buff;
	GLuint shader;
	GLint compiled;
	const GLchar *source[1];

	assert((f = fopen(name, "r")) != NULL);

	// get file size
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	assert((buff = malloc(size)) != NULL);
	assert(fread(buff, 1, size, f) == size);
	source[0] = buff;
	fclose(f);
	shader = glCreateShader(type);
	glShaderSource(shader, 1, source, &size);
	glCompileShader(shader);
	free(buff);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			fprintf(stderr, "Error compiling shader %s:\n%s\n", name, infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

GLuint link_program(GLuint vs, GLuint gs, GLuint fs)
{
	GLuint program;
	assert((program = glCreateProgram()) != 0);
	glAttachShader(program, vs);
	glAttachShader(program, gs);
	glAttachShader(program, fs);

	GLint linked;
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			fprintf(stderr, "Error linking program:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		exit(1);
	}

	return program;
}

void init_gles(void)
{
	GLuint vs, gs, fs;
	assert((vs = LoadShader("vs.glsl", GL_VERTEX_SHADER)) != 0);
	assert((gs = LoadShader("gs.glsl", GL_GEOMETRY_SHADER)) != 0);
	assert((fs = LoadShader("fs.glsl", GL_FRAGMENT_SHADER)) != 0);
	program = link_program(vs, gs, fs);
}

void CheckFrameBufferStatus(void)
{
  GLenum status;
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  switch(status) {
  case GL_FRAMEBUFFER_COMPLETE:
    printf("Framebuffer complete\n");
    break;
  case GL_FRAMEBUFFER_UNSUPPORTED:
    printf("Framebuffer unsuported\n");
    break;
  case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
    printf("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
    break;
  case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
    printf("GL_FRAMEBUFFER_MISSING_ATTACHMENT\n");
    break;
  case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
    printf("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS\n");
    break;
  default:
    printf("Framebuffer error\n");
  }
}

void rgb2yuv(const char *name)
{
	int w = 0, h = 0;
	void *data = readImage(name, &w, &h);
	assert(!(w & 1) && !(h & 1));
	printf("read image %s width=%d height=%d\n", name, w, h);

	glActiveTexture(GL_TEXTURE0);

	GLuint in_tex = 0;
        glGenTextures(1, &in_tex);
	glBindTexture(GL_TEXTURE_2D, in_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	assert(glGetError() == GL_NO_ERROR);
	free(data);

	GLuint out_tex = 0;
        glGenTextures(1, &out_tex);
	glBindTexture(GL_TEXTURE_2D, out_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h * 3 / 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	assert(glGetError() == GL_NO_ERROR);

	GLuint fbo;
	glGenFramebuffers(1, &fbo); 
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_tex, 0);

	CheckFrameBufferStatus();

	GLfloat vertex_attr[] = {
		-1,  1, 0, 0, 1,
		-1, -1, 0, 0, 0,
		 1, -1, 0, 1, 0,
		 1,  1, 0, 1, 1,
	};

	GLushort index[] = {
		0, 1, 3,
		1, 2, 3,
	};

	GLuint VBO[2];
	glGenBuffers(2, VBO);

	glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
	glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat) * 5, vertex_attr, GL_STATIC_DRAW);
	GLint pos = glGetAttribLocation(program, "positionIn");
	glEnableVertexAttribArray(pos);
	glVertexAttribPointer(pos, 3, GL_FLOAT, 0, 20, 0);
	GLint tex = glGetAttribLocation(program, "texcoordIn");
	glEnableVertexAttribArray(tex);
	glVertexAttribPointer(tex, 2, GL_FLOAT, 0, 20, (void *)12);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, VBO[1]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(GLushort), index, GL_STATIC_DRAW);

	GLint tex_map = glGetUniformLocation(program, "texMap");
	glProgramUniform1i(program, tex_map, 0); // GL_TEXTURE0

	assert(glGetError() == GL_NO_ERROR);

	GLfloat viewports[] = {
		0, 0, w, h,
		0, h, w / 2, h / 2,
		w / 2, h, w / 2, h / 2,
	};
	glViewportArrayv(0, 3, viewports);

	glBindTexture(GL_TEXTURE_2D, in_tex);

	glUseProgram(program);

	assert(glGetError() == GL_NO_ERROR);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

	data = malloc(w * h * 3 / 2);
	assert(data);

	glReadPixels(0, 0, w, h * 3 / 2, GL_RED, GL_UNSIGNED_BYTE, data);
	assert(glGetError() == GL_NO_ERROR);

	assert(!writeImage("y.png", w, h, w, data, "hello"));
	assert(!writeImage("u.png", w / 2, h / 2, w, data + h * w, "hello"));
	assert(!writeImage("v.png", w / 2, h / 2, w, data + h * w + w / 2, "hello"));

	free(data);
}

static void usage(const char *name)
{
	printf("usage: %s [xxx.png]\n", name);
}

int main(int argc, char **argv)
{
	if (argc > 2) {
		usage(argv[0]);
		return 0;
	}

	init_context();
	init_gles();
        rgb2yuv(argc == 2 ? argv[1] : "test.png");
	return 0;
}
