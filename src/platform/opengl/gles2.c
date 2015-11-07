/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gles2.h"

#include "gba/video.h"
#include "util/configuration.h"
#include "util/vfs.h"

#define MAX_PASSES 8

static const char* const _vertexShader =
	"attribute vec4 position;\n"
	"varying vec2 texCoord;\n"

	"void main() {\n"
	"	gl_Position = position;\n"
	"	texCoord = (position.st + vec2(1.0, -1.0)) * vec2(0.5, -0.5);\n"
	"}";

static const char* const _nullVertexShader =
	"attribute vec4 position;\n"
	"varying vec2 texCoord;\n"

	"void main() {\n"
	"	gl_Position = position;\n"
	"	texCoord = (position.st + vec2(1.0, 1.0)) * vec2(0.5, 0.5);\n"
	"}";

static const char* const _fragmentShader =
	"varying vec2 texCoord;\n"
	"uniform sampler2D tex;\n"
	"uniform float gamma;\n"
	"uniform vec3 scale;\n"
	"uniform vec3 bias;\n"

	"void main() {\n"
	"	vec4 color = texture2D(tex, texCoord);\n"
	"	color.a = 1.;\n"
	"	color.rgb = scale * pow(color.rgb, vec3(gamma, gamma, gamma)) + bias;\n"
	"	gl_FragColor = color;\n"
	"}";

static const char* const _nullFragmentShader =
	"varying vec2 texCoord;\n"
	"uniform sampler2D tex;\n"

	"void main() {\n"
	"	vec4 color = texture2D(tex, texCoord);\n"
	"	color.a = 1.;\n"
	"	gl_FragColor = color;\n"
	"}";

static const GLfloat _vertices[] = {
	-1.f, -1.f,
	-1.f, 1.f,
	1.f, 1.f,
	1.f, -1.f,
};


static void GBAGLES2ContextInit(struct VideoBackend* v, WHandle handle) {
	UNUSED(handle);
	struct GBAGLES2Context* context = (struct GBAGLES2Context*) v;
	glGenTextures(1, &context->tex);
	glBindTexture(GL_TEXTURE_2D, context->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#ifdef COLOR_16_BIT
#ifdef COLOR_5_6_5
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0);
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, 0);
#endif
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#endif

	glClearColor(0.f, 0.f, 0.f, 1.f);

	struct GBAGLES2Uniform* uniforms = malloc(sizeof(struct GBAGLES2Uniform) * 3);
	uniforms[0].name = "gamma";
	uniforms[0].type = GL_FLOAT;
	uniforms[0].value.f = 1.0f;
	uniforms[1].name = "scale";
	uniforms[1].type = GL_FLOAT_VEC3;
	uniforms[1].value.fvec3[0] = 1.0f;
	uniforms[1].value.fvec3[1] = 1.0f;
	uniforms[1].value.fvec3[2] = 1.0f;
	uniforms[2].name = "bias";
	uniforms[2].type = GL_FLOAT_VEC3;
	uniforms[2].value.fvec3[0] = 0.0f;
	uniforms[2].value.fvec3[1] = 0.0f;
	uniforms[2].value.fvec3[2] = 0.0f;
	GBAGLES2ShaderInit(&context->initialShader, _vertexShader, _fragmentShader, -1, -1, uniforms, 3);
	GBAGLES2ShaderInit(&context->finalShader, 0, 0, 0, 0, 0, 0);
	glDeleteFramebuffers(1, &context->finalShader.fbo);
	context->finalShader.fbo = 0;
}

static void GBAGLES2ContextDeinit(struct VideoBackend* v) {
	struct GBAGLES2Context* context = (struct GBAGLES2Context*) v;
	glDeleteTextures(1, &context->tex);
	GBAGLES2ShaderDeinit(&context->initialShader);
	GBAGLES2ShaderDeinit(&context->finalShader);
	free(context->initialShader.uniforms);
}

