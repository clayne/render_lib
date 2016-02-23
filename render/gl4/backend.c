/* backend.c  -  Render library  -  Public Domain  -  2014 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform rendering library in C11 providing
 * basic 2D/3D rendering functionality for projects based on our foundation library.
 *
 * The latest source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/render_lib
 *
 * The dependent library source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <foundation/foundation.h>
#include <window/window.h>
#include <resource/hashstrings.h>
#include <render/render.h>
#include <render/internal.h>

#if FOUNDATION_PLATFORM_WINDOWS || FOUNDATION_PLATFORM_MACOSX || ( FOUNDATION_PLATFORM_LINUX && !FOUNDATION_PLATFORM_LINUX_RASPBERRYPI )

#include <render/gl4/glwrap.h>

#define GET_BUFFER(id) objectmap_lookup(_render_map_buffer, (id))

#if RENDER_ENABLE_NVGLEXPERT
#  include <nvapi.h>

static void
nvoglexpert_callback(unsigned int category, unsigned int id, unsigned int detail, int object,
                     const char* msg) {
	log_warnf(HASH_RENDER,
	          STRING_CONST("nVidia OpenGL Expert error: Category 0x%08x, Message 0x%08x : %s"), category, id,
	          msg);
}

#endif

#include <render/gl4/glprocs.h>

typedef struct render_backend_gl4_t {
	RENDER_DECLARE_BACKEND;

	void* context;
#if FOUNDATION_PLATFORM_WINDOWS
	HDC hdc;
#endif
	render_resolution_t resolution;

	bool use_clear_scissor;
} render_backend_gl4_t;

const char*
_rb_gl_error_message(GLenum err) {
	switch (err) {
	case GL_NONE:
		return "GL_NONE";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_STACK_OVERFLOW:
		return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:
		return "GL_STACK_UNDERFLOW";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	case GL_INVALID_FRAMEBUFFER_OPERATION_EXT:
		return "GL_INVALID_FRAMEBUFFER_OPERATION";
	default:
		break;
	}
	return "<UNKNOWN>";
}

bool
_rb_gl_check_error(const char* message) {
	GLenum err = glGetError();
	if (err != GL_NONE) {
		log_errorf(HASH_RENDER, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("%s: %s"), message,
		           _rb_gl_error_message(err));
		FOUNDATION_ASSERT_FAILFORMAT("OpenGL error: %s: %s",
		                             message, _rb_gl_error_message(err));
		return true;
	}
	return false;
}

#if !FOUNDATION_BUILD_DEPLOY

void STDCALL
_rb_gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                      const GLchar* message, GLvoid* userParam) {
	log_debugf(HASH_RENDER, STRING_CONST("OpenGL debug message: %.*s"), (int)length, message);
}

#endif

void
_rb_gl_destroy_context(render_drawable_t* drawable, void* context) {
#if FOUNDATION_PLATFORM_WINDOWS
	if (context) {
		if (wglGetCurrentContext() == context)
			wglMakeCurrent(0, 0);
		wglDeleteContext((HGLRC)context);
	}
#elif FOUNDATOIN_PLATFORM_LINUX
	glXDestroyContext(drawable->display, context);
#elif FOUNDATION_PLATFORM_MACOSX
	_rb_gl_destroy_agl_context(context);
#endif
}

void*
_rb_gl_create_context(render_drawable_t* drawable, int major, int minor, void* share_context) {
	if (drawable && (drawable->type == RENDERDRAWABLE_OFFSCREEN)) {
		log_error(HASH_RENDER, ERROR_NOT_IMPLEMENTED, STRING_CONST("Offscreen drawable not implemented"));
		return 0;
	}

#if FOUNDATION_PLATFORM_WINDOWS

	HDC hdc = 0;
	HGLRC hglrc_default = 0;
	HGLRC hglrc = 0;

	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
	pfd.nSize        = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion     = 1;
	pfd.dwFlags      = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
	pfd.iPixelType   = PFD_TYPE_RGBA;
	pfd.cColorBits   = 32;
	pfd.cDepthBits   = 24;
	pfd.cStencilBits = 0;
	pfd.iLayerType   = PFD_MAIN_PLANE;

	hdc = (HDC)drawable->hdc;
	if (!hdc) {
		log_warn(HASH_RENDER, WARNING_INVALID_VALUE,
		         STRING_CONST("Unable to create context, window has no device context"));
		return nullptr;
	}

	int pixelformat = ChoosePixelFormat(hdc, &pfd);
	SetPixelFormat(hdc, pixelformat, &pfd);
	hglrc_default = wglCreateContext(hdc);
	if (!hglrc_default) {
		log_warn(HASH_RENDER, WARNING_INVALID_VALUE,
		         STRING_CONST("Unable to create context, unable to create default GL context"));
		return nullptr;
	}

	wglMakeCurrent(hdc, hglrc_default);

	//Create real context
	int* attributes = 0;
	array_push(attributes, WGL_CONTEXT_MAJOR_VERSION_ARB); array_push(attributes, major);
	array_push(attributes, WGL_CONTEXT_MINOR_VERSION_ARB); array_push(attributes, minor);
	array_push(attributes, WGL_CONTEXT_FLAGS_ARB);
	array_push(attributes, 0); //WGL_CONTEXT_DEBUG_BIT_ARB
	array_push(attributes, WGL_CONTEXT_PROFILE_MASK_ARB);
	array_push(attributes, WGL_CONTEXT_CORE_PROFILE_BIT_ARB);
	array_push(attributes, 0);

	int err = 0;
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
	        _rb_gl_get_proc_address("wglCreateContextAttribsARB");
	if (wglCreateContextAttribsARB)
		hglrc = wglCreateContextAttribsARB(hdc, (HGLRC)share_context, attributes);

	if (!hglrc && (major < 3)) {
		hglrc = wglCreateContext(hdc);
		if (hglrc) {
			if (share_context) {
				wglShareLists(hglrc, (HGLRC)share_context);
				_rb_gl_check_error("Unable to share GL render contexts");
			}

			wglMakeCurrent(hdc, hglrc);

			const char* version = (const char*)glGetString(GL_VERSION);
			int have_major = 0, have_minor = 0, have_revision = 0;
			string_const_t version_arr[3];
			size_t arrsize = string_explode(version, string_length(version), STRING_CONST("."),
			                                version_arr, 3, false);

			have_major    = (arrsize > 0) ? string_to_uint(STRING_ARGS(version_arr[0]), false) : 0;
			have_minor    = (arrsize > 1) ? string_to_uint(STRING_ARGS(version_arr[1]), false) : 0;
			have_revision = (arrsize > 2) ? string_to_uint(STRING_ARGS(version_arr[2]), false) : 0;

			bool supported = (have_major > major);
			if (!supported && ((have_major == major) && (have_minor >= minor)))
				supported = true;

			if (!supported) {
				log_warnf(HASH_RENDER, WARNING_UNSUPPORTED,
				          STRING_CONST("GL version %d.%d not supported, got %d.%d (%s)"),
				          major, minor, have_major, have_minor, version);
				wglMakeCurrent(0, 0);
				wglDeleteContext(hglrc);
				hglrc = 0;
			}
		}
	}

	if (hglrc) {
		wglMakeCurrent(hdc, hglrc);
	}
	else {
		if (major >= 3) {
			int err = GetLastError();
			log_infof(HASH_RENDER, STRING_CONST("Unable to create GL context for version %d.%d: %s (%08x)"),
			          major, minor, system_error_message(err), err);
		}
		wglMakeCurrent(0, 0);
	}
	wglDeleteContext(hglrc_default);

	array_deallocate(attributes);

	return hglrc;

#elif FOUNDATION_PLATFORM_LINUX

	GLXContext context = 0;
	GLXFBConfig* fbconfig;
	Display* display = 0;
	int screen = 0;
	int numconfig = 0;
	bool verify_only = false;

	if (!drawable) {
		display = XOpenDisplay(0);
		screen = DefaultScreen(display);
		verify_only = true;
	}
	else {
		display = drawable->display;
		screen = drawable->screen;
	}

	if (glXQueryExtension(display, 0, 0) != True) {
		log_warn(HASH_RENDER, WARNING_UNSUPPORTED, STRING_CONST("Unable to query GLX extension"));
		goto failed;
	}

	int major_glx = 0, minor_glx = 0;
	glXQueryVersion(display, &major_glx, &minor_glx);
	if (!verify_only)
		log_debugf(HASH_RENDER, "GLX version %d.%d", major_glx, minor_glx);

	fbconfig = glXGetFBConfigs(display, screen, &numconfig);
	if (!verify_only)
		log_debugf(HASH_RENDER, "Got %d configs", numconfig);
	if (fbconfig && (numconfig > 0)) {
		PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribs = (PFNGLXCREATECONTEXTATTRIBSARBPROC)
		                                                            _rb_gl_get_proc_address("glXCreateContextAttribsARB");
		if (glXCreateContextAttribs) {
			int* attributes = 0;
			array_push(attributes, GLX_CONTEXT_MAJOR_VERSION_ARB); array_push(attributes, major);
			array_push(attributes, GLX_CONTEXT_MINOR_VERSION_ARB); array_push(attributes, minor);
			array_push(attributes, GLX_CONTEXT_FLAGS_ARB); array_push(attributes, 0);
			array_push(attributes, GLX_CONTEXT_PROFILE_MASK_ARB);
			array_push(attributes, GLX_CONTEXT_CORE_PROFILE_BIT_ARB);
			array_push(attributes, 0); array_push(attributes, 0);

			for (int ic = 0; ic < numconfig; ++ic) {
				context = glXCreateContextAttribs(display, fbconfig[ic], 0, true, attributes);
				if (context)
					break;
			}

			array_deallocate(attributes);
		}
		else {
			log_warn(HASH_RENDER, WARNING_UNSUPPORTED,
			         STRING_CONST("Unable to get glXCreateContextAttribs proc address"));
		}
	}

failed:

	if (!drawable) {
		glXDestroyContext(display, context);
		XCloseDisplay(display);
	}

	return context;

#elif FOUNDATION_PLATFORM_MACOSX

	bool supported = false;

	CGDirectDisplayID display = CGMainDisplayID();
	void* view = (drawable ? drawable->view : 0);
	if (drawable) {
		//TODO: Get display mask from view
		//display = CGOpenGLDisplayMaskToDisplayID( _adapter._id );
	}
	unsigned int displaymask = CGDisplayIDToOpenGLDisplayMask(display);
	void* context = _rb_gl_create_agl_context(view, displaymask, 32/*color_depth*/, 24/*_res._depth*/,
	                                          8/*_res._stencil*/);
	if (!context) {
		log_warn(HASH_RENDER, WARNING_UNSUPPORTED, STRING_CONST("Unable to create OpenGL context"));
		goto failed;
	}

	const char* version = (const char*)glGetString(GL_VERSION);
	int have_major = 0, have_minor = 0, have_revision = 0;
	char** version_arr = string_explode(version, ".", false);

	have_major    = (array_size(version_arr) > 0) ? string_to_uint(version_arr[0], false) : 0;
	have_minor    = (array_size(version_arr) > 1) ? string_to_uint(version_arr[1], false) : 0;
	have_revision = (array_size(version_arr) > 2) ? string_to_uint(version_arr[2], false) : 0;

	string_array_deallocate(version_arr);

	supported = (have_major > major);
	if (!supported && ((have_major == major) && (have_minor >= minor)))
		supported = true;

	if (!supported) {
		log_infof(HASH_RENDER, STRING_CONST("GL version %d.%d not supported, got %d.%d (%s)"), major, minor,
		          have_major, have_minor, version);
		goto failed;
	}

