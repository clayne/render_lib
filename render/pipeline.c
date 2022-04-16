/* pipeline.c  -  Render library  -  Public Domain  -  2017 Mattias Jansson
 *
 * This library provides a cross-platform rendering library in C11 providing
 * basic 2D/3D rendering functionality for projects based on our foundation library.
 *
 * The latest source code maintained by Mattias Jansson is always available at
 *
 * https://github.com/mjansson/render_lib
 *
 * The dependent library source code maintained by Mattias Jansson is always available at
 *
 * https://github.com/mjansson
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any
 * restrictions.
 *
 */

#include <render/pipeline.h>
#include <render/backend.h>
#include <render/hashstrings.h>

#include <foundation/array.h>

render_pipeline_t*
render_pipeline_allocate(render_backend_t* backend, uint capacity) {
	return backend->vtable.pipeline_allocate(backend, capacity);
}

void
render_pipeline_deallocate(render_pipeline_t* pipeline) {
	if (pipeline && pipeline->backend)
		pipeline->backend->vtable.pipeline_deallocate(pipeline->backend, pipeline);
}

void
render_pipeline_set_color_attachment(render_pipeline_t* pipeline, uint slot, render_target_t* target) {
	pipeline->backend->vtable.pipeline_set_color_attachment(pipeline->backend, pipeline, slot, target);
}

void
render_pipeline_set_depth_attachment(render_pipeline_t* pipeline, render_target_t* target) {
	pipeline->backend->vtable.pipeline_set_depth_attachment(pipeline->backend, pipeline, target);
}

void
render_pipeline_set_color_clear(render_pipeline_t* pipeline, uint slot, render_clear_action_t action, vector_t color) {
	pipeline->backend->vtable.pipeline_set_color_clear(pipeline->backend, pipeline, slot, action, color);
}

void
render_pipeline_set_depth_clear(render_pipeline_t* pipeline, render_clear_action_t action, vector_t color) {
	pipeline->backend->vtable.pipeline_set_depth_clear(pipeline->backend, pipeline, action, color);
}

void
render_pipeline_flush(render_pipeline_t* pipeline) {
	pipeline->backend->vtable.pipeline_flush(pipeline->backend, pipeline);
	pipeline->primitive_buffer->used = 0;
}

void
render_pipeline_queue(render_pipeline_t* pipeline, render_primitive_type type, const render_primitive_t* primitive) {
	FOUNDATION_UNUSED(type);
	if (pipeline->primitive_buffer->used < pipeline->primitive_buffer->allocated) {
		render_primitive_t* primitve_store = pipeline->primitive_buffer->store;
		primitve_store[pipeline->primitive_buffer->used++] = *primitive;
	}
}

render_pipeline_state_t
render_pipeline_state_allocate(render_pipeline_t* pipeline, render_shader_t* shader) {
	return pipeline->backend->vtable.pipeline_state_allocate(pipeline->backend, pipeline, shader);
}

void
render_pipeline_state_deallocate(render_pipeline_t* pipeline, render_pipeline_state_t state) {
	pipeline->backend->vtable.pipeline_state_deallocate(pipeline->backend, pipeline, state);
}

void
render_pipeline_use_buffer(render_pipeline_t* pipeline, render_buffer_index_t buffer) {
	pipeline->backend->vtable.pipeline_use_buffer(pipeline->backend, pipeline, buffer);
}