static void GBAGLES2ContextResized(struct VideoBackend* v, int w, int h) {
	int drawW = w;
	int drawH = h;
	if (v->lockAspectRatio) {
		if (w * 2 > h * 3) {
			drawW = h * 3 / 2;
		} else if (w * 2 < h * 3) {
			drawH = w * 2 / 3;
		}
	}
	glViewport(0, 0, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glViewport((w - drawW) / 2, (h - drawH) / 2, drawW, drawH);
}

static void GBAGLES2ContextClear(struct VideoBackend* v) {
	UNUSED(v);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void _drawShader(struct GBAGLES2Shader* shader) {
	GLint viewport[4];
	glBindFramebuffer(GL_FRAMEBUFFER, shader->fbo);
	if (shader->blend) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		glDisable(GL_BLEND);
	}

	glGetIntegerv(GL_VIEWPORT, viewport);
	int drawW = shader->width;
	int drawH = shader->height;
	int padW = 0;
	int padH = 0;
	if (!shader->width) {
		drawW = viewport[2];
		padW = viewport[0];
	}
	if (!shader->height) {
		drawH = viewport[3];
		padH = viewport[1];
	}
	glViewport(padW, padH, drawW, drawH);
	if (!shader->width || !shader->height) {
		GLint oldTex;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
		glBindTexture(GL_TEXTURE_2D, shader->tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, drawW, drawH, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		glBindTexture(GL_TEXTURE_2D, oldTex);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, shader->filter ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, shader->filter ? GL_LINEAR : GL_NEAREST);
	glUseProgram(shader->program);
	glUniform1i(shader->texLocation, 0);
	glVertexAttribPointer(shader->positionLocation, 2, GL_FLOAT, GL_FALSE, 0, _vertices);
	glEnableVertexAttribArray(shader->positionLocation);
	size_t u;
	for (u = 0; u < shader->nUniforms; ++u) {
		struct GBAGLES2Uniform* uniform = &shader->uniforms[u];
		switch (uniform->type) {
		case GL_FLOAT:
			glUniform1f(uniform->location, uniform->value.f);
			break;
		case GL_INT:
			glUniform1i(uniform->location, uniform->value.i);
			break;
		case GL_UNSIGNED_INT:
			glUniform1ui(uniform->location, uniform->value.ui);
			break;
		case GL_BOOL:
			glUniform1i(uniform->location, uniform->value.b);
			break;
		case GL_FLOAT_VEC2:
			glUniform2fv(uniform->location, 1, uniform->value.fvec2);
			break;
		case GL_FLOAT_VEC3:
			glUniform3fv(uniform->location, 1, uniform->value.fvec3);
			break;
		case GL_FLOAT_VEC4:
			glUniform4fv(uniform->location, 1, uniform->value.fvec4);
			break;
		case GL_INT_VEC2:
			glUniform2iv(uniform->location, 1, uniform->value.ivec2);
			break;
		case GL_INT_VEC3:
			glUniform3iv(uniform->location, 1, uniform->value.ivec3);
			break;
		case GL_INT_VEC4:
			glUniform4iv(uniform->location, 1, uniform->value.ivec4);
			break;
		case GL_BOOL_VEC2:
			glUniform2i(uniform->location, uniform->value.bvec2[0], uniform->value.bvec2[1]);
			break;
		case GL_BOOL_VEC3:
			glUniform3i(uniform->location, uniform->value.bvec3[0], uniform->value.bvec3[1], uniform->value.bvec3[2]);
			break;
		case GL_BOOL_VEC4:
			glUniform4i(uniform->location, uniform->value.bvec4[0], uniform->value.bvec4[1], uniform->value.bvec4[2], uniform->value.bvec4[3]);
			break;
		case GL_FLOAT_MAT2:
			glUniformMatrix2fv(uniform->location, 1, GL_FALSE, uniform->value.fmat2x2);
			break;
		case GL_FLOAT_MAT3:
			glUniformMatrix3fv(uniform->location, 1, GL_FALSE, uniform->value.fmat3x3);
			break;
		case GL_FLOAT_MAT4:
			glUniformMatrix4fv(uniform->location, 1, GL_FALSE, uniform->value.fmat4x4);
			break;
		}
	}
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, shader->tex);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void GBAGLES2ContextDrawFrame(struct VideoBackend* v) {
	struct GBAGLES2Context* context = (struct GBAGLES2Context*) v;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, context->tex);

	context->finalShader.filter = v->filter;
	_drawShader(&context->initialShader);
	size_t n;
	for (n = 0; n < context->nShaders; ++n) {
		_drawShader(&context->shaders[n]);
	}
	_drawShader(&context->finalShader);
	glUseProgram(0);
}

void GBAGLES2ContextPostFrame(struct VideoBackend* v, const void* frame) {
	struct GBAGLES2Context* context = (struct GBAGLES2Context*) v;
	glBindTexture(GL_TEXTURE_2D, context->tex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
#ifdef COLOR_16_BIT
#ifdef COLOR_5_6_5
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, frame);
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, frame);
#endif
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIDEO_HORIZONTAL_PIXELS, VIDEO_VERTICAL_PIXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
#endif
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void GBAGLES2ContextCreate(struct GBAGLES2Context* context) {
	context->d.init = GBAGLES2ContextInit;
	context->d.deinit = GBAGLES2ContextDeinit;
	context->d.resized = GBAGLES2ContextResized;
	context->d.swap = 0;
	context->d.clear = GBAGLES2ContextClear;
	context->d.postFrame = GBAGLES2ContextPostFrame;
	context->d.drawFrame = GBAGLES2ContextDrawFrame;
	context->d.setMessage = 0;
	context->d.clearMessage = 0;
	context->shaders = 0;
	context->nShaders = 0;
}

void GBAGLES2ShaderInit(struct GBAGLES2Shader* shader, const char* vs, const char* fs, int width, int height, struct GBAGLES2Uniform* uniforms, size_t nUniforms) {
	shader->width = width >= 0 ? width : VIDEO_HORIZONTAL_PIXELS;
	shader->height = height >= 0 ? height : VIDEO_VERTICAL_PIXELS;
	shader->filter = false;
	shader->blend = false;
	shader->uniforms = uniforms;
	shader->nUniforms = nUniforms;
	glGenFramebuffers(1, &shader->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, shader->fbo);

	glGenTextures(1, &shader->tex);
	glBindTexture(GL_TEXTURE_2D, shader->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (shader->width && shader->height) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, shader->width, shader->height, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
	}

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shader->tex, 0);
	shader->program = glCreateProgram();
	shader->vertexShader = glCreateShader(GL_VERTEX_SHADER);
	shader->fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	if (vs) {
		glShaderSource(shader->vertexShader, 1, (const GLchar**) &vs, 0);
	} else {
		glShaderSource(shader->vertexShader, 1, (const GLchar**) &_nullVertexShader, 0);
	}
	if (fs) {
		glShaderSource(shader->fragmentShader, 1, (const GLchar**) &fs, 0);
	} else {
		glShaderSource(shader->fragmentShader, 1, (const GLchar**) &_nullFragmentShader, 0);
	}
	glAttachShader(shader->program, shader->vertexShader);
	glAttachShader(shader->program, shader->fragmentShader);
	char log[1024];
	glCompileShader(shader->fragmentShader);
	glGetShaderInfoLog(shader->fragmentShader, 1024, 0, log);
	printf("%s\n", log);
	glCompileShader(shader->vertexShader);
	glGetShaderInfoLog(shader->vertexShader, 1024, 0, log);
	printf("%s\n", log);
	glLinkProgram(shader->program);
	glGetProgramInfoLog(shader->program, 1024, 0, log);
	printf("%s\n", log);

	shader->texLocation = glGetUniformLocation(shader->program, "tex");
	shader->positionLocation = glGetAttribLocation(shader->program, "position");
	size_t i;
	for (i = 0; i < shader->nUniforms; ++i) {
		shader->uniforms[i].location = glGetUniformLocation(shader->program, shader->uniforms[i].name);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GBAGLES2ShaderDeinit(struct GBAGLES2Shader* shader) {
	glDeleteTextures(1, &shader->tex);
	glDeleteShader(shader->fragmentShader);
	glDeleteProgram(shader->program);
	glDeleteFramebuffers(1, &shader->fbo);
}

void GBAGLES2ShaderAttach(struct GBAGLES2Context* context, struct GBAGLES2Shader* shaders, size_t nShaders) {
	if (context->shaders) {
		if (context->shaders == shaders && context->nShaders == nShaders) {
			return;
		}
		GBAGLES2ShaderDetach(context);
	}
	context->shaders = shaders;
	context->nShaders = nShaders;
}

void GBAGLES2ShaderDetach(struct GBAGLES2Context* context) {
	if (!context->shaders) {
		return;
	}
	context->shaders = 0;
}

static bool _lookupIntValue(const struct Configuration* config, const char* section, const char* key, int* out) {
	const char* charValue = ConfigurationGetValue(config, section, key);
	if (!charValue) {
		return false;
	}
	char* end;
	unsigned long value = strtol(charValue, &end, 10);
	if (*end) {
		return false;
	}
	*out = value;
	return true;
}

bool GBAGLES2ShaderLoad(struct GBAGLES2Shader** shaders, size_t* nShaders, struct GBAGLES2ShaderMetadata* metadata, struct VDir* dir) {
	struct VFile* manifest = dir->openFile(dir, "manifest.ini", O_RDONLY);
	if (!manifest) {
		return false;
	}
	bool success = false;
	struct Configuration description;
	ConfigurationInit(&description);
	if (ConfigurationReadVFile(&description, manifest)) {
		int inShaders;
		success = _lookupIntValue(&description, "shader", "passes", &inShaders);
		if (inShaders > MAX_PASSES || inShaders < 1) {
			success = false;
		}
		if (success) {
			if (metadata) {
				metadata->name = ConfigurationGetValue(&description, "shader", "name");
				if (metadata->name) {
					metadata->name = strdup(metadata->name);
				}
				metadata->author = ConfigurationGetValue(&description, "shader", "author");
				if (metadata->author) {
					metadata->author = strdup(metadata->author);
				}
				metadata->description = ConfigurationGetValue(&description, "shader", "description");
				if (metadata->description) {
					metadata->description = strdup(metadata->description);
				}
			}
			struct GBAGLES2Shader* shaderBlock = malloc(sizeof(struct GBAGLES2Shader) * inShaders);
			int n;
			for (n = 0; n < inShaders; ++n) {
				char passName[12];
				snprintf(passName, sizeof(passName), "pass.%u", n);
				const char* fs = ConfigurationGetValue(&description, passName, "fragmentShader");
				const char* vs = ConfigurationGetValue(&description, passName, "vertexShader");
				if (fs && (fs[0] == '.' || strstr(fs, PATH_SEP))) {
					success = false;
					break;
				}
				if (vs && (vs[0] == '.' || strstr(vs, PATH_SEP))) {
					success = false;
					break;
				}
				char* fssrc = 0;
				char* vssrc = 0;
				if (fs) {
					struct VFile* fsf = dir->openFile(dir, fs, O_RDONLY);
					if (!fsf) {
						success = false;
						break;
					}
					fssrc = malloc(fsf->size(fsf));
					fsf->read(fsf, fssrc, fsf->size(fsf));
					fsf->close(fsf);
				}
				if (vs) {
					struct VFile* vsf = dir->openFile(dir, vs, O_RDONLY);
					if (!vsf) {
						success = false;
						free(fssrc);
						break;
					}
					vssrc = malloc(vsf->size(vsf));
					vsf->read(vsf, vssrc, vsf->size(vsf));
					vsf->close(vsf);
				}
				int width = 0;
				int height = 0;
				_lookupIntValue(&description, passName, "width", &width);
				_lookupIntValue(&description, passName, "height", &height);
				GBAGLES2ShaderInit(&shaderBlock[n], vssrc, fssrc, width, height, 0, 0);
				int b = 0;
				_lookupIntValue(&description, passName, "blend", &b);
				if (b) {
					shaderBlock[n].blend = b;
				}
				b = 0;
				_lookupIntValue(&description, passName, "filter", &b);
				if (b) {
					shaderBlock[n].filter = b;
				}
				free(fssrc);
				free(vssrc);
			}
			if (success) {
				*nShaders = inShaders;
				*shaders = shaderBlock;
			} else {
				inShaders = n;
				for (n = 0; n < inShaders; ++n) {
					GBAGLES2ShaderDeinit(&shaderBlock[n]);
				}
			}
		}
	}
	ConfigurationDeinit(&description);
	return success;
}

void GBAGLES2ShaderFree(struct GBAGLES2Shader* shaders, size_t nShaders) {
	size_t n;
	for (n = 0; n < nShaders; ++n) {
		GBAGLES2ShaderDeinit(&shaders[n]);
		size_t u;
		for (u = 0; u < shaders[n].nUniforms; ++u) {
			free((void*) shaders[n].uniforms[u].name);
		}
	}
	free(shaders);
}