failed:

	if ((!supported || !drawable) && context)
		_rb_gl_destroy_agl_context(context);

	return supported ? context : 0;

#else
#  error Not implemented
#endif
}

bool
_rb_gl_check_context(int major, int minor) {
	void* context = 0;

#if FOUNDATION_PLATFORM_WINDOWS

	window_t* window_check = window_create(WINDOW_ADAPTER_DEFAULT, STRING_CONST("__render_gl_check"),
	                                       10, 10, false);
	render_drawable_t* drawable = render_drawable_allocate();
	render_drawable_set_window(drawable, window_check);
	context = _rb_gl_create_context(drawable, major, minor, 0);
	window_deallocate(window_check);
	render_drawable_deallocate(drawable);
	wglMakeCurrent(0, 0);
	if (context)
		wglDeleteContext((HGLRC)context);

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOSX

	context = _rb_gl_create_context(0, major, minor, 0);

#else
#  error Not implemented
#endif

	return (context != 0);
}

bool
_rb_gl_check_extension(const char* name) {
	return false;
}

static bool
_rb_gl4_construct(render_backend_t* backend) {
	//TODO: Caps check
	//if( !... )
	//  return false;

	log_debug(HASH_RENDER, STRING_CONST("Constructed GL4 render backend"));
	return true;
}

