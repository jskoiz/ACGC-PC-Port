#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "acgc/metal_packet_consumer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define SKIP_NO_METAL 77

static uint32_t bits_from_float(float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void set_texture_description(
    AcgcRendererFixtureTextureDescription* description,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t data_size
) {
    memset(description, 0, sizeof(*description));
    description->version = ACGC_RENDERER_FIXTURE_VERSION;
    description->width = width;
    description->height = height;
    description->format = format;
    description->data_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
    description->data_size = data_size;
    description->tlut_byte_order = ACGC_RENDERER_FIXTURE_BIG_ENDIAN;
}

static int make_packet_from_geometry(
    AcgcGxSemanticPacket* packet,
    AcgcRendererGeometryPacket* geometry
) {
    uint32_t vertex_index;

    if (!acgc_renderer_geometry_make_triangle(geometry) ||
        !acgc_gx_semantic_packet_init(packet)) {
        return 0;
    }
    packet->vertex_count = geometry->vertex_count;
    packet->material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_VERTEX_COLOR;
    for (vertex_index = 0; vertex_index < geometry->vertex_count; vertex_index++) {
        const AcgcRendererVertex* source = &geometry->vertices[vertex_index];
        AcgcGxSemanticVertex* destination = &packet->vertices[vertex_index];

        destination->position[0] = source->position_x;
        destination->position[1] = source->position_y;
        destination->position[2] = source->position_z;
        destination->normal[1] = bits_from_float(1.0f);
        destination->color_rgba8 = source->color_rgba8;
    }
    return acgc_gx_semantic_packet_validate(packet);
}

static int make_texture_tev_fixture(
    AcgcRendererFixtureColor* output
) {
    uint8_t rgba8_data[64] = { 0 };
    uint8_t rgba8_output[4 * 4 * 4];
    AcgcRendererFixtureTextureDescription description;
    AcgcRendererFixtureSamplerDescription sampler_description = {
        ACGC_RENDERER_FIXTURE_VERSION,
        ACGC_RENDERER_FIXTURE_WRAP_REPEAT,
        ACGC_RENDERER_FIXTURE_WRAP_CLAMP,
        ACGC_RENDERER_FIXTURE_FILTER_LINEAR,
        ACGC_RENDERER_FIXTURE_FILTER_NEAREST,
        1
    };
    AcgcRendererFixtureSamplerState sampler_state;
    AcgcRendererFixtureTevState tev_state;
    AcgcRendererFixtureTevStage* stage;

    /* Existing RGBA8 tile fixture: first pixel decodes to (20, 30, 40, 10). */
    rgba8_data[0] = 0x10;
    rgba8_data[1] = 0x20;
    rgba8_data[32] = 0x30;
    rgba8_data[33] = 0x40;
    set_texture_description(
        &description,
        4,
        4,
        ACGC_RENDERER_FIXTURE_TF_RGBA8,
        sizeof(rgba8_data)
    );
    if (acgc_renderer_fixture_texture_bytes(
            description.width,
            description.height,
            description.format
        ) != sizeof(rgba8_data) ||
        !acgc_renderer_fixture_decode_texture(
            &description,
            rgba8_data,
            NULL,
            rgba8_output,
            sizeof(rgba8_output)
        ) ||
        !acgc_renderer_fixture_resolve_sampler(
            &sampler_description,
            &sampler_state
        )) {
        return 0;
    }

    memset(&tev_state, 0, sizeof(tev_state));
    tev_state.version = ACGC_RENDERER_FIXTURE_VERSION;
    tev_state.stage_count = 1;
    tev_state.texture[0] = (AcgcRendererFixtureColor){
        rgba8_output[0], rgba8_output[1], rgba8_output[2], rgba8_output[3]
    };
    stage = &tev_state.stages[0];
    stage->color_d = ACGC_RENDERER_FIXTURE_CC_TEXC;
    stage->alpha_d = ACGC_RENDERER_FIXTURE_CA_TEXA;
    stage->color_op = ACGC_RENDERER_FIXTURE_TEV_ADD;
    stage->color_clamp = 1;
    stage->alpha_op = ACGC_RENDERER_FIXTURE_TEV_ADD;
    stage->alpha_clamp = 1;
    if (!acgc_renderer_fixture_tev_evaluate(&tev_state, output)) {
        return 0;
    }
    return output->r == 0x20 && output->g == 0x30 &&
        output->b == 0x40 && output->a == 0x10 &&
        sampler_state.address_s == ACGC_RENDERER_FIXTURE_WRAP_REPEAT;
}

static int test_packet_to_state_and_geometry(void) {
    AcgcGxSemanticPacket packet;
    AcgcRendererGeometryPacket source_geometry;
    AcgcMetalPacketConsumerOutput output;
    AcgcMetalPacketConsumerTexture texture;
    AcgcRendererFixtureColor tev_color;
    AcgcGxSemanticPacket invalid;

    CHECK(make_packet_from_geometry(&packet, &source_geometry));
    CHECK(acgc_metal_packet_consumer_prepare(&packet, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(acgc_metal_state_fixture_validate(&output.state));
    CHECK(acgc_renderer_geometry_validate(&output.geometry));
    CHECK(output.state.transform.matrix[0] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[5] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[10] == bits_from_float(1.0f));
    CHECK(output.state.transform.matrix[15] == bits_from_float(1.0f));
    CHECK(output.geometry.vertices[0].position_x ==
          source_geometry.vertices[0].position_x);
    CHECK(output.geometry.vertices[1].color_rgba8 ==
          source_geometry.vertices[1].color_rgba8);
    CHECK(output.geometry.draws[0].vertex_count == 3);

    /* The packet stores row-major model-view; Metal consumes column-major. */
    packet.transform.modelview[3] = bits_from_float(0.25f);
    packet.transform.projection[0] = bits_from_float(0.5f);
    CHECK(acgc_metal_packet_consumer_prepare(&packet, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(float_from_bits(output.state.transform.matrix[0]) == 0.5f);
    CHECK(float_from_bits(output.state.transform.matrix[12]) == 0.125f);

    CHECK(make_texture_tev_fixture(&tev_color));
    texture.key = 42;
    texture.color = tev_color;
    packet.transform.modelview[3] = 0;
    packet.transform.projection[0] = bits_from_float(1.0f);
    packet.material.flags = ACGC_GX_SEMANTIC_MATERIAL_USE_TEXTURE0;
    packet.material.texture0_key = texture.key;
    CHECK(acgc_metal_packet_consumer_prepare(&packet, &texture, &output) ==
          ACGC_METAL_PACKET_CONSUMER_OK);
    CHECK(output.texture0_color.r == 0x20);
    CHECK(output.geometry.vertices[0].color_rgba8 == 0x20304010);
    CHECK(acgc_metal_packet_consumer_prepare(&packet, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_TEXTURE_REQUIRED);
    texture.key = 43;
    CHECK(acgc_metal_packet_consumer_prepare(&packet, &texture, &output) ==
          ACGC_METAL_PACKET_CONSUMER_TEXTURE_KEY_MISMATCH);

    invalid = packet;
    invalid.reserved = 1;
    CHECK(acgc_metal_packet_consumer_prepare(&invalid, &texture, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_PACKET);
    CHECK(acgc_metal_packet_consumer_prepare(NULL, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_INVALID_ARGUMENT);

    CHECK(acgc_gx_semantic_packet_init(&invalid));
    invalid.primitive = ACGC_GX_SEMANTIC_PRIMITIVE_QUADS;
    invalid.vertex_count = 4;
    CHECK(acgc_gx_semantic_packet_validate(&invalid));
    CHECK(acgc_metal_packet_consumer_prepare(&invalid, NULL, &output) ==
          ACGC_METAL_PACKET_CONSUMER_UNSUPPORTED_TOPOLOGY);
    return 0;
}

static MTLCompareFunction metal_compare_function(uint32_t value) {
    switch (value) {
        case ACGC_METAL_DEPTH_NEVER: return MTLCompareFunctionNever;
        case ACGC_METAL_DEPTH_LESS: return MTLCompareFunctionLess;
        case ACGC_METAL_DEPTH_EQUAL: return MTLCompareFunctionEqual;
        case ACGC_METAL_DEPTH_LESS_EQUAL: return MTLCompareFunctionLessEqual;
        case ACGC_METAL_DEPTH_GREATER: return MTLCompareFunctionGreater;
        case ACGC_METAL_DEPTH_NOT_EQUAL: return MTLCompareFunctionNotEqual;
        case ACGC_METAL_DEPTH_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
        case ACGC_METAL_DEPTH_ALWAYS: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

static MTLBlendFactor metal_blend_factor(uint32_t value) {
    switch (value) {
        case ACGC_METAL_BLEND_ZERO: return MTLBlendFactorZero;
        case ACGC_METAL_BLEND_ONE: return MTLBlendFactorOne;
        case ACGC_METAL_BLEND_SOURCE_ALPHA: return MTLBlendFactorSourceAlpha;
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA:
            return MTLBlendFactorOneMinusSourceAlpha;
    }
    return MTLBlendFactorZero;
}

static MTLWinding metal_winding(uint32_t value) {
    return value == ACGC_METAL_WINDING_COUNTER_CLOCKWISE
        ? MTLWindingCounterClockwise
        : MTLWindingClockwise;
}

static MTLCullMode metal_cull_mode(uint32_t value) {
    switch (value) {
        case ACGC_METAL_CULL_NONE: return MTLCullModeNone;
        case ACGC_METAL_CULL_FRONT: return MTLCullModeFront;
        case ACGC_METAL_CULL_BACK: return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLTriangleFillMode metal_fill_mode(uint32_t value) {
    return value == ACGC_METAL_TRIANGLE_LINES
        ? MTLTriangleFillModeLines
        : MTLTriangleFillModeFill;
}

static const char ACGC_METAL_PACKET_CONSUMER_SHADER[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct AcgcRendererVertex {\n"
    "    uint position_x;\n"
    "    uint position_y;\n"
    "    uint position_z;\n"
    "    uint color_rgba8;\n"
    "};\n"
    "struct AcgcMetalFixedTransform { uint matrix[16]; };\n"
    "struct Output { float4 position [[position]]; float4 color; };\n"
    "vertex Output acgc_metal_packet_consumer_vertex(\n"
    "    const device AcgcRendererVertex* vertices [[buffer(0)]],\n"
    "    constant AcgcMetalFixedTransform& transform [[buffer(1)]],\n"
    "    uint vertex_id [[vertex_id]]) {\n"
    "    AcgcRendererVertex geometry_vertex = vertices[vertex_id];\n"
    "    float4x4 matrix = float4x4(\n"
    "        float4(as_type<float>(transform.matrix[0]), as_type<float>(transform.matrix[1]), as_type<float>(transform.matrix[2]), as_type<float>(transform.matrix[3])),\n"
    "        float4(as_type<float>(transform.matrix[4]), as_type<float>(transform.matrix[5]), as_type<float>(transform.matrix[6]), as_type<float>(transform.matrix[7])),\n"
    "        float4(as_type<float>(transform.matrix[8]), as_type<float>(transform.matrix[9]), as_type<float>(transform.matrix[10]), as_type<float>(transform.matrix[11])),\n"
    "        float4(as_type<float>(transform.matrix[12]), as_type<float>(transform.matrix[13]), as_type<float>(transform.matrix[14]), as_type<float>(transform.matrix[15])));\n"
    "    Output output;\n"
    "    output.position = matrix * float4(as_type<float>(geometry_vertex.position_x), as_type<float>(geometry_vertex.position_y), as_type<float>(geometry_vertex.position_z), 1.0f);\n"
    "    output.color = float4(float((geometry_vertex.color_rgba8 >> 24) & 0xffu), float((geometry_vertex.color_rgba8 >> 16) & 0xffu), float((geometry_vertex.color_rgba8 >> 8) & 0xffu), float(geometry_vertex.color_rgba8 & 0xffu)) / 255.0f;\n"
    "    return output;\n"
    "}\n"
    "fragment float4 acgc_metal_packet_consumer_fragment(Output input [[stage_in]]) { return input.color; }\n";

static int encode_packet_offscreen(
    id<MTLDevice> device,
    const AcgcMetalPacketConsumerOutput* output
) {
    NSError* error = nil;
    id<MTLLibrary> library;
    id<MTLFunction> vertex_function;
    id<MTLFunction> fragment_function;
    MTLRenderPipelineDescriptor* pipeline_descriptor;
    MTLRenderPipelineColorAttachmentDescriptor* color_attachment;
    id<MTLRenderPipelineState> pipeline;
    MTLDepthStencilDescriptor* depth_descriptor;
    id<MTLDepthStencilState> depth_state;
    MTLTextureDescriptor* color_descriptor;
    MTLTextureDescriptor* depth_descriptor_texture;
    id<MTLTexture> color_texture;
    id<MTLTexture> depth_texture;
    MTLRenderPassDescriptor* render_pass;
    id<MTLCommandQueue> command_queue;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLBuffer> vertex_buffer;
    id<MTLBuffer> transform_buffer;
    MTLViewport viewport;

    library = [device newLibraryWithSource:
        [NSString stringWithUTF8String:ACGC_METAL_PACKET_CONSUMER_SHADER]
        options:nil
        error:&error];
    if (library == nil) {
        fprintf(stderr, "Metal packet consumer shader compile failed: %s\n",
                error.localizedDescription.UTF8String);
        return 1;
    }
    vertex_function = [library newFunctionWithName:
        @"acgc_metal_packet_consumer_vertex"];
    fragment_function = [library newFunctionWithName:
        @"acgc_metal_packet_consumer_fragment"];
    CHECK(vertex_function != nil);
    CHECK(fragment_function != nil);

    pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline_descriptor.vertexFunction = vertex_function;
    pipeline_descriptor.fragmentFunction = fragment_function;
    pipeline_descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    color_attachment = pipeline_descriptor.colorAttachments[0];
    color_attachment.pixelFormat = MTLPixelFormatRGBA8Unorm;
    color_attachment.blendingEnabled = output->state.blend.enabled != 0;
    color_attachment.sourceRGBBlendFactor =
        metal_blend_factor(output->state.blend.source_rgb_factor);
    color_attachment.destinationRGBBlendFactor =
        metal_blend_factor(output->state.blend.destination_rgb_factor);
    color_attachment.sourceAlphaBlendFactor =
        metal_blend_factor(output->state.blend.source_alpha_factor);
    color_attachment.destinationAlphaBlendFactor =
        metal_blend_factor(output->state.blend.destination_alpha_factor);
    color_attachment.rgbBlendOperation = MTLBlendOperationAdd;
    color_attachment.alphaBlendOperation = MTLBlendOperationAdd;
    pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor
                                                       error:&error];
    if (pipeline == nil) {
        fprintf(stderr, "Metal packet consumer pipeline creation failed: %s\n",
                error.localizedDescription.UTF8String);
        return 1;
    }

    depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
    depth_descriptor.depthCompareFunction =
        metal_compare_function(output->state.depth.compare_function);
    depth_descriptor.depthWriteEnabled = output->state.depth.write_enabled != 0;
    depth_state = [device newDepthStencilStateWithDescriptor:depth_descriptor];
    CHECK(depth_state != nil);

    color_descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:64
                                    height:64
                                 mipmapped:NO];
    color_descriptor.usage = MTLTextureUsageRenderTarget;
    color_texture = [device newTextureWithDescriptor:color_descriptor];
    CHECK(color_texture != nil);
    depth_descriptor_texture = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:64
                                    height:64
                                 mipmapped:NO];
    depth_descriptor_texture.usage = MTLTextureUsageRenderTarget;
    depth_texture = [device newTextureWithDescriptor:depth_descriptor_texture];
    CHECK(depth_texture != nil);

    render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
    render_pass.colorAttachments[0].texture = color_texture;
    render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    render_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    render_pass.depthAttachment.texture = depth_texture;
    render_pass.depthAttachment.loadAction = MTLLoadActionClear;
    render_pass.depthAttachment.storeAction = MTLStoreActionStore;
    render_pass.depthAttachment.clearDepth = 1.0;

    vertex_buffer = [device newBufferWithBytes:output->geometry.vertices
                                         length:sizeof(output->geometry.vertices)
                                        options:MTLResourceStorageModeShared];
    transform_buffer = [device newBufferWithBytes:&output->state.transform
                                            length:sizeof(output->state.transform)
                                           options:MTLResourceStorageModeShared];
    CHECK(vertex_buffer != nil);
    CHECK(transform_buffer != nil);
    viewport = (MTLViewport){
        float_from_bits(output->state.viewport.origin_x),
        float_from_bits(output->state.viewport.origin_y),
        float_from_bits(output->state.viewport.width),
        float_from_bits(output->state.viewport.height),
        float_from_bits(output->state.viewport.znear),
        float_from_bits(output->state.viewport.zfar)
    };
    command_queue = [device newCommandQueue];
    CHECK(command_queue != nil);
    command_buffer = [command_queue commandBuffer];
    CHECK(command_buffer != nil);
    encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass];
    CHECK(encoder != nil);
    [encoder setViewport:viewport];
    [encoder setDepthStencilState:depth_state];
    [encoder setCullMode:metal_cull_mode(output->state.raster.cull_mode)];
    [encoder setFrontFacingWinding:metal_winding(
        output->state.raster.front_facing_winding
    )];
    [encoder setTriangleFillMode:metal_fill_mode(
        output->state.raster.triangle_fill_mode
    )];
    [encoder setRenderPipelineState:pipeline];
    [encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];
    [encoder setVertexBuffer:transform_buffer offset:0 atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    CHECK(command_buffer.status == MTLCommandBufferStatusCompleted);
    return 0;
}

int main(void) {
    AcgcGxSemanticPacket packet;
    AcgcRendererGeometryPacket source_geometry;
    AcgcMetalPacketConsumerOutput output;
    id<MTLDevice> device;

    @autoreleasepool {
        CHECK(test_packet_to_state_and_geometry() == 0);
        CHECK(make_packet_from_geometry(&packet, &source_geometry));
        CHECK(acgc_metal_packet_consumer_prepare(&packet, NULL, &output) ==
              ACGC_METAL_PACKET_CONSUMER_OK);
        device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            puts("Metal packet consumer: CPU packet/state/fixture contract PASS; SKIP (no macOS Metal device available)");
            return SKIP_NO_METAL;
        }
        CHECK(encode_packet_offscreen(device, &output) == 0);
    }
    puts("Metal packet consumer: PASS (offscreen encoder only; live game-frame gate remains open)");
    return 0;
}
