/* 
 * GStreamer
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>

#include "gl.h"
#include "gstglutils.h"

#ifndef GL_FRAMEBUFFER_UNDEFINED
#define GL_FRAMEBUFFER_UNDEFINED          0x8219
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#endif
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED        0x8CDD
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS 0x8CD9
#endif

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

static GLuint gen_texture;
static GLuint gen_texture_width;
static GLuint gen_texture_height;
static GstVideoFormat gen_texture_video_format;

static GLuint *del_texture;

static gchar *error_message;

static void
gst_gl_display_gen_texture_window_cb (GstGLDisplay * display)
{
  gst_gl_display_gen_texture_thread (display, &gen_texture,
      gen_texture_video_format, gen_texture_width, gen_texture_height);
}

/* Generate a texture if no one is available in the pool
 * Called in the gl thread */
void
gst_gl_display_gen_texture_thread (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  const GstGLFuncs *gl = display->gl_vtable;

  GST_TRACE ("Generating texture format:%u dimensions:%ux%u", v_format,
      width, height);

  gl->GenTextures (1, pTexture);
  gl->BindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);
  gl->TexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, width, height, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
      GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  GST_LOG ("generated texture id:%d", *pTexture);
}

void
gst_gl_display_del_texture_window_cb (GstGLDisplay * display)
{
  glDeleteTextures (1, del_texture);
}

/* called in the gl thread */
gboolean
gst_gl_display_check_framebuffer_status (GstGLDisplay * display)
{
  GLenum status = 0;
  status = display->gl_vtable->CheckFramebufferStatus (GL_FRAMEBUFFER);

  switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:
      return TRUE;
      break;

    case GL_FRAMEBUFFER_UNSUPPORTED:
      GST_ERROR ("GL_FRAMEBUFFER_UNSUPPORTED");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
      break;

    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
      GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS");
      break;
#if GST_GL_HAVE_OPENGL
    case GL_FRAMEBUFFER_UNDEFINED:
      GST_ERROR ("GL_FRAMEBUFFER_UNDEFINED");
      break;
#endif
    default:
      GST_ERROR ("General FBO error");
  }

  return FALSE;
}

void
gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate)
{
  GstGLWindow *window;

  g_return_if_fail (GST_IS_GL_DISPLAY (display));

  if (!activate)
    gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);

  gst_gl_window_activate (window, activate);

  if (activate)
    gst_gl_display_unlock (display);

  gst_object_unref (window);
}

void
gst_gl_display_gen_texture (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height)
{
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);

  if (gst_gl_window_is_running (window)) {
    gen_texture_width = width;
    gen_texture_height = height;
    gen_texture_video_format = v_format;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (gst_gl_display_gen_texture_window_cb), display);
    *pTexture = gen_texture;
  } else
    *pTexture = 0;

  gst_object_unref (window);

  gst_gl_display_unlock (display);
}

void
gst_gl_display_del_texture (GstGLDisplay * display, GLuint * pTexture)
{
  GstGLWindow *window;

  gst_gl_display_lock (display);

  window = gst_gl_display_get_window_unlocked (display);
  if (gst_gl_window_is_running (window) && *pTexture) {
    del_texture = pTexture;
    gst_gl_window_send_message (window,
        GST_GL_WINDOW_CB (gst_gl_display_del_texture_window_cb), display);
  }

  gst_object_unref (window);

  gst_gl_display_unlock (display);
}

typedef struct _GenFBO
{
  GstGLFramebuffer *frame;
  gint width, height;
  GLuint *fbo, *depth;
} GenFBO;

static void
_gen_fbo (GstGLDisplay * display, GenFBO * data)
{
  gst_gl_framebuffer_generate (data->frame, data->width, data->height,
      data->fbo, data->depth);
}

gboolean
gst_gl_display_gen_fbo (GstGLDisplay * display, gint width, gint height,
    GLuint * fbo, GLuint * depthbuffer)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  GenFBO data = { frame, width, height, fbo, depthbuffer };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _gen_fbo, &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _UseFBO
{
  GstGLFramebuffer *frame;
  gint texture_fbo_width;
  gint texture_fbo_height;
  GLuint fbo;
  GLuint depth_buffer;
  GLuint texture_fbo;
  GLCB cb;
  gint input_tex_width;
  gint input_tex_height;
  GLuint input_tex;
  gdouble proj_param1;
  gdouble proj_param2;
  gdouble proj_param3;
  gdouble proj_param4;
  GstGLDisplayProjection projection;
  gpointer stuff;
} UseFBO;

