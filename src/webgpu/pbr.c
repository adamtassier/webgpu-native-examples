#include "pbr.h"

#include <cglm/cglm.h>
#include <math.h>

#include "../core/macro.h"
#include "buffer.h"
#include "shader.h"

texture_t pbr_generate_brdf_lut(wgpu_context_t* wgpu_context)
{
#define BRDF_LUT_DIM 512

  texture_t lut_brdf = {0};

  const WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
  const int32_t dim              = (int32_t)BRDF_LUT_DIM;

  /* Texture dimensions */
  WGPUExtent3D texture_extent = {
    .width              = dim,
    .height             = dim,
    .depthOrArrayLayers = 1,
  };

  /* Create the texture */
  {
    WGPUTextureDescriptor texture_desc = {
      .label         = "LUT BRDF texture",
      .size          = texture_extent,
      .mipLevelCount = 1,
      .sampleCount   = 1,
      .dimension     = WGPUTextureDimension_2D,
      .format        = format,
      .usage
      = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
    };
    lut_brdf.texture
      = wgpuDeviceCreateTexture(wgpu_context->device, &texture_desc);
    ASSERT(lut_brdf.texture != NULL);
  }

  /* Create the texture view */
  {
    WGPUTextureViewDescriptor texture_view_dec = {
      .label           = "LUT BRDF texture view",
      .dimension       = WGPUTextureViewDimension_2D,
      .format          = format,
      .baseMipLevel    = 0,
      .mipLevelCount   = 1,
      .baseArrayLayer  = 0,
      .arrayLayerCount = 1,
    };
    lut_brdf.view = wgpuTextureCreateView(lut_brdf.texture, &texture_view_dec);
    ASSERT(lut_brdf.view != NULL);
  }

  /* Create the texture sampler */
  {
    lut_brdf.sampler = wgpuDeviceCreateSampler(
      wgpu_context->device, &(WGPUSamplerDescriptor){
                              .label         = "LUT BRDF texture sampler",
                              .addressModeU  = WGPUAddressMode_ClampToEdge,
                              .addressModeV  = WGPUAddressMode_ClampToEdge,
                              .addressModeW  = WGPUAddressMode_ClampToEdge,
                              .minFilter     = WGPUFilterMode_Linear,
                              .magFilter     = WGPUFilterMode_Linear,
                              .mipmapFilter  = WGPUMipmapFilterMode_Linear,
                              .lodMinClamp   = 0.0f,
                              .lodMaxClamp   = 1.0f,
                              .maxAnisotropy = 1,
                            });
    ASSERT(lut_brdf.sampler != NULL);
  }

  /* Look-up-table (from BRDF) pipeline */
  WGPURenderPipeline pipeline = NULL;
  {
    // Primitive state
    WGPUPrimitiveState primitive_state = {
      .topology  = WGPUPrimitiveTopology_TriangleList,
      .frontFace = WGPUFrontFace_CCW,
      .cullMode  = WGPUCullMode_None,
    };

    // Color target state
    WGPUBlendState blend_state              = wgpu_create_blend_state(false);
    WGPUColorTargetState color_target_state = (WGPUColorTargetState){
      .format    = format,
      .blend     = &blend_state,
      .writeMask = WGPUColorWriteMask_All,
    };

    // Multisample state
    WGPUMultisampleState multisample_state
      = wgpu_create_multisample_state_descriptor(
        &(create_multisample_state_desc_t){
          .sample_count = 1,
        });

    // Vertex state
    WGPUVertexState vertex_state = wgpu_create_vertex_state(
              wgpu_context, &(wgpu_vertex_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Vertex shader SPIR-V
                .label = "Gen BRDF LUT vertex shader",
                .file  = "shaders/pbr/genbrdflut.vert.spv",
              },
              .buffer_count = 0,
              .buffers      = NULL,
            });

    // Fragment state
    WGPUFragmentState fragment_state = wgpu_create_fragment_state(
              wgpu_context, &(wgpu_fragment_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Fragment shader SPIR-V
                .label = "Gen BRDF LUT fragment shader",
                .file  = "shaders/pbr/genbrdflut.frag.spv",
              },
              .target_count = 1,
              .targets      = &color_target_state,
            });

    // Create rendering pipeline using the specified states
    pipeline = wgpuDeviceCreateRenderPipeline(
      wgpu_context->device, &(WGPURenderPipelineDescriptor){
                              .label        = "Gen BRDF LUT render pipeline",
                              .primitive    = primitive_state,
                              .vertex       = vertex_state,
                              .fragment     = &fragment_state,
                              .depthStencil = NULL,
                              .multisample  = multisample_state,
                            });
    ASSERT(pipeline != NULL);

    // Partial cleanup
    WGPU_RELEASE_RESOURCE(ShaderModule, vertex_state.module);
    WGPU_RELEASE_RESOURCE(ShaderModule, fragment_state.module);
  }

  /* Create the actual renderpass */
  struct {
    WGPURenderPassColorAttachment color_attachment[1];
    WGPURenderPassDescriptor render_pass_descriptor;
  } render_pass = {
    .color_attachment[0]= (WGPURenderPassColorAttachment) {
        .view       = lut_brdf.view,
        .loadOp     = WGPULoadOp_Clear,
        .storeOp    = WGPUStoreOp_Store,
        .clearValue = (WGPUColor) {
          .r = 0.0f,
          .g = 0.0f,
          .b = 0.0f,
          .a = 1.0f,
        },
     },
  };
  render_pass.render_pass_descriptor = (WGPURenderPassDescriptor){
    .label                  = "Gen BRDF LUT render pass descriptor",
    .colorAttachmentCount   = 1,
    .colorAttachments       = render_pass.color_attachment,
    .depthStencilAttachment = NULL,
  };

  /* Render */
  {
    wgpu_context->cmd_enc
      = wgpuDeviceCreateCommandEncoder(wgpu_context->device, NULL);
    wgpu_context->rpass_enc = wgpuCommandEncoderBeginRenderPass(
      wgpu_context->cmd_enc, &render_pass.render_pass_descriptor);
    wgpuRenderPassEncoderSetViewport(wgpu_context->rpass_enc, 0.0f, 0.0f,
                                     (float)dim, (float)dim, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(wgpu_context->rpass_enc, 0u, 0u, dim,
                                        dim);
    wgpuRenderPassEncoderSetPipeline(wgpu_context->rpass_enc, pipeline);
    wgpuRenderPassEncoderDraw(wgpu_context->rpass_enc, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(wgpu_context->rpass_enc);

    WGPU_RELEASE_RESOURCE(RenderPassEncoder, wgpu_context->rpass_enc)

    WGPUCommandBuffer command_buffer
      = wgpuCommandEncoderFinish(wgpu_context->cmd_enc, NULL);
    ASSERT(command_buffer != NULL);
    WGPU_RELEASE_RESOURCE(CommandEncoder, wgpu_context->cmd_enc)

    // Sumbit commmand buffer and cleanup
    wgpuQueueSubmit(wgpu_context->queue, 1, &command_buffer);
    WGPU_RELEASE_RESOURCE(CommandBuffer, command_buffer)
  }

  /* Cleanup */
  WGPU_RELEASE_RESOURCE(RenderPipeline, pipeline);

  return lut_brdf;
}

texture_t pbr_generate_irradiance_cube(wgpu_context_t* wgpu_context,
                                       struct gltf_model_t* skybox,
                                       texture_t* skybox_texture)
{
#define ALIGNMENT 256 /* 256-byte alignment */
#define IRRADIANCE_CUBE_DIM 64
#define IRRADIANCE_CUBE_NUM_MIPS 7 /* ((uint32_t)(floor(log2(dim)))) + 1; */

  texture_t irradiance_cube = {0};

  const WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
  const int32_t dim              = (int32_t)IRRADIANCE_CUBE_DIM;
  const uint32_t num_mips        = (uint32_t)IRRADIANCE_CUBE_NUM_MIPS;
  ASSERT(num_mips == ((uint32_t)(floor(log2(dim)))) + 1);
  const uint32_t array_layer_count = 6u; // Cube map

  /** Pre-filtered cube map **/
  // Texture dimensions
  WGPUExtent3D texture_extent = {
    .width              = dim,
    .height             = dim,
    .depthOrArrayLayers = array_layer_count,
  };

  // Create the texture
  {
    WGPUTextureDescriptor texture_desc = {
      .label         = "Irradiance cube texture",
      .size          = texture_extent,
      .mipLevelCount = num_mips,
      .sampleCount   = 1,
      .dimension     = WGPUTextureDimension_2D,
      .format        = format,
      .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_TextureBinding,
    };
    irradiance_cube.texture
      = wgpuDeviceCreateTexture(wgpu_context->device, &texture_desc);
    ASSERT(irradiance_cube.texture != NULL);
  }

  // Create the texture view
  {
    WGPUTextureViewDescriptor texture_view_dec = {
      .label           = "Irradiance cube texture view",
      .dimension       = WGPUTextureViewDimension_Cube,
      .format          = format,
      .baseMipLevel    = 0,
      .mipLevelCount   = num_mips,
      .baseArrayLayer  = 0,
      .arrayLayerCount = array_layer_count,
    };
    irradiance_cube.view
      = wgpuTextureCreateView(irradiance_cube.texture, &texture_view_dec);
    ASSERT(irradiance_cube.view != NULL);
  }

  // Create the sampler
  {
    irradiance_cube.sampler = wgpuDeviceCreateSampler(
      wgpu_context->device, &(WGPUSamplerDescriptor){
                              .label        = "Irradiance cube texture sampler",
                              .addressModeU = WGPUAddressMode_ClampToEdge,
                              .addressModeV = WGPUAddressMode_ClampToEdge,
                              .addressModeW = WGPUAddressMode_ClampToEdge,
                              .minFilter    = WGPUFilterMode_Linear,
                              .magFilter    = WGPUFilterMode_Linear,
                              .mipmapFilter = WGPUMipmapFilterMode_Linear,
                              .lodMinClamp  = 0.0f,
                              .lodMaxClamp  = (float)num_mips,
                              .maxAnisotropy = 1,
                            });
    ASSERT(irradiance_cube.sampler != NULL);
  }

  // Framebuffer for offscreen rendering
  struct {
    WGPUTexture texture;
    WGPUTextureView texture_views[6 * (uint32_t)IRRADIANCE_CUBE_NUM_MIPS];
  } offscreen;

  /* Offscreen framebuffer */
  {
    /* Color attachment */
    {
      // Create the texture
      WGPUTextureDescriptor texture_desc = {
        .label         = "Irradiance cube offscreen texture",
        .size          = (WGPUExtent3D) {
          .width              = dim,
          .height             = dim,
          .depthOrArrayLayers = array_layer_count,
        },
        .mipLevelCount = num_mips,
        .sampleCount   = 1,
        .dimension     = WGPUTextureDimension_2D,
        .format        = format,
        .usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding
                 | WGPUTextureUsage_RenderAttachment,
      };
      offscreen.texture
        = wgpuDeviceCreateTexture(wgpu_context->device, &texture_desc);
      ASSERT(offscreen.texture != NULL);

      /* Create the texture views */
      uint32_t idx = 0;
      for (uint32_t array_layer = 0; array_layer < array_layer_count;
           ++array_layer) {
        for (uint32_t i = 0; i < num_mips; ++i) {
          idx = (array_layer * num_mips) + i;
          WGPUTextureViewDescriptor texture_view_dec = {
            .label           = "irradiance_cube_offscreen_texture_view",
            .aspect          = WGPUTextureAspect_All,
            .dimension       = WGPUTextureViewDimension_2D,
            .format          = texture_desc.format,
            .baseMipLevel    = i,
            .mipLevelCount   = 1,
            .baseArrayLayer  = array_layer,
            .arrayLayerCount = 1,
          };
          offscreen.texture_views[idx]
            = wgpuTextureCreateView(offscreen.texture, &texture_view_dec);
          ASSERT(offscreen.texture_views[idx] != NULL);
        }
      }
    }
  }

  struct push_block_vs_t {
    mat4 mvp;
    uint8_t padding[192];
  } push_block_vs[(uint32_t)IRRADIANCE_CUBE_NUM_MIPS * 6];

  struct push_block_fs_t {
    float delta_phi;
    float delta_theta;
    uint8_t padding[248];
  } push_block_fs[(uint32_t)IRRADIANCE_CUBE_NUM_MIPS * 6];

  /* Update shader push constant block data */
  {
    mat4 matrices[6] = {
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_X */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_X */
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_Y */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_Y */
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_Z */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_Z */
    };
    /* NEGATIVE_X */
    glm_rotate(matrices[0], glm_rad(90.0f), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(matrices[0], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_X */
    glm_rotate(matrices[1], glm_rad(-90.0f), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(matrices[1], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* POSITIVE_Y */
    glm_rotate(matrices[2], glm_rad(90.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_Y */
    glm_rotate(matrices[3], glm_rad(-90.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* POSITIVE_Z */
    glm_rotate(matrices[4], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_Z */
    glm_rotate(matrices[5], glm_rad(180.0f), (vec3){0.0f, 0.0f, 1.0f});

    mat4 projection = GLM_MAT4_IDENTITY_INIT;
    glm_perspective(PI / 2.0f, 1.0f, 0.1f, 512.0f, projection);
    // Sampling deltas
    const float delta_phi   = (2.0f * PI) / 180.0f;
    const float delta_theta = (0.5f * PI) / 64.0f;
    uint32_t idx            = 0;
    for (uint32_t m = 0; m < num_mips; ++m) {
      for (uint32_t f = 0; f < 6; ++f) {
        idx = (m * 6) + f;
        // Set vertex shader push constant block
        glm_mat4_mul(projection, matrices[f], push_block_vs[idx].mvp);
        // Set fragment shader push constant block
        push_block_fs[idx].delta_phi   = delta_phi;
        push_block_fs[idx].delta_theta = delta_theta;
      }
    }
  }

  static struct {
    // Vertex shader parameter uniform buffer
    struct {
      WGPUBuffer buffer;
      uint64_t buffer_size;
      uint64_t model_size;
    } vs;
    // Fragment parameter uniform buffer
    struct {
      WGPUBuffer buffer;
      uint64_t buffer_size;
      uint64_t model_size;
    } fs;
  } irradiance_cube_ubos = {0};

  // Vertex shader parameter uniform buffer
  {
    irradiance_cube_ubos.vs.model_size = sizeof(mat4);
    irradiance_cube_ubos.vs.buffer_size
      = calc_constant_buffer_byte_size(sizeof(push_block_vs));
    irradiance_cube_ubos.vs.buffer = wgpu_create_buffer_from_data(
      wgpu_context, push_block_vs, irradiance_cube_ubos.vs.buffer_size,
      WGPUBufferUsage_Uniform);
  }

  // Fragment shader parameter uniform buffer
  {
    irradiance_cube_ubos.fs.model_size = sizeof(float) * 2;
    irradiance_cube_ubos.fs.buffer_size
      = calc_constant_buffer_byte_size(sizeof(push_block_fs));
    irradiance_cube_ubos.fs.buffer = wgpu_create_buffer_from_data(
      wgpu_context, push_block_fs, irradiance_cube_ubos.fs.buffer_size,
      WGPUBufferUsage_Uniform);
  }

  // Bind group layout
  WGPUBindGroupLayout bind_group_layout = NULL;
  {
    WGPUBindGroupLayoutEntry bgl_entries[4] = {
      [0] = (WGPUBindGroupLayoutEntry) {
        // Binding 0: Vertex shader uniform UBO
        .binding    = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = (WGPUBufferBindingLayout) {
          .type             = WGPUBufferBindingType_Uniform,
          .hasDynamicOffset = true,
          .minBindingSize   = irradiance_cube_ubos.vs.model_size,
        },
        .sampler = {0},
      },
      [1] = (WGPUBindGroupLayoutEntry) {
        // Binding 1: Fragment shader uniform UBO
        .binding    = 1,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = (WGPUBufferBindingLayout) {
          .type             = WGPUBufferBindingType_Uniform,
          .hasDynamicOffset = true,
          .minBindingSize   = irradiance_cube_ubos.fs.model_size,
        },
        .sampler = {0},
      },
      [2] = (WGPUBindGroupLayoutEntry) {
        // Binding 2: Fragment shader image view
        .binding    = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = (WGPUTextureBindingLayout) {
          .sampleType    = WGPUTextureSampleType_Float,
          .viewDimension = WGPUTextureViewDimension_Cube,
          .multisampled  = false,
        },
        .storageTexture = {0},
      },
      [3] = (WGPUBindGroupLayoutEntry) {
        // Binding 3: Fragment shader image sampler
        .binding    = 3,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = (WGPUSamplerBindingLayout){
          .type = WGPUSamplerBindingType_Filtering,
        },
        .texture = {0},
      },
    };
    bind_group_layout = wgpuDeviceCreateBindGroupLayout(
      wgpu_context->device, &(WGPUBindGroupLayoutDescriptor){
                              .entryCount = (uint32_t)ARRAY_SIZE(bgl_entries),
                              .entries    = bgl_entries,
                            });
    ASSERT(bind_group_layout != NULL);
  }

  // Bind group
  WGPUBindGroup bind_group = NULL;
  {
    WGPUBindGroupEntry bg_entries[4] = {
      [0] = (WGPUBindGroupEntry) {
        // Binding 0: Vertex shader uniform UBO
        .binding = 0,
        .buffer  = irradiance_cube_ubos.vs.buffer,
        .offset  = 0,
        .size    = irradiance_cube_ubos.vs.model_size,
      },
      [1] = (WGPUBindGroupEntry) {
        // Binding 1: Fragment shader uniform UBO
        .binding = 1,
        .buffer  = irradiance_cube_ubos.fs.buffer,
        .offset  = 0,
        .size    = irradiance_cube_ubos.fs.model_size,
      },
      [2] = (WGPUBindGroupEntry) {
        // Binding 2: Fragment shader image view
        .binding     = 2,
        .textureView = skybox_texture->view
      },
      [3] = (WGPUBindGroupEntry) {
        // Binding 3: Fragment shader image sampler
        .binding = 3,
        .sampler = skybox_texture->sampler,
      },
    };
    bind_group = wgpuDeviceCreateBindGroup(
      wgpu_context->device, &(WGPUBindGroupDescriptor){
                              .layout     = bind_group_layout,
                              .entryCount = (uint32_t)ARRAY_SIZE(bg_entries),
                              .entries    = bg_entries,
                            });
    ASSERT(bind_group != NULL);
  }

  // Pipeline layout
  WGPUPipelineLayout pipeline_layout = NULL;
  {
    pipeline_layout = wgpuDeviceCreatePipelineLayout(
      wgpu_context->device, &(WGPUPipelineLayoutDescriptor){
                              .label                = "Pipeline layout",
                              .bindGroupLayoutCount = 1,
                              .bindGroupLayouts     = &bind_group_layout,
                            });
    ASSERT(pipeline_layout != NULL);
  }

  // Irradiance cube map pipeline
  WGPURenderPipeline pipeline = NULL;
  {
    // Primitive state
    WGPUPrimitiveState primitive_state = {
      .topology  = WGPUPrimitiveTopology_TriangleList,
      .frontFace = WGPUFrontFace_CCW,
      .cullMode  = WGPUCullMode_None,
    };

    // Color target state
    WGPUBlendState blend_state              = wgpu_create_blend_state(false);
    WGPUColorTargetState color_target_state = (WGPUColorTargetState){
      .format    = format,
      .blend     = &blend_state,
      .writeMask = WGPUColorWriteMask_All,
    };

    // Vertex buffer layout
    WGPU_GLTF_VERTEX_BUFFER_LAYOUT(
      skybox,
      // Location 0: Position
      WGPU_GLTF_VERTATTR_DESC(0, WGPU_GLTF_VertexComponent_Position));

    // Vertex state
    WGPUVertexState vertex_state = wgpu_create_vertex_state(
              wgpu_context, &(wgpu_vertex_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Vertex shader SPIR-
                .label = "Filtercube vertex shader",
                .file  = "shaders/pbr/filtercube.vert.spv",
              },
             .buffer_count = 1,
             .buffers      = &skybox_vertex_buffer_layout,
            });

    // Fragment state
    WGPUFragmentState fragment_state = wgpu_create_fragment_state(
              wgpu_context, &(wgpu_fragment_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Fragment shader SPIR-V
                .label = "Irradiancecube fragment shader",
                .file  = "shaders/pbr/irradiancecube.frag.spv",
              },
              .target_count = 1,
              .targets      = &color_target_state,
            });

    // Multisample state
    WGPUMultisampleState multisample_state
      = wgpu_create_multisample_state_descriptor(
        &(create_multisample_state_desc_t){
          .sample_count = 1,
        });

    // Create rendering pipeline using the specified states
    pipeline = wgpuDeviceCreateRenderPipeline(
      wgpu_context->device, &(WGPURenderPipelineDescriptor){
                              .label  = "irradiance_cube_map_render_pipeline",
                              .layout = pipeline_layout,
                              .primitive    = primitive_state,
                              .vertex       = vertex_state,
                              .fragment     = &fragment_state,
                              .depthStencil = NULL,
                              .multisample  = multisample_state,
                            });
    ASSERT(pipeline != NULL);

    // Partial cleanup
    WGPU_RELEASE_RESOURCE(ShaderModule, vertex_state.module);
    WGPU_RELEASE_RESOURCE(ShaderModule, fragment_state.module);
  }

  // Create the actual renderpass
  struct {
    WGPURenderPassColorAttachment color_attachment[1];
    WGPURenderPassDescriptor render_pass_descriptor;
  } render_pass = {
    .color_attachment[0]= (WGPURenderPassColorAttachment) {
        .view       = NULL, /* Assigned later */
        .loadOp     = WGPULoadOp_Clear,
        .storeOp    = WGPUStoreOp_Store,
        .clearValue = (WGPUColor) {
          .r = 0.0f,
          .g = 0.0f,
          .b = 0.2f,
          .a = 0.0f,
        },
     },
  };
  render_pass.render_pass_descriptor = (WGPURenderPassDescriptor){
    .colorAttachmentCount   = 1,
    .colorAttachments       = render_pass.color_attachment,
    .depthStencilAttachment = NULL,
  };

  // Render
  {
    wgpu_context->cmd_enc
      = wgpuDeviceCreateCommandEncoder(wgpu_context->device, NULL);

    uint32_t idx         = 0;
    float viewport_width = 0.0f, viewport_height = 0.0f;
    for (uint32_t m = 0; m < num_mips; ++m) {
      viewport_width  = (float)(dim * pow(0.5f, m));
      viewport_height = (float)(dim * pow(0.5f, m));
      for (uint32_t f = 0; f < 6; ++f) {
        render_pass.color_attachment[0].view
          = offscreen.texture_views[(f * num_mips) + m];
        idx = (m * 6) + f;
        // Render scene from cube face's point of view
        wgpu_context->rpass_enc = wgpuCommandEncoderBeginRenderPass(
          wgpu_context->cmd_enc, &render_pass.render_pass_descriptor);
        wgpuRenderPassEncoderSetViewport(wgpu_context->rpass_enc, 0.0f, 0.0f,
                                         viewport_width, viewport_height, 0.0f,
                                         1.0f);
        wgpuRenderPassEncoderSetScissorRect(wgpu_context->rpass_enc, 0u, 0u,
                                            (uint32_t)viewport_width,
                                            (uint32_t)viewport_height);
        wgpuRenderPassEncoderSetPipeline(wgpu_context->rpass_enc, pipeline);
        // Calculate the dynamic offsets
        uint32_t dynamic_offset     = idx * (uint32_t)ALIGNMENT;
        uint32_t dynamic_offsets[2] = {dynamic_offset, dynamic_offset};
        // Bind the bind group for rendering a mesh using the dynamic offset
        wgpuRenderPassEncoderSetBindGroup(wgpu_context->rpass_enc, 0,
                                          bind_group, 2, dynamic_offsets);
        // Draw object
        wgpu_gltf_model_draw(skybox, (wgpu_gltf_model_render_options_t){0});
        // End render pass
        wgpuRenderPassEncoderEnd(wgpu_context->rpass_enc);
        WGPU_RELEASE_RESOURCE(RenderPassEncoder, wgpu_context->rpass_enc)
      }
    }

    // Copy region for transfer from framebuffer to cube face
    for (uint32_t m = 0; m < num_mips; ++m) {
      WGPUExtent3D copy_size = (WGPUExtent3D){
        .width              = (float)(dim * pow(0.5f, m)),
        .height             = (float)(dim * pow(0.5f, m)),
        .depthOrArrayLayers = array_layer_count,
      };
      wgpuCommandEncoderCopyTextureToTexture(
        wgpu_context->cmd_enc,
        // source
        &(WGPUImageCopyTexture){
          .texture  = offscreen.texture,
          .mipLevel = m,
        },
        // destination
        &(WGPUImageCopyTexture){
          .texture  = irradiance_cube.texture,
          .mipLevel = m,
        },
        // copySize
        &copy_size);
    }

    WGPUCommandBuffer command_buffer
      = wgpuCommandEncoderFinish(wgpu_context->cmd_enc, NULL);
    ASSERT(command_buffer != NULL);
    WGPU_RELEASE_RESOURCE(CommandEncoder, wgpu_context->cmd_enc)

    // Sumbit commmand buffer and cleanup
    wgpuQueueSubmit(wgpu_context->queue, 1, &command_buffer);
    WGPU_RELEASE_RESOURCE(CommandBuffer, command_buffer)
  }

  // Cleanup
  WGPU_RELEASE_RESOURCE(Texture, offscreen.texture)
  for (uint32_t i = 0; i < (uint32_t)ARRAY_SIZE(offscreen.texture_views); ++i) {
    WGPU_RELEASE_RESOURCE(TextureView, offscreen.texture_views[i])
  }
  WGPU_RELEASE_RESOURCE(Buffer, irradiance_cube_ubos.vs.buffer)
  WGPU_RELEASE_RESOURCE(Buffer, irradiance_cube_ubos.fs.buffer)
  WGPU_RELEASE_RESOURCE(BindGroup, bind_group)
  WGPU_RELEASE_RESOURCE(BindGroupLayout, bind_group_layout)
  WGPU_RELEASE_RESOURCE(RenderPipeline, pipeline)
  WGPU_RELEASE_RESOURCE(PipelineLayout, pipeline_layout)

  return irradiance_cube;
}

texture_t pbr_generate_prefiltered_cube(wgpu_context_t* wgpu_context,
                                        struct gltf_model_t* skybox,
                                        texture_t* skybox_texture)
{
#define PREFILTERED_CUBE_DIM 512
#define PREFILTERED_CUBE_NUM_MIPS 10 // ((uint32_t)(floor(log2(dim)))) + 1;

  texture_t prefiltered_cube = {0};

  const WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
  const int32_t dim              = (int32_t)PREFILTERED_CUBE_DIM;
  const uint32_t num_mips        = (uint32_t)PREFILTERED_CUBE_NUM_MIPS;
  ASSERT(num_mips == ((uint32_t)(floor(log2(dim)))) + 1)
  const uint32_t array_layer_count = 6; // Cube map

  /** Pre-filtered cube map **/
  // Texture dimensions
  WGPUExtent3D texture_extent = {
    .width              = dim,
    .height             = dim,
    .depthOrArrayLayers = array_layer_count,
  };

  // Create the texture
  {
    WGPUTextureDescriptor texture_desc = {
      .label         = "Prefiltered cube texture",
      .size          = texture_extent,
      .mipLevelCount = num_mips,
      .sampleCount   = 1,
      .dimension     = WGPUTextureDimension_2D,
      .format        = format,
      .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_TextureBinding,
    };
    prefiltered_cube.texture
      = wgpuDeviceCreateTexture(wgpu_context->device, &texture_desc);
    ASSERT(prefiltered_cube.texture != NULL);
  }

  // Create the texture view
  {
    WGPUTextureViewDescriptor texture_view_dec = {
      .label           = "Prefiltered cube texture view",
      .dimension       = WGPUTextureViewDimension_Cube,
      .format          = format,
      .baseMipLevel    = 0,
      .mipLevelCount   = num_mips,
      .baseArrayLayer  = 0,
      .arrayLayerCount = array_layer_count,
    };
    prefiltered_cube.view
      = wgpuTextureCreateView(prefiltered_cube.texture, &texture_view_dec);
    ASSERT(prefiltered_cube.view != NULL);
  }

  // Create the sampler
  {
    prefiltered_cube.sampler = wgpuDeviceCreateSampler(
      wgpu_context->device, &(WGPUSamplerDescriptor){
                              .label = "Prefiltered cube texture sampler",
                              .addressModeU  = WGPUAddressMode_ClampToEdge,
                              .addressModeV  = WGPUAddressMode_ClampToEdge,
                              .addressModeW  = WGPUAddressMode_ClampToEdge,
                              .minFilter     = WGPUFilterMode_Linear,
                              .magFilter     = WGPUFilterMode_Linear,
                              .mipmapFilter  = WGPUMipmapFilterMode_Linear,
                              .lodMinClamp   = 0.0f,
                              .lodMaxClamp   = (float)num_mips,
                              .maxAnisotropy = 1,
                            });
    ASSERT(prefiltered_cube.sampler != NULL);
  }

  // Framebuffer for offscreen rendering
  struct {
    WGPUTexture texture;
    WGPUTextureView texture_views[6 * (uint32_t)PREFILTERED_CUBE_NUM_MIPS];
  } offscreen;

  // Offscreen framebuffer
  {
    // Color attachment
    {
      // Create the texture
      WGPUTextureDescriptor texture_desc = {
        .label         = "Prefiltered cube offscreen texture",
        .size          = (WGPUExtent3D) {
          .width              = dim,
          .height             = dim,
          .depthOrArrayLayers = array_layer_count,
        },
        .mipLevelCount = num_mips,
        .sampleCount   = 1,
        .dimension     = WGPUTextureDimension_2D,
        .format        = format,
        .usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding
                 | WGPUTextureUsage_RenderAttachment,
      };
      offscreen.texture
        = wgpuDeviceCreateTexture(wgpu_context->device, &texture_desc);
      ASSERT(offscreen.texture != NULL)

      // Create the texture views
      uint32_t idx = 0;
      for (uint32_t array_layer = 0; array_layer < array_layer_count;
           ++array_layer) {
        for (uint32_t i = 0; i < num_mips; ++i) {
          idx = (array_layer * num_mips) + i;
          WGPUTextureViewDescriptor texture_view_dec = {
            .label           = "Prefiltered cube offscreen texture view",
            .aspect          = WGPUTextureAspect_All,
            .dimension       = WGPUTextureViewDimension_2D,
            .format          = texture_desc.format,
            .baseMipLevel    = i,
            .mipLevelCount   = 1,
            .baseArrayLayer  = array_layer,
            .arrayLayerCount = 1,
          };
          offscreen.texture_views[idx]
            = wgpuTextureCreateView(offscreen.texture, &texture_view_dec);
          ASSERT(offscreen.texture_views[idx] != NULL)
        }
      }
    }
  }

  struct push_block_vs_t {
    mat4 mvp;
    uint8_t padding[192];
  } push_block_vs[(uint32_t)PREFILTERED_CUBE_NUM_MIPS * 6];

  struct push_block_fs_t {
    float roughness;
    uint32_t num_samples;
    uint8_t padding[248];
  } push_block_fs[(uint32_t)PREFILTERED_CUBE_NUM_MIPS * 6];

  // Update shader push constant block data
  {
    mat4 matrices[6] = {
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_X */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_X */
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_Y */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_Y */
      GLM_MAT4_IDENTITY_INIT, /* POSITIVE_Z */
      GLM_MAT4_IDENTITY_INIT, /* NEGATIVE_Z */
    };
    /* NEGATIVE_X */
    glm_rotate(matrices[0], glm_rad(90.0f), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(matrices[0], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_X */
    glm_rotate(matrices[1], glm_rad(-90.0f), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(matrices[1], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* POSITIVE_Y */
    glm_rotate(matrices[2], glm_rad(90.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_Y */
    glm_rotate(matrices[3], glm_rad(-90.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* POSITIVE_Z */
    glm_rotate(matrices[4], glm_rad(180.0f), (vec3){1.0f, 0.0f, 0.0f});
    /* NEGATIVE_Z */
    glm_rotate(matrices[5], glm_rad(180.0f), (vec3){0.0f, 0.0f, 1.0f});

    mat4 projection = GLM_MAT4_IDENTITY_INIT;
    glm_perspective(PI / 2.0f, 1.0f, 0.1f, 512.0f, projection);
    // Sampling deltas
    uint32_t idx = 0;
    for (uint32_t m = 0; m < num_mips; ++m) {
      for (uint32_t f = 0; f < 6; ++f) {
        idx = (m * 6) + f;
        // Set vertex shader push constant block
        glm_mat4_mul(projection, matrices[f], push_block_vs[idx].mvp);
        // Set fragment shader push constant block
        push_block_fs[idx].roughness   = (float)m / (float)(num_mips - 1);
        push_block_fs[idx].num_samples = 32u;
      }
    }
  }

  static struct {
    // Vertex shader parameter uniform buffer
    struct {
      WGPUBuffer buffer;
      uint64_t buffer_size;
      uint64_t model_size;
    } vs;
    // Fragment parameter uniform buffer
    struct {
      WGPUBuffer buffer;
      uint64_t buffer_size;
      uint64_t model_size;
    } fs;
  } prefiltered_cube_ubos;

  // Vertex shader parameter uniform buffer
  {
    prefiltered_cube_ubos.vs.model_size = sizeof(mat4);
    prefiltered_cube_ubos.vs.buffer_size
      = calc_constant_buffer_byte_size(sizeof(push_block_vs));
    prefiltered_cube_ubos.vs.buffer = wgpu_create_buffer_from_data(
      wgpu_context, push_block_vs, prefiltered_cube_ubos.vs.buffer_size,
      WGPUBufferUsage_Uniform);
  }

  // Fragment shader parameter uniform buffer
  {
    prefiltered_cube_ubos.fs.model_size = sizeof(float) + sizeof(uint32_t);
    prefiltered_cube_ubos.fs.buffer_size
      = calc_constant_buffer_byte_size(sizeof(push_block_fs));
    prefiltered_cube_ubos.fs.buffer = wgpu_create_buffer_from_data(
      wgpu_context, push_block_fs, prefiltered_cube_ubos.fs.buffer_size,
      WGPUBufferUsage_Uniform);
  }

  // Bind group layout
  WGPUBindGroupLayout bind_group_layout = NULL;
  {
    WGPUBindGroupLayoutEntry bgl_entries[4] = {
      [0] = (WGPUBindGroupLayoutEntry) {
        // Binding 0: Vertex shader uniform UBO
        .binding   = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = (WGPUBufferBindingLayout) {
          .type             = WGPUBufferBindingType_Uniform,
          .hasDynamicOffset = true,
          .minBindingSize   = prefiltered_cube_ubos.vs.model_size,
        },
        .sampler = {0},
      },
      [1] = (WGPUBindGroupLayoutEntry) {
        // Binding 1: Fragment shader uniform UBO
        .binding    = 1,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = (WGPUBufferBindingLayout) {
          .type             = WGPUBufferBindingType_Uniform,
          .hasDynamicOffset = true,
          .minBindingSize   = prefiltered_cube_ubos.fs.model_size,
        },
        .sampler = {0},
      },
      [2] = (WGPUBindGroupLayoutEntry) {
        // Binding 2: Fragment shader image view
        .binding    = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = (WGPUTextureBindingLayout) {
          .sampleType    = WGPUTextureSampleType_Float,
          .viewDimension = WGPUTextureViewDimension_Cube,
          .multisampled  = false,
        },
        .storageTexture = {0},
      },
      [3] = (WGPUBindGroupLayoutEntry) {
        // Binding 3: Fragment shader image sampler
        .binding    = 3,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = (WGPUSamplerBindingLayout){
          .type = WGPUSamplerBindingType_Filtering,
        },
        .texture = {0},
      },
    };
    bind_group_layout = wgpuDeviceCreateBindGroupLayout(
      wgpu_context->device, &(WGPUBindGroupLayoutDescriptor){
                              .label      = "Bind group layout",
                              .entryCount = (uint32_t)ARRAY_SIZE(bgl_entries),
                              .entries    = bgl_entries,
                            });
    ASSERT(bind_group_layout != NULL);
  }

  // Bind group
  WGPUBindGroup bind_group = NULL;
  {
    WGPUBindGroupEntry bg_entries[4] = {
      [0] = (WGPUBindGroupEntry) {
        // Binding 0: Vertex shader uniform UBO
        .binding = 0,
        .buffer  = prefiltered_cube_ubos.vs.buffer,
        .offset  = 0,
        .size    = prefiltered_cube_ubos.vs.model_size,
      },
      [1] = (WGPUBindGroupEntry) {
        // Binding 1: Fragment shader uniform UBO
        .binding = 1,
        .buffer  = prefiltered_cube_ubos.fs.buffer,
        .offset  = 0,
        .size    = prefiltered_cube_ubos.fs.model_size,
      },
      [2] = (WGPUBindGroupEntry) {
        // Binding 2: Fragment shader image view
        .binding     = 2,
        .textureView = skybox_texture->view
      },
      [3] = (WGPUBindGroupEntry) {
        // Binding 3: Fragment shader image sampler
        .binding = 3,
        .sampler = skybox_texture->sampler,
      },
    };
    bind_group = wgpuDeviceCreateBindGroup(
      wgpu_context->device, &(WGPUBindGroupDescriptor){
                              .label      = "Bind group",
                              .layout     = bind_group_layout,
                              .entryCount = (uint32_t)ARRAY_SIZE(bg_entries),
                              .entries    = bg_entries,
                            });
    ASSERT(bind_group != NULL);
  }

  // Pipeline layout
  WGPUPipelineLayout pipeline_layout = NULL;
  {
    pipeline_layout = wgpuDeviceCreatePipelineLayout(
      wgpu_context->device, &(WGPUPipelineLayoutDescriptor){
                              .label                = "Pipeline layout",
                              .bindGroupLayoutCount = 1,
                              .bindGroupLayouts     = &bind_group_layout,
                            });
    ASSERT(pipeline_layout != NULL)
  }

  // Prefiltered cube map pipeline
  WGPURenderPipeline pipeline = NULL;
  {
    // Primitive state
    WGPUPrimitiveState primitive_state = {
      .topology  = WGPUPrimitiveTopology_TriangleList,
      .frontFace = WGPUFrontFace_CCW,
      .cullMode  = WGPUCullMode_None,
    };

    // Color target state
    WGPUBlendState blend_state              = wgpu_create_blend_state(false);
    WGPUColorTargetState color_target_state = (WGPUColorTargetState){
      .format    = format,
      .blend     = &blend_state,
      .writeMask = WGPUColorWriteMask_All,
    };

    // Vertex buffer layout
    WGPU_GLTF_VERTEX_BUFFER_LAYOUT(
      skybox,
      // Location 0: Position
      WGPU_GLTF_VERTATTR_DESC(0, WGPU_GLTF_VertexComponent_Position));

    // Vertex state
    WGPUVertexState vertex_state = wgpu_create_vertex_state(
              wgpu_context, &(wgpu_vertex_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Vertex shader SPIR-V
                .label = "filtercube_vertex_shaders",
                .file  = "shaders/pbr/filtercube.vert.spv",
              },
             .buffer_count = 1,
             .buffers = &skybox_vertex_buffer_layout,
            });

    // Fragment state
    WGPUFragmentState fragment_state = wgpu_create_fragment_state(
              wgpu_context, &(wgpu_fragment_state_t){
              .shader_desc = (wgpu_shader_desc_t){
                // Fragment shader SPIR-V
                .label = "prefilterenvmap_fragment_shaders",
                .file  = "shaders/pbr/prefilterenvmap.frag.spv",
              },
              .target_count = 1,
              .targets = &color_target_state,
            });

    // Multisample state
    WGPUMultisampleState multisample_state
      = wgpu_create_multisample_state_descriptor(
        &(create_multisample_state_desc_t){
          .sample_count = 1,
        });

    // Create rendering pipeline using the specified states
    pipeline = wgpuDeviceCreateRenderPipeline(
      wgpu_context->device, &(WGPURenderPipelineDescriptor){
                              .label  = "prefiltered_cube_map_render_pipeline",
                              .layout = pipeline_layout,
                              .primitive    = primitive_state,
                              .vertex       = vertex_state,
                              .fragment     = &fragment_state,
                              .depthStencil = NULL,
                              .multisample  = multisample_state,
                            });
    ASSERT(pipeline != NULL);

    // Partial cleanup
    WGPU_RELEASE_RESOURCE(ShaderModule, vertex_state.module);
    WGPU_RELEASE_RESOURCE(ShaderModule, fragment_state.module);
  }

  // Create the actual renderpass
  struct {
    WGPURenderPassColorAttachment color_attachment[1];
    WGPURenderPassDescriptor render_pass_descriptor;
  } render_pass = {
    .color_attachment[0]= (WGPURenderPassColorAttachment) {
        .view       = NULL, /* Assigned later */
        .loadOp     = WGPULoadOp_Clear,
        .storeOp    = WGPUStoreOp_Store,
        .clearValue = (WGPUColor) {
          .r = 0.0f,
          .g = 0.0f,
          .b = 0.2f,
          .a = 0.0f,
        },
     },
  };
  render_pass.render_pass_descriptor = (WGPURenderPassDescriptor){
    .colorAttachmentCount   = 1,
    .colorAttachments       = render_pass.color_attachment,
    .depthStencilAttachment = NULL,
  };

  // Render
  {
    wgpu_context->cmd_enc
      = wgpuDeviceCreateCommandEncoder(wgpu_context->device, NULL);

    uint32_t idx         = 0;
    float viewport_width = 0.0f, viewport_height = 0.0f;
    for (uint32_t m = 0; m < num_mips; ++m) {
      viewport_width  = (float)(dim * pow(0.5f, m));
      viewport_height = (float)(dim * pow(0.5f, m));
      for (uint32_t f = 0; f < 6; ++f) {
        render_pass.color_attachment[0].view
          = offscreen.texture_views[(f * num_mips) + m];
        idx = (m * 6) + f;
        // Render scene from cube face's point of view
        wgpu_context->rpass_enc = wgpuCommandEncoderBeginRenderPass(
          wgpu_context->cmd_enc, &render_pass.render_pass_descriptor);
        wgpuRenderPassEncoderSetViewport(wgpu_context->rpass_enc, 0.0f, 0.0f,
                                         viewport_width, viewport_height, 0.0f,
                                         1.0f);
        wgpuRenderPassEncoderSetScissorRect(wgpu_context->rpass_enc, 0u, 0u,
                                            (uint32_t)viewport_width,
                                            (uint32_t)viewport_height);
        wgpuRenderPassEncoderSetPipeline(wgpu_context->rpass_enc, pipeline);
        // Calculate the dynamic offsets
        uint32_t dynamic_offset     = idx * (uint32_t)ALIGNMENT;
        uint32_t dynamic_offsets[2] = {dynamic_offset, dynamic_offset};
        // Bind the bind group for rendering a mesh using the dynamic offset
        wgpuRenderPassEncoderSetBindGroup(wgpu_context->rpass_enc, 0,
                                          bind_group, 2, dynamic_offsets);
        // Draw object
        wgpu_gltf_model_draw(skybox, (wgpu_gltf_model_render_options_t){0});
        // End render pass
        wgpuRenderPassEncoderEnd(wgpu_context->rpass_enc);
        WGPU_RELEASE_RESOURCE(RenderPassEncoder, wgpu_context->rpass_enc)
      }
    }

    // Copy region for transfer from framebuffer to cube face
    for (uint32_t m = 0; m < num_mips; ++m) {
      WGPUExtent3D copy_size = (WGPUExtent3D){
        .width              = (float)(dim * pow(0.5f, m)),
        .height             = (float)(dim * pow(0.5f, m)),
        .depthOrArrayLayers = array_layer_count,
      };
      wgpuCommandEncoderCopyTextureToTexture(
        wgpu_context->cmd_enc,
        // source
        &(WGPUImageCopyTexture){
          .texture  = offscreen.texture,
          .mipLevel = m,
        },
        // destination
        &(WGPUImageCopyTexture){
          .texture  = prefiltered_cube.texture,
          .mipLevel = m,
        },
        // copySize
        &copy_size);
    }

    WGPUCommandBuffer command_buffer
      = wgpuCommandEncoderFinish(wgpu_context->cmd_enc, NULL);
    ASSERT(command_buffer != NULL);
    WGPU_RELEASE_RESOURCE(CommandEncoder, wgpu_context->cmd_enc)

    // Sumbit commmand buffer and cleanup
    wgpuQueueSubmit(wgpu_context->queue, 1, &command_buffer);
    WGPU_RELEASE_RESOURCE(CommandBuffer, command_buffer)
  }

  // Cleanup
  WGPU_RELEASE_RESOURCE(Texture, offscreen.texture)
  for (uint32_t i = 0; i < (uint32_t)ARRAY_SIZE(offscreen.texture_views); ++i) {
    WGPU_RELEASE_RESOURCE(TextureView, offscreen.texture_views[i])
  }
  WGPU_RELEASE_RESOURCE(Buffer, prefiltered_cube_ubos.vs.buffer)
  WGPU_RELEASE_RESOURCE(Buffer, prefiltered_cube_ubos.fs.buffer)
  WGPU_RELEASE_RESOURCE(BindGroup, bind_group)
  WGPU_RELEASE_RESOURCE(BindGroupLayout, bind_group_layout)
  WGPU_RELEASE_RESOURCE(RenderPipeline, pipeline)
  WGPU_RELEASE_RESOURCE(PipelineLayout, pipeline_layout)

  return prefiltered_cube;
}