static void
_rb_gl4_destruct(render_backend_t* backend) {
	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;
	if (backend_gl4->context)
		_rb_gl_destroy_context(backend_gl4->drawable, backend_gl4->context);
	backend_gl4->context = 0;

	log_debug(HASH_RENDER, STRING_CONST("Destructed GL4 render backend"));
}

static bool
_rb_gl4_set_drawable(render_backend_t* backend, render_drawable_t* drawable) {
	if (!FOUNDATION_VALIDATE_MSG(drawable->type != RENDERDRAWABLE_OFFSCREEN,
	                             "Offscreen drawable not implemented"))
		return error_report(ERRORLEVEL_ERROR, ERROR_NOT_IMPLEMENTED);

	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;
	if (!FOUNDATION_VALIDATE_MSG(!backend_gl4->context, "Drawable switching not supported yet"))
		return error_report(ERRORLEVEL_ERROR, ERROR_NOT_IMPLEMENTED);

	backend_gl4->context = _rb_gl_create_context(drawable, 4, 0, 0);
	if (!backend_gl4->context) {
		log_error(HASH_RENDER, ERROR_UNSUPPORTED, STRING_CONST("Unable to create OpenGL 4 context"));
		return false;
	}

#if FOUNDATION_PLATFORM_WINDOWS

	backend_gl4->hdc = (HDC)drawable->hdc;

#endif

#if FOUNDATION_PLATFORM_LINUX

	glXMakeCurrent(drawable->display, drawable->drawable, backend_gl4->context);

	if (True == glXIsDirect(drawable->display, backend_gl4->context))
		log_debug(HASH_RENDER, STRING_CONST("Direct rendering enabled"));
	else
		log_warn(HASH_RENDER, WARNING_PERFORMANCE, STRING_CONST("Indirect rendering"));

#endif

#if RENDER_ENABLE_NVGLEXPERT
	NvAPI_OGL_ExpertModeSet(20, NVAPI_OGLEXPERT_REPORT_ALL, NVAPI_OGLEXPERT_OUTPUT_TO_CALLBACK,
	                        nvoglexpert_callback);
#endif

#if BUILD_ENABLE_LOG
	const char* vendor   = (const char*)glGetString(GL_VENDOR);
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	const char* version  = (const char*)glGetString(GL_VERSION);
	const char* ext      = (const char*)glGetString(GL_EXTENSIONS);
	glGetError();

	log_infof(HASH_RENDER, STRING_CONST("Vendor:     %s"), vendor ? vendor : "<unknown>");
	log_infof(HASH_RENDER, STRING_CONST("Renderer:   %s"), renderer ? renderer : "<unknown>");
	log_infof(HASH_RENDER, STRING_CONST("Version:    %s"), version ? version : "<unknown>");
	log_infof(HASH_RENDER, STRING_CONST("Extensions: %s"), ext ? ext : "<none>");
#endif

#if FOUNDATION_PLATFORM_WINDOWS
	const char* wglext = 0;
	PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = 0;
	PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsStringEXT = 0;
	if ((wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)
	                                 _rb_gl_get_proc_address("wglGetExtensionsStringARB")) != 0)
		wglext = wglGetExtensionsStringARB((HDC)drawable->hdc);
	else if ((wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)
	                                      _rb_gl_get_proc_address("wglGetExtensionsStringEXT")) != 0)
		wglext = wglGetExtensionsStringEXT();
	log_debugf(HASH_RENDER, STRING_CONST("WGL Extensions: %s"), wglext ? wglext : "<none>");
#endif

	if (!_rb_gl_get_standard_procs(4, 0))
		return false;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glReadBuffer(GL_BACK);
	glDrawBuffer(GL_BACK);

	glViewport(0, 0, render_drawable_width(drawable), render_drawable_height(drawable));

	_rb_gl_check_error("Error setting up default state");

	return true;
}

