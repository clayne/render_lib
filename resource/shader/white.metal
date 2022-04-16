
#include <metal_stdlib>

using namespace metal;

// Vertex declaration
typedef struct vertex_t {
	vector_float4 position;
	vector_float4 color;
} vertex_t;

// Vertex shader argument buffers
struct vertex_shader_global_arg_t {
	const float4x4 world_to_clip [[ id(0) ]];
};

struct vertex_shader_material_arg_t {
	const float4 color [[ id(0) ]];
};

struct vertex_shader_instance_arg_t {
	const float4x4 model_to_world [[ id(0) ]];
	const device vertex_t* vertices [[ id(1) ]];
};

// Vertex shader to pixel shader data exchange
struct rasterizer_data_t {
	float4 position [[position]];
};

vertex rasterizer_data_t
vertex_shader(uint vid [[ vertex_id ]],
              const device vertex_shader_global_arg_t* global [[ buffer(0) ]],
              const device vertex_shader_material_arg_t* material [[ buffer(1) ]],
              const device vertex_shader_instance_arg_t* instance [[ buffer(2) ]]) {
	rasterizer_data_t out;

	out.position = (instance->vertices[vid].position * instance->model_to_world) * global->world_to_clip;

	return out;
}

fragment float4
pixel_shader(rasterizer_data_t in [[ stage_in ]]) {
	return (float4){1.0, 1.0, 1.0, 1.0};
}
