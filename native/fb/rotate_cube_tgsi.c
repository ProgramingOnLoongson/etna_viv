/*
 * Copyright (c) 2012-2013 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* Rotating, animated cube. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

#include <errno.h>

#include "etna_pipe.h"

#include "write_bmp.h"
#include "viv.h"
#include "etna.h"
#include "etna_state.h"
#include "etna_rs.h"
#include "etna_fb.h"
#include "etna_bswap.h"
#include "etna_tex.h"
#include "state_tracker/graw.h"

#include "esTransform.h"

/*********************************************************************/
#define VERTEX_BUFFER_SIZE 0x60000

static const char cube_vert[] = 
"VERT\n"
"DCL IN[0]\n"
"DCL IN[1]\n"
"DCL IN[2]\n"
"DCL OUT[0], POSITION\n"
"DCL OUT[1], GENERIC[0]\n"
"DCL CONST[0..10]\n"
"DCL TEMP[0..4], LOCAL\n"
"IMM[0] FLT32 {    2.0000,    20.0000,     1.0000,     0.0000}\n"
"  0: MUL TEMP[0], CONST[3], IN[0].xxxx\n"
"  1: MAD TEMP[0], CONST[4], IN[0].yyyy, TEMP[0]\n"
"  2: MAD TEMP[0], CONST[5], IN[0].zzzz, TEMP[0]\n"
"  3: MAD TEMP[0], CONST[6], IN[0].wwww, TEMP[0]\n"
"  4: MUL TEMP[1], CONST[7], IN[0].xxxx\n"
"  5: MAD TEMP[1], CONST[8], IN[0].yyyy, TEMP[1]\n"
"  6: MAD TEMP[1], CONST[9], IN[0].zzzz, TEMP[1]\n"
"  7: MAD TEMP[1], CONST[10], IN[0].wwww, TEMP[1]\n"
"  8: RCP TEMP[2].x, TEMP[1].wwww\n"
"  9: MUL TEMP[1].xyz, TEMP[1].xyzz, TEMP[2].xxxx\n"
" 10: ADD TEMP[1].xyz, IMM[0].xxyy, -TEMP[1].xyzz\n"
" 11: MOV TEMP[2].w, IMM[0].zzzz\n"
" 12: MUL TEMP[3].xyz, CONST[0].xyzz, IN[1].xxxx\n"
" 13: MAD TEMP[3].xyz, CONST[1].xyzz, IN[1].yyyy, TEMP[3].xyzz\n"
" 14: MAD TEMP[3].xyz, CONST[2].xyzz, IN[1].zzzz, TEMP[3].xyzz\n"
" 15: DP3 TEMP[4].x, TEMP[1].xyzz, TEMP[1].xyzz\n"
" 16: RSQ TEMP[4].x, TEMP[4].xxxx\n"
" 17: MUL TEMP[1].xyz, TEMP[1].xyzz, TEMP[4].xxxx\n"
" 18: DP3 TEMP[1].x, TEMP[3].xyzz, TEMP[1].xyzz\n"
" 19: MAX TEMP[1].x, IMM[0].wwww, TEMP[1].xxxx\n"
" 20: MUL TEMP[2].xyz, TEMP[1].xxxx, IN[2].xyzz\n"
" 21: MOV OUT[1], TEMP[2]\n"
" 22: MOV OUT[0], TEMP[0]\n"
" 23: END\n";

static const char cube_frag[] = 
"FRAG\n"
"PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n"
"DCL IN[0], GENERIC[0], PERSPECTIVE\n"
"DCL OUT[0], COLOR\n"
"  0: MOV OUT[0], IN[0]\n"
"  1: END\n";

float vVertices[] = {
  // front
  -1.0f, -1.0f, +1.0f, // point blue
  +1.0f, -1.0f, +1.0f, // point magenta
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  // back
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, +1.0f, -1.0f, // point yellow
  -1.0f, +1.0f, -1.0f, // point green
  // right
  +1.0f, -1.0f, +1.0f, // point magenta
  +1.0f, -1.0f, -1.0f, // point red
  +1.0f, +1.0f, +1.0f, // point white
  +1.0f, +1.0f, -1.0f, // point yellow
  // left
  -1.0f, -1.0f, -1.0f, // point black
  -1.0f, -1.0f, +1.0f, // point blue
  -1.0f, +1.0f, -1.0f, // point green
  -1.0f, +1.0f, +1.0f, // point cyan
  // top
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  -1.0f, +1.0f, -1.0f, // point green
  +1.0f, +1.0f, -1.0f, // point yellow
  // bottom
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, +1.0f, // point blue
  +1.0f, -1.0f, +1.0f  // point magenta
};