FOUNDATION_DECLARE_THREAD_LOCAL(void*, gl4_context, 0);

static void
_rb_gl4_enable_thread(render_backend_t* backend) {
	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;

	void* thread_context = get_thread_gl4_context();
	if (!thread_context) {
		thread_context = _rb_gl_create_context(backend->drawable, 4, 0, backend_gl4->context);
		set_thread_gl4_context(thread_context);
	}

#if NEO_PLATFORM_WINDOWS
	if (!wglMakeCurrent((HDC)backend->drawable->hdc, (HGLRC)thread_context))
		_rb_gl_check_error("Unable to enable thread for rendering");
	else
		log_debug(HASH_RENDER, STRING_CONST("Enabled thread for GL4 rendering"));
#elif NEO_PLATFORM_LINUX
	glXMakeCurrent(backend->drawable->display, backend->drawable->drawable, thread_context);
	_rb_gl_check_error("Unable to enable thread for rendering");
#else
	FOUNDATION_ASSERT_FAIL("Platform not implemented");
	error_report(ERRORLEVEL_ERROR, ERROR_NOT_IMPLEMENTED);
#endif
}

static void
_rb_gl4_disable_thread(render_backend_t* backend) {
	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;

	void* thread_context = get_thread_gl4_context();
	if (thread_context) {
		_rb_gl_destroy_context(backend_gl4->drawable, thread_context);
		log_debug(HASH_RENDER, STRING_CONST("Disabled thread for GL4 rendering"));
	}
	set_thread_gl4_context(0);
}

unsigned int*
_rb_gl4_enumerate_adapters(render_backend_t* backend) {
	unsigned int* adapters = 0;
	array_push(adapters, WINDOW_ADAPTER_DEFAULT);
	return adapters;
}

render_resolution_t*
_rb_gl4_enumerate_modes(render_backend_t* backend, unsigned int adapter) {
	render_resolution_t* modes = 0;
	FOUNDATION_UNUSED(backend);

#if FOUNDATION_PLATFORM_LINUX

	FOUNDATION_ASSERT_MSG(adapter == WINDOW_ADAPTER_DEFAULT,
	                      "render_enumerate_modes not implemented when adapter is specified");

	Display* display = XOpenDisplay(0);
	if (!display) {
		log_error(HASH_RENDER, ERROR_SYSTEM_CALL_FAIL,
		          STRING_CONST("Unable to enumerate modes, unable to open display"));
		goto exit;
	}

	int depths[] = { 15, 16, 24, 32 };
	for (int i = 0; i < 4; ++i) {
		int num = 0;
		XVisualInfo info;
		memset(&info, 0, sizeof(XVisualInfo));
		info.depth = depths[i];

		XVisualInfo* visual = XGetVisualInfo(display, VisualDepthMask, &info, &num);
		for (int v = 0; v < num; ++v) {
			XF86VidModeModeInfo** xmodes = 0;
			int nummodes = 0;

			int depth = 0, stencil = 0, color = 0;
			glXGetConfig(display, &visual[v], GLX_BUFFER_SIZE,  &color);
			glXGetConfig(display, &visual[v], GLX_DEPTH_SIZE,   &depth);
			glXGetConfig(display, &visual[v], GLX_STENCIL_SIZE, &stencil);

			if ((color < 24) || (depth < 15))
				continue;

			XF86VidModeGetAllModeLines(display, visual[v].screen, &nummodes, &xmodes);
			/*if( nummodes )
			qsort( xmodes, nummodes, sizeof( XF86VidModeModeInfo* ), Adapter::compareModes );*/

			for (int m = 0; m < nummodes; ++m) {
				if ((xmodes[m]->hdisplay < 600) || (xmodes[m]->vdisplay < 400))
					continue;

				int refresh = ((int)(0.5f + (1000.0f * xmodes[m]->dotclock) / (float)(
				                         xmodes[m]->htotal * xmodes[m]->vtotal)));

				if (XF86VidModeValidateModeLine(display, visual[v].screen,
				                                xmodes[m]) == 255)    // 255 = MODE_BAD according to XFree sources...
					continue;

				pixelformat_t format = PIXELFORMAT_R8G8B8A8;
				if (color == 24)
					format = PIXELFORMAT_R8G8B8;

				resolution_t mode = {
					0,
					xmodes[m]->hdisplay,
					xmodes[m]->vdisplay,
					format,
					COLORSPACE_LINEAR,
					refresh
				};

				bool found = false;
				for (int c = 0, size = array_size(modes); c < size; ++c) {
					if (!memcmp(modes + c, &mode, sizeof(resolution_t))) {
						found = true;
						break;
					}
				}
				if (!found)
					array_push(modes, mode);
			}

			XFree(xmodes);
		}

		XFree(visual);
	}

	//Sort and index modes
	for (int c = 0, size = array_size(modes); c < size; ++c) {
		modes[c].id = c;
	}

	if (!array_size(modes)) {
		log_warnf(HASH_RENDER, WARNING_SUSPICIOUS,
		          STRING_CONST("Unable to enumerate resolutions for adapter %d, adding default fallback"), adapter);
		render_resolution_t mode = {
			0,
			800,
			600,
			PIXELFORMAT_R8G8B8X8,
			COLORSPACE_LINEAR,
			60
		};
		array_push(modes, mode);
	}

exit:

	XCloseDisplay(display);

#else

	render_resolution_t mode = {
		0,
		800,
		600,
		PIXELFORMAT_R8G8B8X8,
		60
	};
	array_push(modes, mode);

#endif

	return modes;
}

