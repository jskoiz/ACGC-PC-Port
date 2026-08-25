#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "acgc/metal_sink.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct AcgcMetalSinkState {
    atomic_uint_least32_t initialized;
    atomic_uint_least32_t available;
    atomic_uint_least32_t submit_count;
    atomic_uint_least32_t completed_count;
    atomic_uint_least32_t readback_count;
    atomic_uint_least32_t last_status;
    atomic_uint_least32_t last_pixel_rgba8;
    atomic_uint_least32_t last_checksum;
    atomic_uint_least32_t unavailable_status;
} AcgcMetalSinkState;

static AcgcMetalSinkState s_sink_state = {
    .last_status = ACGC_METAL_SINK_NOT_INITIALIZED,
    .unavailable_status = ACGC_METAL_SINK_NOT_INITIALIZED
};

static id<MTLDevice> s_device;
static id<MTLCommandQueue> s_command_queue;
static id<MTLLibrary> s_library;
static id<MTLTexture> s_color_texture;
static id<MTLTexture> s_depth_texture;

static const char ACGC_METAL_SINK_SHADER[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct AcgcMetalSinkVertex {\n"
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
    "struct AcgcMetalSinkOutput {\n"
    "    float4 position [[position]];\n"
    "    float4 color;\n"
    "};\n"
    "\n"
    "vertex AcgcMetalSinkOutput acgc_metal_sink_vertex(\n"
    "    const device AcgcMetalSinkVertex* vertices [[buffer(0)]],\n"
    "    constant AcgcMetalFixedTransform& transform [[buffer(1)]],\n"
    "    uint vertex_id [[vertex_id]]\n"
    ") {\n"
    "    AcgcMetalSinkVertex sink_vertex = vertices[vertex_id];\n"
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
    "    AcgcMetalSinkOutput output;\n"
    "    output.position = matrix * float4(\n"
    "        as_type<float>(sink_vertex.position_x),\n"
    "        as_type<float>(sink_vertex.position_y),\n"
    "        as_type<float>(sink_vertex.position_z),\n"
    "        1.0f\n"
    "    );\n"
    "    output.color = float4(\n"
    "        float((sink_vertex.color_rgba8 >> 24) & 0xffu),\n"
    "        float((sink_vertex.color_rgba8 >> 16) & 0xffu),\n"
    "        float((sink_vertex.color_rgba8 >> 8) & 0xffu),\n"
    "        float(sink_vertex.color_rgba8 & 0xffu)\n"
    "    ) / 255.0f;\n"
    "    return output;\n"
    "}\n"
    "\n"
    "fragment float4 acgc_metal_sink_fragment(\n"
    "    AcgcMetalSinkOutput input [[stage_in]]\n"
    ") {\n"
    "    return input.color;\n"
    "}\n";

static void increment_counter(atomic_uint_least32_t* counter) {
    uint_least32_t expected =
        atomic_load_explicit(counter, memory_order_relaxed);

    while (expected != UINT32_MAX &&
           !atomic_compare_exchange_weak_explicit(
               counter,
               &expected,
               expected + 1,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
        /* expected is refreshed by a failed compare-exchange. */
    }
}

static void set_last_status(AcgcMetalSinkStatus status) {
    atomic_store_explicit(
        &s_sink_state.last_status,
        (uint_least32_t)status,
        memory_order_release
    );
}

static const char* error_description(NSError* error) {
    const char* description = error.localizedDescription.UTF8String;

    return description != NULL ? description : "unknown Metal error";
}

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
        case ACGC_METAL_BLEND_DESTINATION_COLOR:
            return MTLBlendFactorDestinationColor;
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_COLOR:
            return MTLBlendFactorOneMinusDestinationColor;
        case ACGC_METAL_BLEND_SOURCE_COLOR:
            return MTLBlendFactorSourceColor;
        case ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR:
            return MTLBlendFactorOneMinusSourceColor;
        case ACGC_METAL_BLEND_DESTINATION_ALPHA:
            return MTLBlendFactorDestinationAlpha;
        case ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_ALPHA:
            return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorZero;
}

static MTLBlendOperation metal_blend_operation(uint32_t value) {
    switch (value) {
        case ACGC_METAL_BLEND_ADD: return MTLBlendOperationAdd;
        case ACGC_METAL_BLEND_REVERSE_SUBTRACT:
            return MTLBlendOperationReverseSubtract;
    }
    return MTLBlendOperationAdd;
}

static int sink_canonical_blend_factor_to_metal(
    uint32_t factor,
    int source_factor,
    uint32_t* output
) {
    if (output == NULL) {
        return 0;
    }
    switch (factor) {
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ZERO:
            *output = ACGC_METAL_BLEND_ZERO;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_ONE:
            *output = ACGC_METAL_BLEND_ONE;
            return 1;
        /* GX's numeric color aliases are interpreted by their field. */
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_COLOR:
            *output = source_factor
                ? ACGC_METAL_BLEND_DESTINATION_COLOR
                : ACGC_METAL_BLEND_SOURCE_COLOR;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_COLOR:
            *output = source_factor
                ? ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_COLOR
                : ACGC_METAL_BLEND_ONE_MINUS_SOURCE_COLOR;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_SOURCE_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_SOURCE_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_DEST_ALPHA:
            *output = ACGC_METAL_BLEND_DESTINATION_ALPHA;
            return 1;
        case ACGC_GX_SEMANTIC_V3_BLEND_FACTOR_INV_DEST_ALPHA:
            *output = ACGC_METAL_BLEND_ONE_MINUS_DESTINATION_ALPHA;
            return 1;
    }
    return 0;
}

static int sink_canonical_blend_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalBlendState* blend;
    uint32_t source_factor;
    uint32_t destination_factor;
    uint32_t operation;

    if (output == NULL ||
        output->canonical_blend_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_BLEND_DISPOSITION_MAPPED) {
        return 0;
    }
    blend = &output->canonical_blend;
    if (!acgc_gx_canonical_blend_state_validate(blend) ||
        blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_LOGIC ||
        !sink_canonical_blend_factor_to_metal(
            blend->source_factor, 1, &source_factor) ||
        !sink_canonical_blend_factor_to_metal(
            blend->destination_factor, 0, &destination_factor)) {
        return 0;
    }
    operation = blend->mode == ACGC_GX_SEMANTIC_V3_BLEND_MODE_SUBTRACT
        ? ACGC_METAL_BLEND_REVERSE_SUBTRACT
        : ACGC_METAL_BLEND_ADD;
    return output->state.blend.enabled ==
            (blend->mode != ACGC_GX_SEMANTIC_V3_BLEND_MODE_NONE) &&
        output->state.blend.source_rgb_factor == source_factor &&
        output->state.blend.destination_rgb_factor == destination_factor &&
        output->state.blend.source_alpha_factor == source_factor &&
        output->state.blend.destination_alpha_factor == destination_factor &&
        output->state.blend.rgb_operation == operation &&
        output->state.blend.alpha_operation == operation;
}

static int sink_canonical_alpha_compare_is_true(
    uint32_t compare,
    uint32_t fragment_alpha,
    uint32_t reference
) {
    switch (compare) {
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN:
            return 0;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 1:
            return fragment_alpha < reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 2:
            return fragment_alpha == reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 3:
            return fragment_alpha <= reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 4:
            return fragment_alpha > reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 5:
            return fragment_alpha != reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MIN + 6:
            return fragment_alpha >= reference;
        case ACGC_GX_CANONICAL_ALPHA_COMPARE_MAX:
            return 1;
    }
    return 0;
}

static int sink_canonical_alpha_predicate_is_tautology(
    const AcgcGxCanonicalAlphaState* alpha
) {
    uint32_t fragment_alpha;

    if (alpha == NULL ||
        !acgc_gx_canonical_alpha_state_validate(alpha) ||
        alpha->color_update_enable != ACGC_GX_CANONICAL_ALPHA_BOOLEAN_MAX) {
        return 0;
    }
    for (fragment_alpha = ACGC_GX_CANONICAL_ALPHA_REFERENCE_MIN;
         fragment_alpha <= ACGC_GX_CANONICAL_ALPHA_REFERENCE_MAX;
         fragment_alpha++) {
        const int first = sink_canonical_alpha_compare_is_true(
            alpha->comp0, fragment_alpha, alpha->ref0);
        const int second = sink_canonical_alpha_compare_is_true(
            alpha->comp1, fragment_alpha, alpha->ref1);
        int result;

        switch (alpha->op) {
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN:
                result = first && second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 1:
                result = first || second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MIN + 2:
                result = first != second;
                break;
            case ACGC_GX_CANONICAL_ALPHA_OPERATOR_MAX:
                result = first == second;
                break;
            default:
                return 0;
        }
        if (!result) {
            return 0;
        }
    }
    return 1;
}

static int sink_canonical_alpha_matches_state(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcGxCanonicalAlphaState* alpha;

    if (output == NULL ||
        output->canonical_alpha_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_ALPHA_DISPOSITION_PASSTHROUGH) {
        return 0;
    }
    alpha = &output->canonical_alpha;
    return sink_canonical_alpha_predicate_is_tautology(alpha) &&
        output->alpha_write_enabled == alpha->alpha_update_enable;
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

static int sink_output_is_valid(
    const AcgcMetalPacketConsumerOutput* output
) {
    const AcgcRendererDraw* draw;
    const AcgcMetalStateFixture* state;

    if (output == NULL ||
        !acgc_metal_state_fixture_validate(&output->state) ||
        !acgc_renderer_geometry_validate(&output->geometry)) {
        return 0;
    }
    if (output->source_kind ==
            ACGC_METAL_PACKET_CONSUMER_SOURCE_CANONICAL_PLAN &&
        (/* The current shader consumes only vertex color. A canonical staged
         * TEV must not silently reuse that legacy path. */
         output->canonical_tev_disposition !=
            ACGC_METAL_PACKET_CONSUMER_CANONICAL_TEV_DISPOSITION_VERTEX_COLOR_PASSTHROUGH ||
         !sink_canonical_blend_matches_state(output) ||
         !sink_canonical_alpha_matches_state(output))) {
        return 0;
    }

    state = &output->state;
    if (float_from_bits(state->viewport.origin_x) != 0.0f ||
        float_from_bits(state->viewport.origin_y) != 0.0f ||
        float_from_bits(state->viewport.width) !=
            (float)ACGC_METAL_SINK_WIDTH ||
        float_from_bits(state->viewport.height) !=
            (float)ACGC_METAL_SINK_HEIGHT ||
        float_from_bits(state->viewport.znear) != 0.0f ||
        float_from_bits(state->viewport.zfar) != 1.0f ||
        output->geometry.vertex_count == 0 ||
        output->geometry.draw_count != ACGC_RENDERER_GEOMETRY_MAX_DRAWS) {
        return 0;
    }

    draw = &output->geometry.draws[0];
    return draw->primitive == ACGC_RENDERER_PRIMITIVE_TRIANGLES &&
        draw->first_vertex == 0 &&
        draw->vertex_count == output->geometry.vertex_count;
}

static void clear_resources(void) {
    s_depth_texture = nil;
    s_color_texture = nil;
    s_library = nil;
    s_command_queue = nil;
    s_device = nil;
}

AcgcMetalSinkStatus acgc_metal_sink_init(void) {
    AcgcMetalSinkStatus status;

    if (atomic_load_explicit(
            &s_sink_state.initialized,
            memory_order_acquire
        ) != 0) {
        return (AcgcMetalSinkStatus)atomic_load_explicit(
            &s_sink_state.last_status,
            memory_order_acquire
        );
    }

    @autoreleasepool {
        NSError* error = nil;
        id<MTLFunction> vertex_function = nil;
        id<MTLFunction> fragment_function = nil;
        MTLTextureDescriptor* color_descriptor = nil;
        MTLTextureDescriptor* depth_texture_descriptor = nil;

        atomic_store_explicit(&s_sink_state.available, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.submit_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.completed_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.readback_count, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.last_pixel_rgba8, 0, memory_order_relaxed);
        atomic_store_explicit(&s_sink_state.last_checksum, 0, memory_order_relaxed);
        atomic_store_explicit(
            &s_sink_state.unavailable_status,
            ACGC_METAL_SINK_RESOURCE_FAILURE,
            memory_order_relaxed
        );

        s_device = MTLCreateSystemDefaultDevice();
        if (s_device == nil) {
            status = ACGC_METAL_SINK_NO_DEVICE;
            atomic_store_explicit(
                &s_sink_state.unavailable_status,
                status,
                memory_order_relaxed
            );
        } else {
            s_library = [s_device newLibraryWithSource:
                [NSString stringWithUTF8String:ACGC_METAL_SINK_SHADER]
                options:nil
                error:&error];
            vertex_function = [s_library newFunctionWithName:
                @"acgc_metal_sink_vertex"];
            fragment_function = [s_library newFunctionWithName:
                @"acgc_metal_sink_fragment"];
            color_descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:ACGC_METAL_SINK_WIDTH
                                            height:ACGC_METAL_SINK_HEIGHT
                                         mipmapped:NO];
            depth_texture_descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:ACGC_METAL_SINK_WIDTH
                                            height:ACGC_METAL_SINK_HEIGHT
                                         mipmapped:NO];

            if (s_library == nil || vertex_function == nil ||
                fragment_function == nil) {
                fprintf(stderr, "ACGC Metal sink shader compile failed: %s\n",
                        error_description(error));
                status = ACGC_METAL_SINK_RESOURCE_FAILURE;
            } else {
                color_descriptor.usage = MTLTextureUsageRenderTarget;
                color_descriptor.storageMode = MTLStorageModeShared;
                s_color_texture = [s_device
                    newTextureWithDescriptor:color_descriptor];

                depth_texture_descriptor.usage = MTLTextureUsageRenderTarget;
                depth_texture_descriptor.storageMode = MTLStorageModeShared;
                s_depth_texture = [s_device
                    newTextureWithDescriptor:depth_texture_descriptor];
                s_command_queue = [s_device newCommandQueue];

                if (s_color_texture == nil || s_depth_texture == nil ||
                    s_command_queue == nil) {
                    fprintf(stderr,
                            "ACGC Metal sink resource creation failed: %s\n",
                            error_description(error));
                    clear_resources();
                    status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                } else {
                    status = ACGC_METAL_SINK_OK;
                }
            }

            if (status != ACGC_METAL_SINK_OK) {
                clear_resources();
            }
            atomic_store_explicit(
                &s_sink_state.unavailable_status,
                status,
                memory_order_relaxed
            );
        }

        atomic_store_explicit(
            &s_sink_state.available,
            status == ACGC_METAL_SINK_OK ? 1 : 0,
            memory_order_release
        );
        set_last_status(status);
        atomic_store_explicit(&s_sink_state.initialized, 1, memory_order_release);
    }

    return status;
}