static void
_use_fbo (GstGLDisplay * display, UseFBO * data)
{
  gst_gl_framebuffer_use (data->frame, data->texture_fbo_width,
      data->texture_fbo_height, data->fbo, data->depth_buffer,
      data->texture_fbo, data->cb, data->input_tex_width,
      data->input_tex_height, data->input_tex, data->proj_param1,
      data->proj_param2, data->proj_param3, data->proj_param4, data->projection,
      data->stuff);
}

/* Called by glfilter */
/* this function really has to be simplified...  do we really need to
   set projection this way? Wouldn't be better a set_projection
   separate call? or just make glut functions available out of
   gst-libs and call it if needed on drawcallback? -- Filippo */
/* GLCB too.. I think that only needed parameters should be
 * GstGLDisplay *display and gpointer data, or just gpointer data */
/* ..everything here has to be simplified! */
gboolean
gst_gl_display_use_fbo (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB cb, gint input_tex_width,
    gint input_tex_height, GLuint input_tex, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer stuff)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  UseFBO data =
      { frame, texture_fbo_width, texture_fbo_height, fbo, depth_buffer,
    texture_fbo, cb, input_tex_width, input_tex_height, input_tex,
    proj_param1, proj_param2, proj_param3, proj_param4, projection, stuff
  };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _use_fbo, &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _UseFBO2
{
  GstGLFramebuffer *frame;
  gint texture_fbo_width;
  gint texture_fbo_height;
  GLuint fbo;
  GLuint depth_buffer;
  GLuint texture_fbo;
  GLCB_V2 cb;
  gpointer stuff;
} UseFBO2;

static void
_use_fbo_v2 (GstGLDisplay * display, UseFBO2 * data)
{
  gst_gl_framebuffer_use_v2 (data->frame, data->texture_fbo_width,
      data->texture_fbo_height, data->fbo, data->depth_buffer,
      data->texture_fbo, data->cb, data->stuff);
}

gboolean
gst_gl_display_use_fbo_v2 (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer stuff)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  UseFBO2 data =
      { frame, texture_fbo_width, texture_fbo_height, fbo, depth_buffer,
    texture_fbo, cb, stuff
  };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _use_fbo_v2,
      &data);

  gst_object_unref (frame);

  return TRUE;
}

typedef struct _DelFBO
{
  GstGLFramebuffer *frame;
  GLuint fbo;
  GLuint depth;
} DelFBO;

/* Called in the gl thread */
static void
_del_fbo (GstGLDisplay * display, DelFBO * data)
{
  gst_gl_framebuffer_delete (data->frame, data->fbo, data->depth);
}

/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay * display, GLuint fbo, GLuint depth_buffer)
{
  GstGLFramebuffer *frame = gst_gl_framebuffer_new (display);

  DelFBO data = { frame, fbo, depth_buffer };

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _del_fbo, &data);

  gst_object_unref (frame);
}

static void
_compile_shader (GstGLDisplay * display, GstGLShader ** shader)
{
  GError *error = NULL;

  gst_gl_shader_compile (*shader, &error);
  if (error) {
    gst_gl_display_set_error (display, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_display_clear_shader (display);
    gst_object_unref (*shader);
    *shader = NULL;
  }
}

/* Called by glfilter */
gboolean
gst_gl_display_gen_shader (GstGLDisplay * display, const gchar * vert_src,
    const gchar * frag_src, GstGLShader ** shader)
{
  g_return_val_if_fail (frag_src != NULL || vert_src != NULL, FALSE);
  g_return_val_if_fail (shader != NULL, FALSE);

  *shader = gst_gl_shader_new (display);

  if (frag_src)
    gst_gl_shader_set_fragment_source (*shader, frag_src);
  if (vert_src)
    gst_gl_shader_set_vertex_source (*shader, vert_src);

  gst_gl_display_thread_add (display, (GstGLDisplayThreadFunc) _compile_shader,
      shader);

  return *shader != NULL;
}

void
gst_gl_display_set_error (GstGLDisplay * display, const char *format, ...)
{
  va_list args;

  if (error_message)
    g_free (error_message);

  va_start (args, format);
  error_message = g_strdup_vprintf (format, args);
  va_end (args);

  GST_WARNING ("%s", error_message);
}

gchar *
gst_gl_display_get_error (void)
{
  return error_message;
}

/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay * display, GstGLShader * shader)
{
  gst_object_unref (shader);
}