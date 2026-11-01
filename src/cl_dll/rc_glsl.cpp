// ReCoF: OpenGL 2.0 shader entry points for the client renderer (rc_glsl.h)

#include "hud.h"
#include "cl_util.h"
#include "rc_glsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcGlsl_t rcgl;

static int s_iRcGlslState;	// 0 not tried, 1 loaded, -1 missing

template <typename FuncType>
static bool RcGlslLoad( FuncType &pfn, const char *name )
{
	pfn = (FuncType)gl.wglGetProcAddress( name );
	return pfn != NULL;
}

bool RcGlslInit( void )
{
	if ( s_iRcGlslState )
		return s_iRcGlslState > 0;

	s_iRcGlslState = -1;

	if ( !gl.IsGLAllowed() || !gl.ARB_VBO_supported )
		return false;

	const char *version = (const char *)gl.glGetString( GL_VERSION );
	if ( !version || atoi( version ) < 2 )
	{
		ConLog( "ReCoF shaders: OpenGL 2.0 is missing (%s), the renderer keeps the fixed function paths\n", version ? version : "no version" );
		return false;
	}

	bool ok = true;
	ok &= RcGlslLoad( rcgl.glCreateShader, "glCreateShader" );
	ok &= RcGlslLoad( rcgl.glShaderSource, "glShaderSource" );
	ok &= RcGlslLoad( rcgl.glCompileShader, "glCompileShader" );
	ok &= RcGlslLoad( rcgl.glGetShaderiv, "glGetShaderiv" );
	ok &= RcGlslLoad( rcgl.glGetShaderInfoLog, "glGetShaderInfoLog" );
	ok &= RcGlslLoad( rcgl.glDeleteShader, "glDeleteShader" );
	ok &= RcGlslLoad( rcgl.glCreateProgram, "glCreateProgram" );
	ok &= RcGlslLoad( rcgl.glAttachShader, "glAttachShader" );
	ok &= RcGlslLoad( rcgl.glBindAttribLocation, "glBindAttribLocation" );
	ok &= RcGlslLoad( rcgl.glLinkProgram, "glLinkProgram" );
	ok &= RcGlslLoad( rcgl.glGetProgramiv, "glGetProgramiv" );
	ok &= RcGlslLoad( rcgl.glGetProgramInfoLog, "glGetProgramInfoLog" );
	ok &= RcGlslLoad( rcgl.glDeleteProgram, "glDeleteProgram" );
	ok &= RcGlslLoad( rcgl.glUseProgram, "glUseProgram" );
	ok &= RcGlslLoad( rcgl.glGetUniformLocation, "glGetUniformLocation" );
	ok &= RcGlslLoad( rcgl.glUniform1i, "glUniform1i" );
	ok &= RcGlslLoad( rcgl.glUniform1f, "glUniform1f" );
	ok &= RcGlslLoad( rcgl.glUniform2f, "glUniform2f" );
	ok &= RcGlslLoad( rcgl.glUniform3f, "glUniform3f" );
	ok &= RcGlslLoad( rcgl.glUniform3fv, "glUniform3fv" );
	ok &= RcGlslLoad( rcgl.glUniform4f, "glUniform4f" );
	ok &= RcGlslLoad( rcgl.glUniform4fv, "glUniform4fv" );
	ok &= RcGlslLoad( rcgl.glVertexAttribPointer, "glVertexAttribPointer" );
	ok &= RcGlslLoad( rcgl.glEnableVertexAttribArray, "glEnableVertexAttribArray" );
	ok &= RcGlslLoad( rcgl.glDisableVertexAttribArray, "glDisableVertexAttribArray" );

	if ( !ok )
	{
		ConLog( "ReCoF shaders: the driver reports OpenGL %s but lacks shader functions\n", version );
		return false;
	}

	rcgl.maxVertexUniforms = 0;
	gl.glGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS, &rcgl.maxVertexUniforms );

	ConLog( "ReCoF shaders: OpenGL %s, %d vertex uniform components\n", version, rcgl.maxVertexUniforms );
	s_iRcGlslState = 1;
	return true;
}

static GLuint RcGlslShader( const char *name, GLenum type, const char *source )
{
	GLuint shader = rcgl.glCreateShader( type );
	if ( !shader )
		return 0;

	rcgl.glShaderSource( shader, 1, &source, NULL );
	rcgl.glCompileShader( shader );

	GLint status = 0;
	rcgl.glGetShaderiv( shader, GL_COMPILE_STATUS, &status );
	if ( !status )
	{
		char log[2048];
		log[0] = 0;
		rcgl.glGetShaderInfoLog( shader, sizeof( log ), NULL, log );
		ConLog( "ReCoF shaders: the %s %s shader does not compile:\n%s\n", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log );
		rcgl.glDeleteShader( shader );
		return 0;
	}

	return shader;
}

GLuint RcGlslProgram( const char *name, const char *vertex, const char *fragment, const int *attribIndex, const char **attribName )
{
	GLuint vs = RcGlslShader( name, GL_VERTEX_SHADER, vertex );
	if ( !vs )
		return 0;

	GLuint fs = 0;
	if ( fragment )
	{
		fs = RcGlslShader( name, GL_FRAGMENT_SHADER, fragment );
		if ( !fs )
		{
			rcgl.glDeleteShader( vs );
			return 0;
		}
	}

	GLuint program = rcgl.glCreateProgram();
	if ( program )
	{
		rcgl.glAttachShader( program, vs );
		if ( fs )
			rcgl.glAttachShader( program, fs );

		for ( int i = 0; attribName && attribName[i]; i++ )
			rcgl.glBindAttribLocation( program, attribIndex[i], attribName[i] );

		rcgl.glLinkProgram( program );

		GLint status = 0;
		rcgl.glGetProgramiv( program, GL_LINK_STATUS, &status );
		if ( !status )
		{
			char log[2048];
			log[0] = 0;
			rcgl.glGetProgramInfoLog( program, sizeof( log ), NULL, log );
			ConLog( "ReCoF shaders: the %s program does not link:\n%s\n", name, log );
			rcgl.glDeleteProgram( program );
			program = 0;
		}
	}

	// the program keeps the compiled shaders
	rcgl.glDeleteShader( vs );
	if ( fs )
		rcgl.glDeleteShader( fs );

	if ( program )
		ConLog( "ReCoF shaders: %s program ready\n", name );

	return program;
}