static void*
_rb_gl4_allocate_buffer(render_backend_t* backend, render_buffer_t* buffer) {
	FOUNDATION_UNUSED(backend);
	return memory_allocate(HASH_RENDER, buffer->size * buffer->allocated, 16, MEMORY_PERSISTENT);
}

static void
_rb_gl4_deallocate_buffer(render_backend_t* backend, render_buffer_t* buffer, bool sys, bool aux) {
	FOUNDATION_UNUSED(backend);
	if (sys)
		memory_deallocate(buffer->store);

	if (aux && buffer->backend_data[0]) {
		GLuint buffer_object = (GLuint)buffer->backend_data[0];
		glDeleteBuffers(1, &buffer_object);
		buffer->backend_data[0] = 0;
	}
}

static bool
_rb_gl4_upload_buffer(render_backend_t* backend, render_buffer_t* buffer) {
	FOUNDATION_UNUSED(backend);
	if ((buffer->buffertype == RENDERBUFFER_PARAMETER) || (buffer->buffertype == RENDERBUFFER_STATE))
		return true;

	GLuint buffer_object = (GLuint)buffer->backend_data[0];
	if (!buffer_object) {
		glGenBuffers(1, &buffer_object);
		if (_rb_gl_check_error("Unable to create buffer object"))
			return false;
		buffer->backend_data[0] = buffer_object;
	}

	glBindBuffer(GL_ARRAY_BUFFER, buffer_object);
	glBufferData(GL_ARRAY_BUFFER, buffer->size * buffer->allocated, buffer->store,
	             (buffer->usage == RENDERUSAGE_DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	if (_rb_gl_check_error("Unable to upload buffer object data"))
		return false;

	buffer->flags &= ~RENDERBUFFER_DIRTY;
	return true;
}

static void
_rb_gl4_link_buffer(render_backend_t* backend, render_buffer_t* buffer, render_program_t* program) {
	FOUNDATION_UNUSED(backend);
	FOUNDATION_UNUSED(buffer);
	FOUNDATION_UNUSED(program);
}

static bool
_rb_gl4_upload_shader(render_backend_t* backend, render_shader_t* shader, const void* buffer,
                      size_t size) {
	bool ret = false;
	//render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;
	FOUNDATION_UNUSED(backend);

	//Shader backend data:
	//  0 - Shader object
	if (shader->backend_data[0])
		glDeleteShader((GLuint)shader->backend_data[0]);

	switch (shader->shadertype) {
	case SHADER_PIXEL:
	case SHADER_VERTEX: {
			bool is_pixel_shader = (shader->shadertype == SHADER_PIXEL);
			GLuint handle = glCreateShader(is_pixel_shader ? GL_FRAGMENT_SHADER_ARB : GL_VERTEX_SHADER_ARB);
			const GLchar* source = (GLchar*)buffer;
			GLint source_size = (GLint)size;
			glShaderSource(handle, 1, &source, &source_size);
			glCompileShader(handle);

			GLint compiled = 0;
			glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);

			if (!compiled) {
#if BUILD_DEBUG
				GLsizei log_capacity = 2048;
				GLchar* log_buffer = memory_allocate(HASH_RESOURCE, log_capacity, 0, MEMORY_TEMPORARY);
				GLint log_length = 0;
				GLint compiled = 0;
				glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);
				glGetShaderInfoLog(handle, log_capacity, &log_length, log_buffer);
				log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to compile shader: %.*s"),
				           (int)log_length, log_buffer);
				memory_deallocate(log_buffer);
#endif
				glDeleteShader(handle);
				shader->backend_data[0] = 0;
			}
			else {
				shader->backend_data[0] = handle;
				ret = true;
			}
		}
		break;

	default:
		break;
	}
	return ret;
}

static void
_rb_gl4_deallocate_shader(render_backend_t* backend, render_shader_t* shader) {
	FOUNDATION_UNUSED(backend);
	if (shader->backend_data[0])
		glDeleteShader((GLuint)shader->backend_data[0]);
	shader->backend_data[0] = 0;
}

static bool
_rb_gl4_check_program_link(GLuint handle) {
	GLint result = 0;
	glGetProgramiv(handle, GL_LINK_STATUS, &result);
	if (!result) {
		GLsizei buffer_size = 4096;
		GLint log_length = 0;
		GLchar* log = 0;

		glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &buffer_size);
		log = memory_allocate(HASH_RENDER, buffer_size + 1, 0, MEMORY_TEMPORARY);
		glGetProgramInfoLog(handle, buffer_size, &log_length, log);

		log_errorf(ERRORLEVEL_ERROR, ERROR_SYSTEM_CALL_FAIL,
		           STRING_CONST("Unable to compile program: %.*s"),
		           (int)log_length, log);
		memory_deallocate(log);

		glDeleteProgram(handle);

		return false;
	}
	return true;
}