void acgc_metal_sink_shutdown(void) {
    if (atomic_load_explicit(
            &s_sink_state.initialized,
            memory_order_acquire
        ) == 0) {
        return;
    }

    atomic_store_explicit(&s_sink_state.available, 0, memory_order_release);
    clear_resources();
    atomic_store_explicit(
        &s_sink_state.unavailable_status,
        ACGC_METAL_SINK_NOT_INITIALIZED,
        memory_order_relaxed
    );
    set_last_status(ACGC_METAL_SINK_NOT_INITIALIZED);
    atomic_store_explicit(&s_sink_state.initialized, 0, memory_order_release);
}

AcgcMetalSinkStatus acgc_metal_sink_submit(
    const AcgcMetalPacketConsumerOutput* output
) {
    AcgcMetalSinkStatus status = ACGC_METAL_SINK_NOT_INITIALIZED;

    @autoreleasepool {
        if (atomic_load_explicit(
                &s_sink_state.initialized,
                memory_order_acquire
            ) == 0) {
            set_last_status(status);
        } else {
            NSError* error = nil;
            id<MTLBuffer> vertex_buffer = nil;
            id<MTLBuffer> transform_buffer = nil;
            MTLRenderPipelineDescriptor* pipeline_descriptor = nil;
            MTLRenderPipelineColorAttachmentDescriptor* color_attachment = nil;
            id<MTLRenderPipelineState> pipeline = nil;
            MTLDepthStencilDescriptor* depth_descriptor = nil;
            id<MTLDepthStencilState> depth_state = nil;
            MTLRenderPassDescriptor* render_pass = nil;
            id<MTLCommandBuffer> command_buffer = nil;
            id<MTLRenderCommandEncoder> encoder = nil;
            MTLViewport viewport;
            uint8_t readback[ACGC_METAL_SINK_READBACK_BYTES];
            uint32_t checksum = UINT32_C(2166136261);
            uint32_t pixel;
            size_t byte_index;

            increment_counter(&s_sink_state.submit_count);
            if (!sink_output_is_valid(output)) {
                status = ACGC_METAL_SINK_INVALID_OUTPUT;
            } else if (atomic_load_explicit(
                           &s_sink_state.available,
                           memory_order_acquire
                       ) == 0) {
                status = (AcgcMetalSinkStatus)atomic_load_explicit(
                    &s_sink_state.unavailable_status,
                    memory_order_acquire
                );
            } else {
                vertex_buffer = [s_device
                    newBufferWithBytes:output->geometry.vertices
                                 length:(NSUInteger)output->geometry.vertex_count *
                                     sizeof(output->geometry.vertices[0])
                                options:MTLResourceStorageModeShared];
                transform_buffer = [s_device
                    newBufferWithBytes:&output->state.transform
                                 length:sizeof(output->state.transform)
                                options:MTLResourceStorageModeShared];
                render_pass = [MTLRenderPassDescriptor renderPassDescriptor];
                render_pass.colorAttachments[0].texture = s_color_texture;
                render_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                render_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                render_pass.colorAttachments[0].clearColor =
                    MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                render_pass.depthAttachment.texture = s_depth_texture;
                render_pass.depthAttachment.loadAction = MTLLoadActionClear;
                render_pass.depthAttachment.storeAction = MTLStoreActionStore;
                render_pass.depthAttachment.clearDepth = 1.0;
                viewport = (MTLViewport){
                    float_from_bits(output->state.viewport.origin_x),
                    float_from_bits(output->state.viewport.origin_y),
                    float_from_bits(output->state.viewport.width),
                    float_from_bits(output->state.viewport.height),
                    float_from_bits(output->state.viewport.znear),
                    float_from_bits(output->state.viewport.zfar)
                };

                pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
                pipeline_descriptor.vertexFunction = [s_library
                    newFunctionWithName:@"acgc_metal_sink_vertex"];
                pipeline_descriptor.fragmentFunction = [s_library
                    newFunctionWithName:@"acgc_metal_sink_fragment"];
                pipeline_descriptor.depthAttachmentPixelFormat =
                    MTLPixelFormatDepth32Float;
                color_attachment = pipeline_descriptor.colorAttachments[0];
                color_attachment.pixelFormat = MTLPixelFormatRGBA8Unorm;
                color_attachment.blendingEnabled =
                    output->state.blend.enabled != 0;
                color_attachment.sourceRGBBlendFactor = metal_blend_factor(
                    output->state.blend.source_rgb_factor
                );
                color_attachment.destinationRGBBlendFactor = metal_blend_factor(
                    output->state.blend.destination_rgb_factor
                );
                color_attachment.sourceAlphaBlendFactor = metal_blend_factor(
                    output->state.blend.source_alpha_factor
                );
                color_attachment.destinationAlphaBlendFactor = metal_blend_factor(
                    output->state.blend.destination_alpha_factor
                );
                color_attachment.writeMask = output->alpha_write_enabled
                    ? (MTLColorWriteMaskRed |
                       MTLColorWriteMaskGreen |
                       MTLColorWriteMaskBlue |
                       MTLColorWriteMaskAlpha)
                    : (MTLColorWriteMaskRed |
                       MTLColorWriteMaskGreen |
                       MTLColorWriteMaskBlue);
                color_attachment.rgbBlendOperation = metal_blend_operation(
                    output->state.blend.rgb_operation
                );
                color_attachment.alphaBlendOperation = metal_blend_operation(
                    output->state.blend.alpha_operation
                );
                pipeline = [s_device
                    newRenderPipelineStateWithDescriptor:pipeline_descriptor
                                                   error:&error];
                depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
                depth_descriptor.depthCompareFunction = metal_compare_function(
                    output->state.depth.compare_function
                );
                depth_descriptor.depthWriteEnabled =
                    output->state.depth.write_enabled != 0;
                depth_state = [s_device
                    newDepthStencilStateWithDescriptor:depth_descriptor];

                if (vertex_buffer == nil || transform_buffer == nil ||
                    pipeline == nil || depth_state == nil) {
                    status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                } else {
                    command_buffer = [s_command_queue commandBuffer];
                    encoder = [command_buffer
                        renderCommandEncoderWithDescriptor:render_pass];
                    if (command_buffer == nil || encoder == nil) {
                        status = ACGC_METAL_SINK_RESOURCE_FAILURE;
                    } else {
                        [encoder setViewport:viewport];
                        [encoder setDepthStencilState:depth_state];
                        [encoder setCullMode:metal_cull_mode(
                            output->state.raster.cull_mode
                        )];
                        [encoder setFrontFacingWinding:metal_winding(
                            output->state.raster.front_facing_winding
                        )];
                        [encoder setTriangleFillMode:metal_fill_mode(
                            output->state.raster.triangle_fill_mode
                        )];
                        [encoder setRenderPipelineState:pipeline];
                        [encoder setVertexBuffer:vertex_buffer
                                           offset:0
                                          atIndex:0];
                        [encoder setVertexBuffer:transform_buffer
                                           offset:0
                                          atIndex:1];
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                    vertexStart:(NSUInteger)output->geometry.draws[0].first_vertex
                                    vertexCount:(NSUInteger)output->geometry.draws[0].vertex_count];
                        [encoder endEncoding];
                        [command_buffer commit];
                        [command_buffer waitUntilCompleted];
                        if (command_buffer.status !=
                            MTLCommandBufferStatusCompleted) {
                            fprintf(stderr,
                                    "ACGC Metal sink command buffer failed: %s\n",
                                    error_description(command_buffer.error));
                            status = ACGC_METAL_SINK_COMMAND_BUFFER_FAILURE;
                        } else {
                            increment_counter(&s_sink_state.completed_count);
                            [s_color_texture
                                getBytes:readback
                                bytesPerRow:ACGC_METAL_SINK_WIDTH * 4
                                fromRegion:MTLRegionMake2D(
                                    0,
                                    0,
                                    ACGC_METAL_SINK_WIDTH,
                                    ACGC_METAL_SINK_HEIGHT
                                )
                                mipmapLevel:0];
                            for (byte_index = 0;
                                 byte_index < sizeof(readback);
                                 byte_index++) {
                                checksum =
                                    (checksum ^ readback[byte_index]) *
                                    UINT32_C(16777619);
                            }
                            byte_index =
                                ((ACGC_METAL_SINK_HEIGHT / 2) *
                                    ACGC_METAL_SINK_WIDTH +
                                 (ACGC_METAL_SINK_WIDTH / 2)) * 4;
                            pixel = ((uint32_t)readback[byte_index] << 24) |
                                ((uint32_t)readback[byte_index + 1] << 16) |
                                ((uint32_t)readback[byte_index + 2] << 8) |
                                readback[byte_index + 3];
                            atomic_store_explicit(
                                &s_sink_state.last_pixel_rgba8,
                                pixel,
                                memory_order_relaxed
                            );
                            atomic_store_explicit(
                                &s_sink_state.last_checksum,
                                checksum,
                                memory_order_relaxed
                            );
                            increment_counter(&s_sink_state.readback_count);
                            status = ACGC_METAL_SINK_OK;
                        }
                    }
                }
            }
            set_last_status(status);
        }
    }

    return status;
}

void acgc_metal_sink_get_snapshot(AcgcMetalSinkSnapshot* snapshot) {
    if (snapshot == NULL) {
        return;
    }

    snapshot->initialized = atomic_load_explicit(
        &s_sink_state.initialized,
        memory_order_acquire
    );
    snapshot->available = atomic_load_explicit(
        &s_sink_state.available,
        memory_order_acquire
    );
    snapshot->submit_count = atomic_load_explicit(
        &s_sink_state.submit_count,
        memory_order_relaxed
    );
    snapshot->completed_count = atomic_load_explicit(
        &s_sink_state.completed_count,
        memory_order_relaxed
    );
    snapshot->readback_count = atomic_load_explicit(
        &s_sink_state.readback_count,
        memory_order_relaxed
    );
    snapshot->last_status = atomic_load_explicit(
        &s_sink_state.last_status,
        memory_order_acquire
    );
    snapshot->last_pixel_rgba8 = atomic_load_explicit(
        &s_sink_state.last_pixel_rgba8,
        memory_order_relaxed
    );
    snapshot->last_checksum = atomic_load_explicit(
        &s_sink_state.last_checksum,
        memory_order_relaxed
    );
}