float vColors[] = {
  // front
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f, // magenta
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  // back
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  0.0f, // black
  1.0f,  1.0f,  0.0f, // yellow
  0.0f,  1.0f,  0.0f, // green
  // right
  1.0f,  0.0f,  1.0f, // magenta
  1.0f,  0.0f,  0.0f, // red
  1.0f,  1.0f,  1.0f, // white
  1.0f,  1.0f,  0.0f, // yellow
  // left
  0.0f,  0.0f,  0.0f, // black
  0.0f,  0.0f,  1.0f, // blue
  0.0f,  1.0f,  0.0f, // green
  0.0f,  1.0f,  1.0f, // cyan
  // top
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  0.0f,  1.0f,  0.0f, // green
  1.0f,  1.0f,  0.0f, // yellow
  // bottom
  0.0f,  0.0f,  0.0f, // black
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f  // magenta
};

float vNormals[] = {
  // front
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  // back
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  // top
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  // bottom
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f  // down
};
#define COMPONENTS_PER_VERTEX (3)
#define NUM_VERTICES (6*4)

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    
    fb_info fb;
    rv = fb_open(0, &fb);
    if(rv!=0)
    {
        exit(1);
    }
    width = fb.fb_var.xres;
    height = fb.fb_var.yres;

    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    etna_ctx *ctx = 0;
    struct pipe_context *pipe = 0;
    etna_bswap_buffers *buffers = 0;
    if(etna_create(&ctx) != ETNA_OK ||
        etna_bswap_create(ctx, &buffers, (etna_set_buffer_cb_t)&fb_set_buffer, (etna_copy_buffer_cb_t)&etna_fb_copy_buffer, &fb) != ETNA_OK ||
        (pipe = etna_new_pipe_context(ctx)) == NULL)
    {
        printf("Unable to create etna context\n");
        exit(1);
    }

    /* resources */
    struct pipe_resource *rt_resource = etna_pipe_create_2d(pipe, ETNA_IS_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = etna_pipe_create_2d(pipe, ETNA_IS_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);

    /* bind render target to framebuffer */
    etna_fb_bind_resource(&fb, rt_resource);

    /* geometry */
    struct pipe_resource *vtx_resource = etna_pipe_create_buffer(pipe, ETNA_IS_VERTEX, VERTEX_BUFFER_SIZE);

    float *vtx_logical = etna_pipe_get_resource_ptr(pipe, vtx_resource, 0, 0);
    assert(vtx_logical);
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int src_idx = vert * COMPONENTS_PER_VERTEX;
        int dest_idx = vert * COMPONENTS_PER_VERTEX * 3;
        for(int comp=0; comp<COMPONENTS_PER_VERTEX; ++comp)
        {
            ((float*)vtx_logical)[dest_idx+comp+0] = vVertices[src_idx + comp]; /* 0 */
            ((float*)vtx_logical)[dest_idx+comp+3] = vNormals[src_idx + comp]; /* 1 */
            ((float*)vtx_logical)[dest_idx+comp+6] = vColors[src_idx + comp]; /* 2 */
        }
    }

    struct pipe_vertex_buffer vertex_buffer_desc = {
            .stride = (3 + 3 + 3)*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            };
    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* normals */
            .src_offset = 0xc,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* texture coord */
            .src_offset = 0x18,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT
        }
    };
    void *vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);

    /* compile other gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 0,
                    .rgb_func = PIPE_BLEND_ADD,
                    .rgb_src_factor = PIPE_BLENDFACTOR_ONE,
                    .rgb_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .alpha_func = PIPE_BLEND_ADD,
                    .alpha_src_factor = PIPE_BLENDFACTOR_ONE,
                    .alpha_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .colormask = 0xf
                }
            });

    void *rasterizer = pipe->create_rasterizer_state(pipe, &(struct pipe_rasterizer_state){
                .flatshade = 0,
                .light_twoside = 1,
                .clamp_vertex_color = 1,
                .clamp_fragment_color = 1,
                .front_ccw = 1,
                .cull_face = PIPE_FACE_BACK,      /**< PIPE_FACE_x */
                .fill_front = PIPE_POLYGON_MODE_FILL,     /**< PIPE_POLYGON_MODE_x */
                .fill_back = PIPE_POLYGON_MODE_FILL,      /**< PIPE_POLYGON_MODE_x */
                .offset_point = 0,
                .offset_line = 0,
                .offset_tri = 0,
                .scissor = 0,
                .poly_smooth = 1,
                .poly_stipple_enable = 0,
                .point_smooth = 0,
                .sprite_coord_mode = 0,     /**< PIPE_SPRITE_COORD_ */
                .point_quad_rasterization = 0, /** points rasterized as quads or points */
                .point_size_per_vertex = 0, /**< size computed in vertex shader */
                .multisample = 0,
                .line_smooth = 0,
                .line_stipple_enable = 0,
                .line_last_pixel = 0,
                .flatshade_first = 0,
                .gl_rasterization_rules = 1,
                .rasterizer_discard = 0,
                .depth_clip = 0,
                .clip_plane_enable = 0,
                .line_stipple_factor = 0,
                .line_stipple_pattern = 0,
                .sprite_coord_enable = 0,
                .line_width = 1.0f,
                .point_size = 1.0f,
                .offset_units = 0.0f,
                .offset_scale = 0.0f,
                .offset_clamp = 0.0f
            });

    void *dsa = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = {
                .enabled = 0
            },
            .stencil[1] = {
                .enabled = 0
            },
            .alpha = {
                .enabled = 0
            }
            });

    struct pipe_surface *cbuf = pipe->create_surface(pipe, rt_resource, &(struct pipe_surface){
        .texture = rt_resource,
        .format = rt_resource->format,
        .u.tex.level = 0
        });
    struct pipe_surface *zsbuf = pipe->create_surface(pipe, z_resource, &(struct pipe_surface){
        .texture = z_resource,
        .format = z_resource->format,
        .u.tex.level = 0
        });
    
    /* bind */
    pipe->bind_blend_state(pipe, blend);
    pipe->bind_rasterizer_state(pipe, rasterizer);
    pipe->bind_depth_stencil_alpha_state(pipe, dsa);
    pipe->bind_vertex_elements_state(pipe, vertex_elements);

    pipe->set_blend_color(pipe, &(struct pipe_blend_color){
            .color = {0.0f,0.0f,0.0f,1.0f}
            });
    pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
            .ref_value[0] = 0xff,
            .ref_value[1] = 0xff
            });
    pipe->set_sample_mask(pipe, 0xf);
    pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){
            .width = width,
            .height = height,
            .nr_cbufs = 1,
            .cbufs[0] = cbuf,
            .zsbuf = zsbuf
            });
    pipe->set_scissor_state(pipe, &(struct pipe_scissor_state){
            .minx = 0,
            .miny = 0,
            .maxx = 65535,
            .maxy = 65535
            });
    pipe->set_viewport_state(pipe, &(struct pipe_viewport_state){
            .scale = {width/2.0f, height/2.0f, 0.5f, 1.0f},
            .translate = {width/2.0f, height/2.0f, 0.5f, 1.0f}
            });
    pipe->set_vertex_buffers(pipe, 0, 1, &vertex_buffer_desc);
    pipe->set_index_buffer(pipe, NULL);
   
    void *vtx_shader = graw_parse_vertex_shader(pipe, cube_vert);
    void *frag_shader = graw_parse_fragment_shader(pipe, cube_frag);
    pipe->bind_vs_state(pipe, vtx_shader);
    pipe->bind_fs_state(pipe, frag_shader);

    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /*   Compute transform matrices in the same way as cube egl demo */ 
        ESMatrix modelview, projection, modelviewprojection;
        ESMatrix inverse, normal; 
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);
        GLfloat aspect = (GLfloat)(height) / (GLfloat)(width);
        esMatrixLoadIdentity(&projection);
        esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);
        esMatrixLoadIdentity(&modelviewprojection);
        esMatrixMultiply(&modelviewprojection, &modelview, &projection);
        esMatrixInverse3x3(&inverse, &modelview);
        esMatrixTranspose(&normal, &inverse);
       
        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 1.0, 0xff);
        
        pipe->set_etna_uniforms(pipe, NULL, PIPE_SHADER_VERTEX, 0*4, 3, (uint32_t*)&normal.m[0][0]); /* CONST[0] */
        pipe->set_etna_uniforms(pipe, NULL, PIPE_SHADER_VERTEX, 1*4, 3, (uint32_t*)&normal.m[1][0]); /* CONST[1] */
        pipe->set_etna_uniforms(pipe, NULL, PIPE_SHADER_VERTEX, 2*4, 3, (uint32_t*)&normal.m[2][0]); /* CONST[2] */
        pipe->set_etna_uniforms(pipe, NULL, PIPE_SHADER_VERTEX, 3*4, 16, (uint32_t*)&modelviewprojection.m[0][0]); /* CONST[3..6] */
        pipe->set_etna_uniforms(pipe, NULL, PIPE_SHADER_VERTEX, 7*4, 16, (uint32_t*)&modelview.m[0][0]); /* CONST[7..10] */

        for(int prim=0; prim<6; ++prim)
        {
            pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                    .indexed = 0,
                    .mode = PIPE_PRIM_TRIANGLE_STRIP,
                    .start = prim*4,
                    .count = 4
                    });
        }
#if 0
        etna_dump_cmd_buffer(ctx);
        exit(1);
#endif
        etna_swap_buffers(buffers);
    }
#ifdef DUMP
    bmp_dump32(fb.logical[1-buffers->backbuffer], width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    etna_bswap_free(buffers);
    etna_free(ctx);
    viv_close();
    return 0;
}