static bool
_rb_gl4_upload_program(render_backend_t* backend, render_program_t* program) {
	FOUNDATION_UNUSED(backend);
	if (program->backend_data[0])
		glDeleteProgram((GLuint)program->backend_data[0]);

	GLint attributes = 0;
	GLint uniforms = 0;
	GLint ia, iu;
	GLsizei num_chars = 0;
	GLint size = 0;
	GLenum type = GL_NONE;
	hash_t name_hash;
	char name[256];
	GLuint handle = glCreateProgram();

	glAttachShader(handle, (GLuint)program->vertexshader->backend_data[0]);
	glAttachShader(handle, (GLuint)program->pixelshader->backend_data[0]);
	glLinkProgram(handle);
	if (!_rb_gl4_check_program_link(handle))
		return false;

	glGetProgramiv(handle, GL_ACTIVE_ATTRIBUTES, &attributes);
	for (ia = 0; ia < attributes; ++ia) {
		num_chars = 0;
		size = 0;
		type = GL_NONE;
		glGetActiveAttrib(handle, ia, sizeof(name), &num_chars, &size, &type, name);

		name_hash = hash(name, num_chars);
		for (size_t iattrib = 0; iattrib < program->attributes.num_attributes; ++iattrib) {
			if (program->attribute_name[iattrib] == name_hash) {
				render_vertex_attribute_t* attribute = program->attributes.attribute + iattrib;
				glBindAttribLocation(handle, attribute->binding, name);
				break;
			}
		}
	}
	glLinkProgram(handle);
	if (!_rb_gl4_check_program_link(handle))
		return false;

	glGetProgramiv(handle, GL_ACTIVE_UNIFORMS, &uniforms);
	for (iu = 0; iu < uniforms; ++iu) {
		num_chars = 0;
		size = 0;
		type = GL_NONE;
		glGetActiveUniform(handle, iu, sizeof(name), &num_chars, &size, &type, name);

		name_hash = hash(name, num_chars);
		for (size_t iparam = 0; iparam < program->parameters.num_parameters; ++iparam) {
			render_parameter_t* parameter = program->parameters.parameters + iparam;
			if (parameter->name == name_hash)
				parameter->location = glGetUniformLocation(handle, name);
		}
	}

	program->backend_data[0] = handle;

	return true;
}

static void
_rb_gl4_deallocate_program(render_backend_t* backend, render_program_t* program) {
	FOUNDATION_UNUSED(backend);
	if (program->backend_data[0])
		glDeleteProgram((GLuint)program->backend_data[0]);
	program->backend_data[0] = 0;
}

#if 0

static void _rb_gl4_upload_texture(render_backend_t* backend, render_texture_t* texture,
                                   unsigned int width, unsigned int height, unsigned int depth, unsigned int levels,
                                   const void* data) {
}

static render_texture_t* _rb_gl4_allocate_texture(render_backend_t* backend,
                                                  render_texture_type_t type, render_usage_t usage, pixelformat_t format, colorspace_t colorspace) {
	render_texture_t* texture = memory_allocate(HASH_RENDER, sizeof(render_texture_t), 0,
	                                            MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZE);
	texture->textype = type;
	texture->usage = usage;
	texture->format = format;
	texture->colorspace = colorspace;
	return texture;
}

static void _rb_gl4_deallocate_texture(render_backend_t* backend, render_texture_t* texture) {
	memory_deallocate(texture);
}

#endif

static void
_rb_gl4_clear(render_backend_gl4_t* backend, render_context_t* context, render_command_t* command) {
	unsigned int buffer_mask = command->data.clear.buffer_mask;
	unsigned int bits = 0;
	FOUNDATION_UNUSED(context);

	if (buffer_mask & RENDERBUFFER_COLOR) {
		unsigned int color_mask = command->data.clear.color_mask;
		uint32_t color = command->data.clear.color;
		glColorMask((color_mask & 0x01) ? GL_TRUE : GL_FALSE, (color_mask & 0x02) ? GL_TRUE : GL_FALSE,
		            (color_mask & 0x04) ? GL_TRUE : GL_FALSE, (color_mask & 0x08) ? GL_TRUE : GL_FALSE);
		bits |= GL_COLOR_BUFFER_BIT;
		//color_linear_t color = uint32_to_color( command->data.clear.color );
		//glClearColor( vector_x( color ), vector_y( color ), vector_z( color ), vector_w( color ) );
		glClearColor((float)(color & 0xFF) / 255.0f, (float)((color >> 8) & 0xFF) / 255.0f,
		             (float)((color >> 16) & 0xFF) / 255.0f, (float)((color >> 24) & 0xFF) / 255.0f);
	}

	if (buffer_mask & RENDERBUFFER_DEPTH) {
		glDepthMask(GL_TRUE);
		bits |= GL_DEPTH_BUFFER_BIT;
		glClearDepth(command->data.clear.depth);
	}

	if (buffer_mask & RENDERBUFFER_STENCIL) {
		glClearStencil(command->data.clear.stencil);
		bits |= GL_STENCIL_BUFFER_BIT;
	}

	if (backend->use_clear_scissor)
		glEnable(GL_SCISSOR_TEST);

	glClear(bits);

	if (backend->use_clear_scissor)
		glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	_rb_gl_check_error("Error clearing targets");
}

static void
_rb_gl4_viewport(render_backend_gl4_t* backend, render_context_t* context,
                 render_command_t* command) {
	int target_width = render_target_width(context->target);
	int target_height = render_target_height(context->target);

	GLint x = command->data.viewport.x;
	GLint y = command->data.viewport.y;
	GLsizei w = command->data.viewport.width;
	GLsizei h = command->data.viewport.height;

	glViewport(x, y, w, h);
	glScissor(x, y, w, h);

	backend->use_clear_scissor = (x || y || (w != target_width) || (h != target_height));

	_rb_gl_check_error("Error setting viewport");
}

static const GLint        _rb_gl4_vertex_format_size[VERTEXFORMAT_NUMTYPES] = { 1,        2,        3,        4,        4,                4,                1,        2,        4,        1,        2,        4        };
static const GLenum       _rb_gl4_vertex_format_type[VERTEXFORMAT_NUMTYPES] = { GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_UNSIGNED_BYTE, GL_BYTE,          GL_SHORT, GL_SHORT, GL_SHORT, GL_INT,   GL_INT,   GL_INT   };
static const GLboolean    _rb_gl4_vertex_format_norm[VERTEXFORMAT_NUMTYPES] = { GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE,          GL_TRUE,          GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE };

static const GLenum       _rb_gl4_primitive_type[RENDERPRIMITIVE_NUMTYPES] = { GL_TRIANGLES };
static const unsigned int _rb_gl4_primitive_mult[RENDERPRIMITIVE_NUMTYPES] = { 3 };
static const unsigned int _rb_gl4_primitive_add[RENDERPRIMITIVE_NUMTYPES]  = { 0 };

//                                                 BLEND_ZERO, BLEND_ONE, BLEND_SRCCOLOR, BLEND_INVSRCCOLOR,      BLEND_DESTCOLOR, BLEND_INVDESTCOLOR,     BLEND_SRCALPHA, BLEND_INVSRCALPHA,      BLEND_DESTALPHA, BLEND_INVDESTALPHA,     BLEND_FACTOR,      BLEND_INVFACTOR,             BLEND_SRCALPHASAT
static const GLenum       _rb_gl4_blend_func[] = { GL_ZERO,    GL_ONE,    GL_SRC_COLOR,   GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,    GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA,   GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,    GL_ONE_MINUS_DST_ALPHA, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_SRC_ALPHA_SATURATE };

static void
_rb_gl4_render(render_backend_gl4_t* backend, render_context_t* context,
               render_command_t* command) {
	render_vertexbuffer_t* vertexbuffer = GET_BUFFER(command->data.render.vertexbuffer);
	render_indexbuffer_t* indexbuffer  = GET_BUFFER(command->data.render.indexbuffer);
	render_parameterbuffer_t* parameterbuffer = GET_BUFFER(command->data.render.parameterbuffer);
	render_program_t* program = command->data.render.program;
	FOUNDATION_UNUSED(context);

	if (!vertexbuffer || !indexbuffer || !parameterbuffer || !program) { //Outdated references
		FOUNDATION_ASSERT_FAIL("Render command using invalid resources");
		return;
	}

	if (vertexbuffer->flags & RENDERBUFFER_DIRTY)
		_rb_gl4_upload_buffer((render_backend_t*)backend, (render_buffer_t*)vertexbuffer);
	if (indexbuffer->flags & RENDERBUFFER_DIRTY)
		_rb_gl4_upload_buffer((render_backend_t*)backend, (render_buffer_t*)indexbuffer);
	_rb_gl_check_error("Error render primitives (upload buffers)");

#if 0
	Implement using core gl4
	//Bind vertex attributes
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)vertexbuffer->backend_data[0]);
	_rb_gl_check_error("Error render primitives (bind vertex buffer)");

	const render_vertex_decl_t* decl = &vertexbuffer->decl;
	for (unsigned int attrib = 0; attrib < VERTEXATTRIBUTE_NUMATTRIBUTES; ++attrib) {
		const uint8_t format = decl->attribute[attrib].format;
		if (format < VERTEXFORMAT_NUMTYPES) {
			glVertexAttribPointer(attrib, _rb_gl4_vertex_format_size[format],
			                      _rb_gl4_vertex_format_type[format], _rb_gl4_vertex_format_norm[format],
			                      (GLsizei)vertexbuffer->size,
			                      (const void*)(uintptr_t)decl->attribute[attrib].offset);
			_rb_gl_check_error("Error render primitives (bind attribute)");
			glEnableVertexAttribArray(attrib);
			_rb_gl_check_error("Error render primitives (enable attribute)");
		}
		else {
			glDisableVertexAttribArray(attrib);
		}
	}
	_rb_gl_check_error("Error render primitives (bind attributes)");

	//Index buffer
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)indexbuffer->backend_data[0]);
	_rb_gl_check_error("Error render primitives (bind index buffer)");

	//Bind programs/shaders
	glUseProgram((GLuint)program->backend_data[0]);
	_rb_gl_check_error("Error render primitives (bind program)");

	// Bind the parameter blocks
	render_parameter_decl_t* paramdecl = &parameterbuffer->decl;
	render_parameter_t* param = paramdecl->parameters;
	for (unsigned int ip = 0; ip < paramdecl->num_parameters; ++ip, ++param) {
		/*if (param->type == RENDERPARAMETER_TEXTURE) {
			//TODO: Dynamic use of texture units, reusing unit that already have correct texture bound, and least-recently-used evicting old bindings to free a new unit
			glActiveTexture(GL_TEXTURE0 + param_info->unit);
			glEnable(GL_TEXTURE_2D);

			object_t object = *(object_t*)pointer_offset(block, param_info->offset);
			render_texture_gl2_t* texture = object ? pool_lookup(_global_pool_texture, object) : 0;
			NEO_ASSERT_MSGFORMAT(!object ||
			                     texture, "Parameter block using old/invalid texture 0x%llx", object);

			glBindTexture(GL_TEXTURE_2D, texture ? texture->object : 0);

			for (unsigned int iu = 0; iu < program->num_uniforms; ++iu) {
				if (program->uniforms[iu].name == *param_name) {
					glUniform1i(program->uniforms[iu].location, param_info->unit);
					break;
				}
			}
		}
		else*/ {
			void* data = pointer_offset(parameterbuffer->store, param->offset);
			if (param->type == RENDERPARAMETER_FLOAT4)
				glUniform4fv(param->location, param->dim, data);
			else if (param->type == RENDERPARAMETER_INT4)
				glUniform4iv(param->location, param->dim, data);
			else if (param->type == RENDERPARAMETER_MATRIX)
				glUniformMatrix4fv(param->location, param->dim, GL_TRUE, data);
		}
	}
	_rb_gl_check_error("Error render primitives (bind uniforms)");

	//TODO: Proper states
	/*ID3D10Device_RSSetState( device, backend_dx10->rasterizer_state[0].state );

	FLOAT blend_factors[] = { 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f };
	ID3D10Device_OMSetBlendState( device, backend_dx10->blend_state[ ( command->data.render.blend_state >> 48ULL ) & 0xFFFFULL ].state, blend_factors, 0xFFFFFFFF );
	ID3D10Device_OMSetDepthStencilState( device, backend_dx10->depthstencil_state[0].state, 0xFFFFFFFF );*/

	if (command->data.render.blend_state) {
		unsigned int source_color = (unsigned int)(command->data.render.blend_state & 0xFULL);
		unsigned int dest_color = (unsigned int)((command->data.render.blend_state >> 4ULL) & 0xFULL);

		glBlendFunc(_rb_gl4_blend_func[source_color], _rb_gl4_blend_func[dest_color]);
	}
	else {
		glBlendFunc(GL_ONE, GL_ZERO);
	}

	unsigned int primitive = command->type - RENDERCOMMAND_RENDER_TRIANGLELIST;
	unsigned int num = command->count;
	unsigned int pnum = _rb_gl4_primitive_mult[primitive] * num + _rb_gl4_primitive_add[primitive];

	glDrawElements(_rb_gl4_primitive_type[primitive], pnum,
	               (indexbuffer->size == 2) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, 0);

	_rb_gl_check_error("Error render primitives");
