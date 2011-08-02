
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "GrGLInterface.h"

#import <OpenGLES/ES1/gl.h>
#import <OpenGLES/ES1/glext.h>

#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>

void GrGLSetDefaultGLInterface() {
    static GrGLInterface gDefaultInterface;
    static bool gDefaultInterfaceInit;
    if (!gDefaultInterfaceInit) {
        gDefaultInterface.fNPOTRenderTargetSupport = kProbe_GrGLCapability;
        gDefaultInterface.fMinRenderTargetHeight = kProbe_GrGLCapability;
        gDefaultInterface.fMinRenderTargetWidth = kProbe_GrGLCapability;
        
        gDefaultInterface.fActiveTexture = glActiveTexture;
        gDefaultInterface.fAttachShader = glAttachShader;
        gDefaultInterface.fBindAttribLocation = glBindAttribLocation;
        gDefaultInterface.fBindBuffer = glBindBuffer;
        gDefaultInterface.fBindTexture = glBindTexture;
        gDefaultInterface.fBlendColor = glBlendColor;
        gDefaultInterface.fBlendFunc = glBlendFunc;
        gDefaultInterface.fBufferData = (GrGLBufferDataProc)glBufferData;
        gDefaultInterface.fBufferSubData = (GrGLBufferSubDataProc)glBufferSubData;
        gDefaultInterface.fClear = glClear;
        gDefaultInterface.fClearColor = glClearColor;
        gDefaultInterface.fClearStencil = glClearStencil;
        gDefaultInterface.fClientActiveTexture = glClientActiveTexture;
        gDefaultInterface.fColorMask = glColorMask;
        gDefaultInterface.fColorPointer = glColorPointer;
        gDefaultInterface.fColor4ub = glColor4ub;
        gDefaultInterface.fCompileShader = glCompileShader;
        gDefaultInterface.fCompressedTexImage2D = glCompressedTexImage2D;
        gDefaultInterface.fCreateProgram = glCreateProgram;
        gDefaultInterface.fCreateShader = glCreateShader;
        gDefaultInterface.fCullFace = glCullFace;
        gDefaultInterface.fDeleteBuffers = glDeleteBuffers;
        gDefaultInterface.fDeleteProgram = glDeleteProgram;
        gDefaultInterface.fDeleteShader = glDeleteShader;
        gDefaultInterface.fDeleteTextures = glDeleteTextures;
        gDefaultInterface.fDepthMask = glDepthMask;
        gDefaultInterface.fDisable = glDisable;
        gDefaultInterface.fDisableClientState = glDisableClientState;
        gDefaultInterface.fDisableVertexAttribArray =
        glDisableVertexAttribArray;
        gDefaultInterface.fDrawArrays = glDrawArrays;
        gDefaultInterface.fDrawElements = glDrawElements;
        gDefaultInterface.fEnable = glEnable;
        gDefaultInterface.fEnableClientState = glEnableClientState;
        gDefaultInterface.fEnableVertexAttribArray = glEnableVertexAttribArray;
        gDefaultInterface.fFrontFace = glFrontFace;
        gDefaultInterface.fGenBuffers = glGenBuffers;
        gDefaultInterface.fGetBufferParameteriv = glGetBufferParameteriv;
        gDefaultInterface.fGetError = glGetError;
        gDefaultInterface.fGetIntegerv = glGetIntegerv;
        gDefaultInterface.fGetProgramInfoLog = glGetProgramInfoLog;
        gDefaultInterface.fGetProgramiv = glGetProgramiv;
        gDefaultInterface.fGetShaderInfoLog = glGetShaderInfoLog;
        gDefaultInterface.fGetShaderiv = glGetShaderiv;
        gDefaultInterface.fGetString = glGetString;
        gDefaultInterface.fGenTextures = glGenTextures;
        gDefaultInterface.fGetUniformLocation = glGetUniformLocation;
        gDefaultInterface.fLineWidth = glLineWidth;
        gDefaultInterface.fLinkProgram = glLinkProgram;
        gDefaultInterface.fLoadMatrixf = glLoadMatrixf;
        gDefaultInterface.fMatrixMode = glMatrixMode;
        gDefaultInterface.fPointSize = glPointSize;
        gDefaultInterface.fPixelStorei = glPixelStorei;
        gDefaultInterface.fReadPixels = glReadPixels;
        gDefaultInterface.fScissor = glScissor;
        gDefaultInterface.fShadeModel = glShadeModel;
        gDefaultInterface.fShaderSource = glShaderSource;
        gDefaultInterface.fStencilFunc = glStencilFunc;
        gDefaultInterface.fStencilFuncSeparate = glStencilFuncSeparate;
        gDefaultInterface.fStencilMask = glStencilMask;
        gDefaultInterface.fStencilMaskSeparate = glStencilMaskSeparate;
        gDefaultInterface.fStencilOp = glStencilOp;
        gDefaultInterface.fStencilOpSeparate = glStencilOpSeparate;
        gDefaultInterface.fTexCoordPointer = glTexCoordPointer;
        gDefaultInterface.fTexEnvi = glTexEnvi;
        // mac uses GLenum for internalFormat param (non-standard)
        // amounts to int vs. uint.
        gDefaultInterface.fTexImage2D = (GrGLTexImage2DProc)glTexImage2D;
        gDefaultInterface.fTexParameteri = glTexParameteri;
        gDefaultInterface.fTexSubImage2D = glTexSubImage2D;
        gDefaultInterface.fUniform1f = glUniform1f;
        gDefaultInterface.fUniform1i = glUniform1i;
        gDefaultInterface.fUniform1fv = glUniform1fv;
        gDefaultInterface.fUniform1iv = glUniform1iv;
        gDefaultInterface.fUniform2f = glUniform2f;
        gDefaultInterface.fUniform2i = glUniform2i;
        gDefaultInterface.fUniform2fv = glUniform2fv;
        gDefaultInterface.fUniform2iv = glUniform2iv;
        gDefaultInterface.fUniform3f = glUniform3f;
        gDefaultInterface.fUniform3i = glUniform3i;
        gDefaultInterface.fUniform3fv = glUniform3fv;
        gDefaultInterface.fUniform3iv = glUniform3iv;
        gDefaultInterface.fUniform4f = glUniform4f;
        gDefaultInterface.fUniform4i = glUniform4i;
        gDefaultInterface.fUniform4fv = glUniform4fv;
        gDefaultInterface.fUniform4iv = glUniform4iv;
        gDefaultInterface.fUniform4fv = glUniform4fv;
        gDefaultInterface.fUniformMatrix2fv = glUniformMatrix2fv;
        gDefaultInterface.fUniformMatrix3fv = glUniformMatrix3fv;
        gDefaultInterface.fUniformMatrix4fv = glUniformMatrix4fv;
        gDefaultInterface.fUseProgram = glUseProgram;
        gDefaultInterface.fVertexAttrib4fv = glVertexAttrib4fv;
        gDefaultInterface.fVertexAttribPointer = glVertexAttribPointer;
        gDefaultInterface.fVertexPointer = glVertexPointer;
        gDefaultInterface.fViewport = glViewport;
        
        gDefaultInterface.fGenFramebuffers = glGenFramebuffers;
        gDefaultInterface.fGetFramebufferAttachmentParameteriv = glGetFramebufferAttachmentParameteriv;
        gDefaultInterface.fGetRenderbufferParameteriv = glGetRenderbufferParameteriv;
        gDefaultInterface.fBindFramebuffer = glBindFramebuffer;
        gDefaultInterface.fFramebufferTexture2D = glFramebufferTexture2D;
        gDefaultInterface.fCheckFramebufferStatus = glCheckFramebufferStatus;
        gDefaultInterface.fDeleteFramebuffers = glDeleteFramebuffers;
        gDefaultInterface.fRenderbufferStorage = glRenderbufferStorage;
        gDefaultInterface.fGenRenderbuffers = glGenRenderbuffers;
        gDefaultInterface.fDeleteRenderbuffers = glDeleteRenderbuffers;
        gDefaultInterface.fFramebufferRenderbuffer = glFramebufferRenderbuffer;
        gDefaultInterface.fBindRenderbuffer = glBindRenderbuffer;
       
#if GL_OES_mapbuffer
        gDefaultInterface.fMapBuffer = glMapBufferOES;
        gDefaultInterface.fUnmapBuffer = glUnmapBufferOES;
#endif
        
#if GL_APPLE_framebuffer_multisample
        gDefaultInterface.fRenderbufferStorageMultisample = glRenderbufferStorageMultisampleAPPLE;
        gDefaultInterface.fResolveMultisampleFramebuffer = glResolveMultisampleFramebufferAPPLE;
#endif
        gDefaultInterface.fBindFragDataLocationIndexed = NULL;
        
        gDefaultInterface.fBindingsExported = kES2_GrGLBinding;
        
        gDefaultInterfaceInit = true;
    }
    GrGLSetGLInterface(&gDefaultInterface);
}