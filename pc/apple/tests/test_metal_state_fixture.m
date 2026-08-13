#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "acgc/metal_state_fixture.h"
#include "acgc/renderer_geometry.h"

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

static float float_from_bits(uint32_t bits) {
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
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

static MTLBlendOperation metal_blend_operation(uint32_t value) {
    switch (value) {
        case ACGC_METAL_BLEND_ADD: return MTLBlendOperationAdd;
    }
    return MTLBlendOperationAdd;
}

static MTLCullMode metal_cull_mode(uint32_t value) {
    switch (value) {
        case ACGC_METAL_CULL_NONE: return MTLCullModeNone;
        case ACGC_METAL_CULL_FRONT: return MTLCullModeFront;
        case ACGC_METAL_CULL_BACK: return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLWinding metal_winding(uint32_t value) {
    return value == ACGC_METAL_WINDING_COUNTER_CLOCKWISE
        ? MTLWindingCounterClockwise
        : MTLWindingClockwise;
}

static MTLTriangleFillMode metal_fill_mode(uint32_t value) {
    return value == ACGC_METAL_TRIANGLE_LINES
        ? MTLTriangleFillModeLines
        : MTLTriangleFillModeFill;
}

static const char ACGC_METAL_STATE_FIXTURE_SHADER[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct AcgcMetalGeometryVertex {\n"
    "    uint position_x;\n"
    "    uint position_y;\n"
    "    uint position_z;\n"
    "    uint color_rgba8;\n"
    "};\n"
    "\n"
    "struct AcgcMetalFixedTransform {\n"
    "    uint matrix[16];\n"
    "};\n"
    "\n"
    "struct AcgcMetalStateFixtureOutput {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex AcgcMetalStateFixtureOutput acgc_metal_state_fixture_vertex(\n"
    "    const device AcgcMetalGeometryVertex* vertices [[buffer(0)]],\n"
    "    constant AcgcMetalFixedTransform& transform [[buffer(1)]],\n"
    "    uint vertex_id [[vertex_id]]\n"
    ") {\n"
    "    AcgcMetalGeometryVertex vertex = vertices[vertex_id];\n"
    "    float4x4 matrix = float4x4(\n"
    "        float4(as_type<float>(transform.matrix[0]),\n"
    "                as_type<float>(transform.matrix[1]),\n"
    "                as_type<float>(transform.matrix[2]),\n"
    "                as_type<float>(transform.matrix[3])),\n"
    "        float4(as_type<float>(transform.matrix[4]),\n"
    "                as_type<float>(transform.matrix[5]),\n"
    "                as_type<float>(transform.matrix[6]),\n"
    "                as_type<float>(transform.matrix[7])),\n"
    "        float4(as_type<float>(transform.matrix[8]),\n"
    "                as_type<float>(transform.matrix[9]),\n"
    "                as_type<float>(transform.matrix[10]),\n"
    "                as_type<float>(transform.matrix[11])),\n"
    "        float4(as_type<float>(transform.matrix[12]),\n"
    "                as_type<float>(transform.matrix[13]),\n"
    "                as_type<float>(transform.matrix[14]),\n"
    "                as_type<float>(transform.matrix[15]))\n"
    "    );\n"
    "    AcgcMetalStateFixtureOutput output;\n"
    "    output.position = matrix * float4(\n"
    "        as_type<float>(vertex.position_x),\n"
    "        as_type<float>(vertex.position_y),\n"
    "        as_type<float>(vertex.position_z),\n"
    "        1.0f\n"
    "    );\n"
    "    output.color = float4(\n"
    "        float((vertex.color_rgba8 >> 24) & 0xffu),\n"
    "        float((vertex.color_rgba8 >> 16) & 0xffu),\n"
    "        float((vertex.color_rgba8 >> 8) & 0xffu),\n"
    "        float(vertex.color_rgba8 & 0xffu)\n"
    "    ) / 255.0f;\n"
    "    return output;\n"
    "}\n"
    "\n"
    "fragment float4 acgc_metal_state_fixture_fragment(\n"
    "    AcgcMetalStateFixtureOutput input [[stage_in]]\n"
    ") {\n"
    "    return input.color;\n"
    "}\n";

/*
 * The packet below is the deterministic Apple triangle fixture. No live GX
 * packet enters this target, so completion is fixture evidence only until a
 * real GX submission is supplied at the host boundary.
 */
static int test_fixed_width_fixture_contract(
    AcgcMetalStateFixture* state,
    AcgcRendererGeometryPacket* geometry
) {
    AcgcMetalStateFixture invalid;

    CHECK(acgc_metal_state_fixture_make(state));
    CHECK(acgc_metal_state_fixture_validate(state));
    CHECK(acgc_renderer_geometry_make_triangle(geometry));
    CHECK(acgc_renderer_geometry_validate(geometry));
    CHECK(sizeof(state->transform.matrix[0]) == sizeof(uint32_t));
    CHECK(state->transform.matrix[0] == ACGC_METAL_FLOAT_ONE);
    CHECK(state->transform.matrix[5] == ACGC_METAL_FLOAT_ONE);
    CHECK(state->transform.matrix[10] == ACGC_METAL_FLOAT_ONE);
    CHECK(state->transform.matrix[12] == ACGC_METAL_FLOAT_EIGHTH);
    CHECK(state->transform.matrix[13] == ACGC_METAL_FLOAT_NEGATIVE_QUARTER);
    CHECK(float_from_bits(state->viewport.width) == 64.0f);
    CHECK(float_from_bits(state->viewport.height) == 64.0f);
    CHECK(state->depth.compare_function == ACGC_METAL_DEPTH_LESS_EQUAL);
    CHECK(state->depth.write_enabled == 1);
    CHECK(state->blend.enabled == 1);
    CHECK(state->raster.cull_mode == ACGC_METAL_CULL_BACK);
    CHECK(geometry->draws[0].primitive == ACGC_RENDERER_PRIMITIVE_TRIANGLES);
    CHECK(geometry->draws[0].first_vertex == 0);
    CHECK(geometry->draws[0].vertex_count == 3);

    invalid = *state;
    invalid.reserved = 1;
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    invalid = *state;
    invalid.transform.matrix[0] = UINT32_C(0x7F800000); /* +infinity */
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    invalid = *state;
    invalid.viewport.width = ACGC_METAL_FLOAT_ZERO;
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    invalid = *state;
    invalid.depth.compare_function = UINT32_C(8);
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    invalid = *state;
    invalid.blend.destination_rgb_factor = UINT32_C(99);
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    invalid = *state;
    invalid.raster.cull_mode = UINT32_C(99);
    CHECK(!acgc_metal_state_fixture_validate(&invalid));
    return 0;
}

static int test_offscreen_metal_encoder(
    id<MTLDevice> device,
    const AcgcMetalStateFixture* state,
    const AcgcRendererGeometryPacket* geometry
) {
    NSError* error = nil;
    id<MTLLibrary> library;
    id<MTLFunction> vertex_function;
    id<MTLFunction> fragment_function;
    MTLRenderPipelineDescriptor* pipeline_descriptor;
    id<MTLRenderPipelineState> pipeline;
    MTLDepthStencilDescriptor* depth_descriptor;
    id<MTLDepthStencilState> depth_state;
    MTLTextureDescriptor* color_descriptor;
    MTLTextureDescriptor* depth_texture_descriptor;
    id<MTLTexture> color_texture;
    id<MTLTexture> depth_texture;
    MTLRenderPassDescriptor* render_pass;
    id<MTLCommandQueue> command_queue;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLBuffer> vertex_buffer;
    id<MTLBuffer> transform_buffer;
    MTLRenderPipelineColorAttachmentDescriptor* color_attachment;
    MTLViewport viewport;
    AcgcRendererDraw draw;

    library = [device newLibraryWithSource:
        [NSString stringWithUTF8String:ACGC_METAL_STATE_FIXTURE_SHADER]
        options:nil
        error:&error];
    if (library == nil) {
        fprintf(stderr, "Metal fixture shader compile failed: %s\n",
                error.localizedDescription.UTF8String);
        return 1;
    }
    vertex_function = [library newFunctionWithName:
        @"acgc_metal_state_fixture_vertex"];
    fragment_function = [library newFunctionWithName:
        @"acgc_metal_state_fixture_fragment"];
    CHECK(vertex_function != nil);
    CHECK(fragment_function != nil);

    pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline_descriptor.label = @"ACGC Metal state encoder fixture";
    pipeline_descriptor.vertexFunction = vertex_function;
    pipeline_descriptor.fragmentFunction = fragment_function;
    pipeline_descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    color_attachment = pipeline_descriptor.colorAttachments[0];
    color_attachment.pixelFormat = MTLPixelFormatRGBA8Unorm;
    color_attachment.blendingEnabled = state->blend.enabled != 0;
    color_attachment.sourceRGBBlendFactor =
        metal_blend_factor(state->blend.source_rgb_factor);
    color_attachment.destinationRGBBlendFactor =
        metal_blend_factor(state->blend.destination_rgb_factor);
    color_attachment.sourceAlphaBlendFactor =
        metal_blend_factor(state->blend.source_alpha_factor);
    color_attachment.destinationAlphaBlendFactor =
        metal_blend_factor(state->blend.destination_alpha_factor);
    color_attachment.rgbBlendOperation =
        metal_blend_operation(state->blend.rgb_operation);
    color_attachment.alphaBlendOperation =
        metal_blend_operation(state->blend.alpha_operation);
    CHECK(color_attachment.blendingEnabled);
    CHECK(color_attachment.sourceRGBBlendFactor == MTLBlendFactorSourceAlpha);
    CHECK(color_attachment.destinationRGBBlendFactor ==
          MTLBlendFactorOneMinusSourceAlpha);
    CHECK(color_attachment.sourceAlphaBlendFactor == MTLBlendFactorOne);
    CHECK(color_attachment.destinationAlphaBlendFactor ==
          MTLBlendFactorOneMinusSourceAlpha);
    CHECK(color_attachment.rgbBlendOperation == MTLBlendOperationAdd);
    CHECK(color_attachment.alphaBlendOperation == MTLBlendOperationAdd);
    CHECK(pipeline_descriptor.depthAttachmentPixelFormat ==
          MTLPixelFormatDepth32Float);

    pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor
                                                       error:&error];
    if (pipeline == nil) {
        fprintf(stderr, "Metal fixture pipeline creation failed: %s\n",
                error.localizedDescription.UTF8String);
        return 1;
    }

    depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
    depth_descriptor.depthCompareFunction =
        metal_compare_function(state->depth.compare_function);
    depth_descriptor.depthWriteEnabled = state->depth.write_enabled != 0;
    CHECK(depth_descriptor.depthCompareFunction == MTLCompareFunctionLessEqual);
    CHECK(depth_descriptor.depthWriteEnabled);
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

    depth_texture_descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:64
                                    height:64
                                 mipmapped:NO];
    depth_texture_descriptor.usage = MTLTextureUsageRenderTarget;
    depth_texture = [device newTextureWithDescriptor:depth_texture_descriptor];
    CHECK(depth_texture != nil);

    render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
    render_pass.colorAttachments[0].texture = color_texture;
    render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    render_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.125, 0.25, 0.5, 1.0);
    render_pass.depthAttachment.texture = depth_texture;
    render_pass.depthAttachment.loadAction = MTLLoadActionClear;
    render_pass.depthAttachment.storeAction = MTLStoreActionStore;
    render_pass.depthAttachment.clearDepth = 1.0;

    vertex_buffer = [device newBufferWithBytes:geometry->vertices
                                         length:sizeof(geometry->vertices)
                                        options:MTLResourceStorageModeShared];
    transform_buffer = [device newBufferWithBytes:&state->transform
                                            length:sizeof(state->transform)
                                           options:MTLResourceStorageModeShared];
    CHECK(vertex_buffer != nil);
    CHECK(transform_buffer != nil);
    CHECK(vertex_buffer.length == sizeof(geometry->vertices));
    CHECK(transform_buffer.length == sizeof(AcgcMetalFixedTransform));
    CHECK(memcmp(transform_buffer.contents,
                 &state->transform,
                 sizeof(state->transform)) == 0);

    viewport = (MTLViewport){
        float_from_bits(state->viewport.origin_x),
        float_from_bits(state->viewport.origin_y),
        float_from_bits(state->viewport.width),
        float_from_bits(state->viewport.height),
        float_from_bits(state->viewport.znear),
        float_from_bits(state->viewport.zfar)
    };
    CHECK(viewport.originX == 0.0);
    CHECK(viewport.originY == 0.0);
    CHECK(viewport.width == 64.0);
    CHECK(viewport.height == 64.0);
    CHECK(viewport.znear == 0.0);
    CHECK(viewport.zfar == 1.0);

    command_queue = [device newCommandQueue];
    CHECK(command_queue != nil);
    command_buffer = [command_queue commandBuffer];
    CHECK(command_buffer != nil);
    command_buffer.label = @"ACGC offscreen Metal geometry/state fixture";
    encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass];
    CHECK(encoder != nil);
    [encoder setViewport:viewport];
    [encoder setDepthStencilState:depth_state];
    [encoder setCullMode:metal_cull_mode(state->raster.cull_mode)];
    [encoder setFrontFacingWinding:metal_winding(state->raster.front_facing_winding)];
    [encoder setTriangleFillMode:metal_fill_mode(state->raster.triangle_fill_mode)];
    [encoder setRenderPipelineState:pipeline];
    [encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];
    [encoder setVertexBuffer:transform_buffer offset:0 atIndex:1];
    draw = geometry->draws[0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:(NSUInteger)draw.first_vertex
                vertexCount:(NSUInteger)draw.vertex_count];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    CHECK(command_buffer.status == MTLCommandBufferStatusCompleted);
    return 0;
}

int main(void) {
    AcgcMetalStateFixture state;
    AcgcRendererGeometryPacket geometry;
    id<MTLDevice> device;

    @autoreleasepool {
        CHECK(test_fixed_width_fixture_contract(&state, &geometry) == 0);
        device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            puts("Metal state fixture: CPU contract PASS; SKIP (no macOS Metal device available)");
            return SKIP_NO_METAL;
        }
        CHECK(test_offscreen_metal_encoder(device, &state, &geometry) == 0);
    }
    puts("Metal state fixture: PASS (offscreen encoder fixture; not game-frame proof)");
    return 0;
}
