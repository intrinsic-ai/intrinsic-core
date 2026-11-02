// This file is part of meshoptimizer library; see LICENSE for version/license details
#ifndef THIRD_PARTY_MESHOPTIMIZER_SIMPLIFIER_H_
#define THIRD_PARTY_MESHOPTIMIZER_SIMPLIFIER_H_

#include <stddef.h>

#include "meshoptimizer.h"

namespace intrinsic_thirdparty {

/**
 * Experimental: Mesh simplifier with custom collapse callback
 *
 * Similar to meshopt_simplify, but invokes callback(context, v0, v1, triangles, triangle_count)
 * before committing each edge collapse v0 -> v1.
 * triangles contains triangle_count * 3 vertex indices for the post-collapse 1-ring triangles around v1.
 * callback can be NULL; otherwise, it should return 1 to allow the collapse and 0 to reject it.
 */
size_t meshopt_simplifyCustom(
    unsigned int* destination, const unsigned int* indices, size_t index_count,
    const float* vertex_positions, size_t vertex_count,
    size_t vertex_positions_stride, size_t target_index_count,
    float target_error, unsigned int options, float* result_error,
    int (*callback)(void* context, unsigned int v0, unsigned int v1,
                    const unsigned int* triangles, size_t triangle_count),
    void* context);

template <typename T, typename F>
inline size_t meshopt_simplifyCustom(
    T* destination, const T* indices, size_t index_count,
    const float* vertex_positions, size_t vertex_count,
    size_t vertex_positions_stride, size_t target_index_count,
    float target_error, unsigned int options, float* result_error,
    F callback)
{
	struct Call
	{
		static int accept(void* context, unsigned int v0, unsigned int v1,
                                  const unsigned int* triangles, size_t triangle_count)
		{
			return (*static_cast<F*>(context))(v0, v1, triangles, triangle_count) ? 1 : 0;
		}
	};

	meshopt_IndexAdapter<T> in(NULL, indices, index_count);
	meshopt_IndexAdapter<T> out(destination, NULL, index_count);

	return meshopt_simplifyCustom(
            out.data, in.data, index_count, vertex_positions, vertex_count,
            vertex_positions_stride, target_index_count, target_error, options,
            result_error, &Call::accept, &callback);
}

}  // namespace intrinsic_thirdparty

#endif  // THIRD_PARTY_MESHOPTIMIZER_SIMPLIFIER_H_
