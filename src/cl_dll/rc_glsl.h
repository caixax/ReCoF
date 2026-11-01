// ReCoF: OpenGL 2.0 shader entry points for the client renderer
//
// The engine makes an OpenGL 1.x compatibility context, so the shader functions are loaded through
// wglGetProcAddress. A program made here has a vertex shader and, if given, a fragment shader; without one the
// fixed function fragment stage (texture environments, register combiners, alpha test, fog) stays in use.

#ifndef RC_GLSL_H
#define RC_GLSL_H

#include "glmanager.h"

struct RcGlsl_t
{
	PFNGLCREATESHADERPROC				glCreateShader;
	PFNGLSHADERSOURCEPROC				glShaderSource;
	PFNGLCOMPILESHADERPROC				glCompileShader;
	PFNGLGETSHADERIVPROC				glGetShaderiv;
	PFNGLGETSHADERINFOLOGPROC			glGetShaderInfoLog;
	PFNGLDELETESHADERPROC				glDeleteShader;
	PFNGLCREATEPROGRAMPROC				glCreateProgram;
	PFNGLATTACHSHADERPROC				glAttachShader;
	PFNGLBINDATTRIBLOCATIONPROC			glBindAttribLocation;
	PFNGLLINKPROGRAMPROC				glLinkProgram;
	PFNGLGETPROGRAMIVPROC				glGetProgramiv;
	PFNGLGETPROGRAMINFOLOGPROC			glGetProgramInfoLog;
	PFNGLDELETEPROGRAMPROC				glDeleteProgram;
	PFNGLUSEPROGRAMPROC					glUseProgram;
	PFNGLGETUNIFORMLOCATIONPROC			glGetUniformLocation;
	PFNGLUNIFORM1IPROC					glUniform1i;
	PFNGLUNIFORM1FPROC					glUniform1f;
	PFNGLUNIFORM2FPROC					glUniform2f;
	PFNGLUNIFORM3FPROC					glUniform3f;
	PFNGLUNIFORM3FVPROC					glUniform3fv;
	PFNGLUNIFORM4FPROC					glUniform4f;
	PFNGLUNIFORM4FVPROC					glUniform4fv;
	PFNGLVERTEXATTRIBPOINTERPROC		glVertexAttribPointer;
	PFNGLENABLEVERTEXATTRIBARRAYPROC	glEnableVertexAttribArray;
	PFNGLDISABLEVERTEXATTRIBARRAYPROC	glDisableVertexAttribArray;

	int		maxVertexUniforms;	// GL_MAX_VERTEX_UNIFORM_COMPONENTS
};

extern RcGlsl_t rcgl;

// loads the entry points; false when the driver lacks OpenGL 2.0 or vertex buffers
bool	RcGlslInit( void );

// compiles and links a program; attribIndex and attribName list the generic attributes, ending with a NULL
// name. Returns 0 and prints the driver's log when it fails
GLuint	RcGlslProgram( const char *name, const char *vertex, const char *fragment, const int *attribIndex, const char **attribName );

#endif // RC_GLSL_H