#endif
}

static void
_rb_gl4_dispatch(render_backend_t* backend, render_context_t** contexts, size_t num_contexts) {
	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;

	for (size_t context_index = 0, context_size = num_contexts; context_index < context_size;
	        ++context_index) {
		render_context_t* context = contexts[context_index];
		render_command_t* command = context->commands;
		const radixsort_index_t* order = context->order;

		for (int cmd_index = 0, cmd_size = atomic_load32(&context->reserved); cmd_index < cmd_size;
		        ++cmd_index, ++order) {
			command = context->commands + *order;
			switch (command->type) {
			case RENDERCOMMAND_CLEAR:
				_rb_gl4_clear(backend_gl4, context, command);
				break;

			case RENDERCOMMAND_VIEWPORT:
				_rb_gl4_viewport(backend_gl4, context, command);
				break;

			case RENDERCOMMAND_RENDER_TRIANGLELIST:
				_rb_gl4_render(backend_gl4, context, command);
				break;
			}
		}
	}
}

static void
_rb_gl4_flip(render_backend_t* backend) {
	render_backend_gl4_t* backend_gl4 = (render_backend_gl4_t*)backend;

#if FOUNDATION_PLATFORM_WINDOWS

	if (backend_gl4->hdc) {
		if (!SwapBuffers(backend_gl4->hdc)) {
			string_const_t errmsg = system_error_message(0);
			log_warnf(HASH_RENDER, WARNING_SYSTEM_CALL_FAIL, STRING_CONST("SwapBuffers failed: %.*s"),
			          STRING_FORMAT(errmsg));
		}
	}

#elif FOUNDATION_PLATFORM_MACOSX

	/*if( _fullscreen && _context )
		CGLFlushDrawable( _context );
	else
	window::objc::flushDrawable( (void*)_context );*/

#elif FOUNDATION_PLATFORM_LINUX

	if (backend_gl4->drawable->display)
		glXSwapBuffers(backend_gl4->drawable->display, backend_gl4->drawable->drawable);

#else
#  error Not implemented
#endif

	++backend->framecount;
}

static render_backend_vtable_t _render_backend_vtable_gl4 = {
	.construct = _rb_gl4_construct,
	.destruct  = _rb_gl4_destruct,
	.enumerate_adapters = _rb_gl4_enumerate_adapters,
	.enumerate_modes = _rb_gl4_enumerate_modes,
	.set_drawable = _rb_gl4_set_drawable,
	.enable_thread = _rb_gl4_enable_thread,
	.disable_thread = _rb_gl4_disable_thread,
	.allocate_buffer = _rb_gl4_allocate_buffer,
	.upload_buffer = _rb_gl4_upload_buffer,
	.upload_shader = _rb_gl4_upload_shader,
	.upload_program = _rb_gl4_upload_program,
	.link_buffer = _rb_gl4_link_buffer,
	.deallocate_buffer = _rb_gl4_deallocate_buffer,
	.deallocate_shader = _rb_gl4_deallocate_shader,
	.deallocate_program = _rb_gl4_deallocate_program,
	/*
	.allocate_texture = _rb_gl4_allocate_texture,
	.upload_texture = _rb_gl4_upload_texture,
	.deallocate_texture = _rb_gl4_deallocate_texture,
	*/
	.dispatch = _rb_gl4_dispatch,
	.flip = _rb_gl4_flip
};

render_backend_t*
render_backend_gl4_allocate() {
	if (!_rb_gl_check_context(4, 0))
		return 0;

#if NEO_ENABLE_NVGLEXPERT
	static bool nvInitialized = false;
	if (!nvInitialized) {
		nvInitialized = true;
		NvAPI_Initialize();
	}
#endif

	render_backend_gl4_t* backend = memory_allocate(HASH_RENDER, sizeof(render_backend_gl4_t), 0,
	                                                MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
	backend->api = RENDERAPI_OPENGL4;
	backend->api_group = RENDERAPIGROUP_OPENGL;
	backend->vtable = _render_backend_vtable_gl4;
	return (render_backend_t*)backend;
}

#else

render_backend_t*
render_gl4_allocate() {
	return 0;
}

#endif
