#include "mesh.h"

#include <core/platform_layer.h>

#include <cgltf.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <float.h>

#define cgltf_call(call) do { cgltf_result _result = (call); assert(_result == cgltf_result_success); } while(0)

internal RHI_DescriptorHeap* s_image_heap;
internal RHI_DescriptorHeap* s_sampler_heap;
internal RHI_DescriptorSetLayout s_descriptor_set_layout;
internal RHI_DescriptorSetLayout s_meshlet_set_layout;

typedef struct aabb aabb;
struct aabb
{
    hmm_vec3 min;
    hmm_vec3 max;
};

typedef struct meshlet_vector meshlet_vector;
struct meshlet_vector
{
    Meshlet* meshlets;
    u32 used;
    u32 size;
};

void init_meshlet_vector(meshlet_vector* vec, u32 start_size)
{
    vec->meshlets = calloc(start_size, sizeof(Meshlet));
    vec->size = start_size;
    vec->used = 0;
}

void free_meshlet_vector(meshlet_vector* vec)
{
    free(vec->meshlets);
}

void push_meshlet(meshlet_vector* vec, Meshlet m)
{
    if (vec->used >= vec->size)
    {
        vec->size *= 2;
        vec->meshlets = realloc(vec->meshlets, vec->size * sizeof(Meshlet));
    }
    vec->meshlets[vec->used++] = m;
}

void mesh_loader_init(i32 dset_layout_binding)
{
    s_descriptor_set_layout.descriptors[0] = DESCRIPTOR_BUFFER;
    s_descriptor_set_layout.descriptor_count = 1;
    rhi_init_descriptor_set_layout(&s_descriptor_set_layout);

    s_meshlet_set_layout.descriptors[0] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    s_meshlet_set_layout.descriptors[1] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    s_meshlet_set_layout.descriptor_count = 2;
    rhi_init_descriptor_set_layout(&s_meshlet_set_layout);
}

void mesh_loader_free()
{
    rhi_free_descriptor_set_layout(&s_meshlet_set_layout);
    rhi_free_descriptor_set_layout(&s_descriptor_set_layout);
}

RHI_DescriptorSetLayout* mesh_loader_get_descriptor_set_layout()
{
    return &s_descriptor_set_layout;
}

RHI_DescriptorSetLayout* mesh_loader_get_geometry_descriptor_set_layout()
{
    return &s_meshlet_set_layout;
}

void mesh_loader_set_texture_heap(RHI_DescriptorHeap* heap)
{
    s_image_heap = heap;
}

u32 cgltf_comp_size(cgltf_component_type type)
{
    switch (type)
    {
    case cgltf_component_type_r_8:
    case cgltf_component_type_r_8u:
        return 1;
    case cgltf_component_type_r_16:
    case cgltf_component_type_r_16u:
        return 2;
    case cgltf_component_type_r_32u:
    case cgltf_component_type_r_32f:
        return 4;
    }

    assert(0);
    return 0;
}

u32 cgltf_comp_count(cgltf_type type)
{
    switch (type)
    {
    case cgltf_type_scalar:
        return 1;
    case cgltf_type_vec2:
        return 2;
    case cgltf_type_vec3:
        return 3;
    case cgltf_type_vec4:
    case cgltf_type_mat2:
        return 4;
    case cgltf_type_mat3:
        return 9;
    case cgltf_type_mat4:
        return 16;
    }

    assert(0);
    return 0;
}

void* cgltf_get_accessor_data(cgltf_accessor* accessor, u32* component_size, u32* component_count)
{
    *component_size = cgltf_comp_size(accessor->component_type);
    *component_count = cgltf_comp_count(accessor->type);

    cgltf_buffer_view* view = accessor->buffer_view;
    return OFFSET_PTR_BYTES(void, view->buffer->data, view->offset);
}

void mesh_load_albedo(Thread* thread)
{
    GLTFMaterial* mat = (GLTFMaterial*)aurora_platform_get_thread_ptr(thread);

    rhi_load_raw_image(&mat->raw_color, mat->albedo_path);
}

void mesh_load_normal(Thread* thread)
{
    GLTFMaterial* mat = (GLTFMaterial*)aurora_platform_get_thread_ptr(thread);

    rhi_load_raw_image(&mat->raw_normal, mat->normal_path);
}

void mesh_load_pbr(Thread* thread)
{
    GLTFMaterial* mat = (GLTFMaterial*)aurora_platform_get_thread_ptr(thread);

    rhi_load_raw_image(&mat->raw_pbr, mat->mr_path);
}

void cgltf_process_primitive(cgltf_primitive* cgltf_primitive, u32* primitive_index, Mesh* m, hmm_mat4 transform)
{
    Primitive* pri = &m->primitives[(*primitive_index)++];
    pri->transform = transform;

    if (cgltf_primitive->type != cgltf_primitive_type_triangles)
        return;

    cgltf_attribute* position_attribute = 0;
    cgltf_attribute* texcoord_attribute = 0;
    cgltf_attribute* normal_attribute = 0;

    for (i32 attribute_index = 0; attribute_index < cgltf_primitive->attributes_count; attribute_index++)
    {
        cgltf_attribute* attribute = &cgltf_primitive->attributes[attribute_index];

        if (strcmp(attribute->name, "POSITION") == 0) position_attribute = attribute;
        if (strcmp(attribute->name, "TEXCOORD_0") == 0) texcoord_attribute = attribute;
        if (strcmp(attribute->name, "NORMAL") == 0) normal_attribute = attribute;
    }

    assert(position_attribute && texcoord_attribute && normal_attribute);

    u32 vertex_count = (u32)normal_attribute->data->count;
    u64 vertices_size = vertex_count * sizeof(Vertex);
    Vertex* vertices = (Vertex*)malloc(vertices_size);
    memset(vertices, 0, sizeof(vertices));

    {
        u32 component_size, component_count;
        f32* src = (f32*)cgltf_get_accessor_data(position_attribute->data, &component_size, &component_count);
        assert(component_size == 4);

        if (src)
        {
            for (u32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
            {
                vertices[vertex_index].position.X = src[vertex_index * component_count + 0];
                vertices[vertex_index].position.Y = src[vertex_index * component_count + 1];
                vertices[vertex_index].position.Z = src[vertex_index * component_count + 2];
            }
        }
    }

    {
        u32 component_size, component_count;
        f32* src = (f32*)cgltf_get_accessor_data(texcoord_attribute->data, &component_size, &component_count);
        assert(component_size == 4);

        if (src)
        {
            for (u32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
            {
                vertices[vertex_index].uv.X = src[vertex_index * component_count + 0];
                vertices[vertex_index].uv.Y = src[vertex_index * component_count + 1];
            }
        }
    }

    {
        u32 component_size, component_count;
        f32* src = (f32*)cgltf_get_accessor_data(normal_attribute->data, &component_size, &component_count);
        assert(component_size == 4);

        if (src)
        {
            for (u32 vertex_index = 0; vertex_index < vertex_count; vertex_index++)
            {
                vertices[vertex_index].normals.X = src[vertex_index * component_count + 0];
                vertices[vertex_index].normals.Y = src[vertex_index * component_count + 1];
                vertices[vertex_index].normals.Z = src[vertex_index * component_count + 2];
            }
        }
    }

    pri->index_count = (u32)cgltf_primitive->indices->count;
    u32 index_size = pri->index_count * sizeof(u32);
    u32* indices = (u32*)malloc(index_size);
    memset(indices, 0, index_size);

    {
        if (cgltf_primitive->indices != NULL)
        {
            for (u32 k = 0; k < (u32)cgltf_primitive->indices->count; k++)
                indices[k] = (u32)(cgltf_accessor_read_index(cgltf_primitive->indices, k));
        }
    }

    rhi_allocate_buffer(&pri->vertex_buffer, vertices_size, BUFFER_VERTEX);
    rhi_upload_buffer(&pri->vertex_buffer, vertices, vertices_size);

    rhi_allocate_buffer(&pri->index_buffer, index_size, BUFFER_INDEX);
    rhi_upload_buffer(&pri->index_buffer, indices, index_size);

    // MAKE MESHLETS

    meshlet_vector vec;
    init_meshlet_vector(&vec, 256);

    u8* meshlet_vertices = (u8*)malloc(sizeof(u8) * vertex_count);
    memset(meshlet_vertices, 0xff, sizeof(u8) * vertex_count);

    Meshlet ml;
    memset(&ml, 0, sizeof(ml));

    for (i64 i = 0; i < pri->index_count; i += 3)
    {
        u32 a = indices[i + 0];
        u32 b = indices[i + 1];
        u32 c = indices[i + 2];

        u8 av = meshlet_vertices[a];
        u8 bv = meshlet_vertices[b];
        u8 cv = meshlet_vertices[c];

        u32 used_extra = (av == 0xff) + (bv == 0xff) + (cv == 0xff);

        if (ml.vertex_count + used_extra > MAX_MESHLET_VERTICES || ml.triangle_count >= MAX_MESHLET_TRIANGLES)
        {
            push_meshlet(&vec, ml);
            
            for (size_t j = 0; j < ml.vertex_count; ++j)
                meshlet_vertices[ml.vertices[j]] = 0xff;

            memset(&ml, 0, sizeof(ml));
        }

        if (av == 0xff)
        {
            av = ml.vertex_count;
            ml.vertices[ml.vertex_count++] = a;
        }

        if (bv == 0xff)
        {
            bv = ml.vertex_count;
            ml.vertices[ml.vertex_count++] = b;
        }

        if (cv == 0xff)
        {
            cv = ml.vertex_count;
            ml.vertices[ml.vertex_count++] = c;
        }

        ml.indices[ml.triangle_count * 3 + 0] = av;
        ml.indices[ml.triangle_count * 3 + 1] = bv;
        ml.indices[ml.triangle_count * 3 + 2] = cv;
        ml.triangle_count++;
    }

    if (ml.triangle_count)
        push_meshlet(&vec, ml);

    // Bounding Sphere

    for (u32 i = 0; i < vec.used; i++)
    {
        aabb bbox;
        memset(&bbox, 0, sizeof(aabb));

        bbox.min = HMM_Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
        bbox.max = HMM_Vec3(FLT_MIN, FLT_MIN, FLT_MIN);

        for (u32 j = 0; j < vec.meshlets[i].vertex_count; ++j)
        {
            u32 a = vec.meshlets[i].indices[j];
            const Vertex* va = &vertices[vec.meshlets[i].vertices[a]];

            bbox.min.X = min(bbox.min.X, va->position.X);
            bbox.min.Y = min(bbox.min.Y, va->position.Y);
            bbox.min.Z = min(bbox.min.Z, va->position.Z);

            bbox.max.X = max(bbox.max.X, va->position.X);
            bbox.max.Y = max(bbox.max.Y, va->position.Y);
            bbox.max.Z = max(bbox.max.Z, va->position.Z);
        }

        hmm_vec3 bbox_extent = HMM_MultiplyVec3f(HMM_SubtractVec3(bbox.max, bbox.min), 0.5f);
        hmm_vec3 bbox_center = HMM_AddVec3(bbox.min, bbox_extent);

        vec.meshlets[i].sphere.XYZ = bbox_center;

        for (u32 j = 0; j < vec.meshlets[i].vertex_count; ++j)
        {
            u32 a = vec.meshlets[i].indices[j];
            const Vertex* va = &vertices[vec.meshlets[i].vertices[a]];

            vec.meshlets[i].sphere.W = max(vec.meshlets[i].sphere.W, HMM_DistanceVec3(vec.meshlets[i].sphere.XYZ, va->position));
        }
    }

    rhi_allocate_buffer(&pri->meshlet_buffer, vec.used * sizeof(Meshlet), BUFFER_VERTEX);
    rhi_upload_buffer(&pri->meshlet_buffer, vec.meshlets, vec.used * sizeof(Meshlet));

    rhi_init_descriptor_set(&pri->geometry_descriptor_set, &s_meshlet_set_layout);
    rhi_descriptor_set_write_storage_buffer(&pri->geometry_descriptor_set, &pri->vertex_buffer, vertices_size, 0);
    rhi_descriptor_set_write_storage_buffer(&pri->geometry_descriptor_set, &pri->meshlet_buffer, vec.used * sizeof(Meshlet), 1);

    typedef struct temp_mat
    {
        i32 albedo_idx;
        i32 normal_idx;
        i32 mr_idx;
        i32 sampler_idx;
        hmm_vec3 bc_factor;
        f32 m_factor;
        f32 r_factor;
        hmm_vec3 pad;
    } temp_mat;

    // Load textures
    {
        if (cgltf_primitive->material)
        {
            Thread* albedo_thread = aurora_platform_new_thread(mesh_load_albedo);
            Thread* normal_thread = aurora_platform_new_thread(mesh_load_normal);
            Thread* pbr_thread = aurora_platform_new_thread(mesh_load_pbr);

            pri->material_index = m->material_count;

            aurora_platform_set_thread_ptr(albedo_thread, &m->materials[pri->material_index]);
            aurora_platform_set_thread_ptr(normal_thread, &m->materials[pri->material_index]);
            aurora_platform_set_thread_ptr(pbr_thread, &m->materials[pri->material_index]);

            sprintf(m->materials[pri->material_index].albedo_path, "%s%s", m->directory, cgltf_primitive->material->pbr_metallic_roughness.base_color_texture.texture->image->uri);

            if (cgltf_primitive->material->normal_texture.texture) 
            {
                m->materials[pri->material_index].has_normal = 1;
                sprintf(m->materials[pri->material_index].normal_path, "%s%s", m->directory, cgltf_primitive->material->normal_texture.texture->image->uri);
            }
            
            if (cgltf_primitive->material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                m->materials[pri->material_index].has_metallic = 1;
                sprintf(m->materials[pri->material_index].mr_path, "%s%s", m->directory, cgltf_primitive->material->pbr_metallic_roughness.metallic_roughness_texture.texture->image->uri);
            }

            if (MULTITHREADING_ENABLED)
            {
                aurora_platform_execute_thread(albedo_thread);
                if (m->materials[pri->material_index].has_normal) aurora_platform_execute_thread(normal_thread);
                if (m->materials[pri->material_index].has_metallic) aurora_platform_execute_thread(pbr_thread);

                aurora_platform_join_thread(albedo_thread);
                if (m->materials[pri->material_index].has_normal) aurora_platform_join_thread(normal_thread);
                if (m->materials[pri->material_index].has_metallic) aurora_platform_join_thread(pbr_thread);
            }
            else
            {
                rhi_load_raw_image(&m->materials[pri->material_index].raw_color,  m->materials[pri->material_index].albedo_path);
                if (m->materials[pri->material_index].has_normal) rhi_load_raw_image(&m->materials[pri->material_index].raw_normal, m->materials[pri->material_index].normal_path);
                if (m->materials[pri->material_index].has_metallic) rhi_load_raw_image(&m->materials[pri->material_index].raw_pbr,    m->materials[pri->material_index].mr_path);
            }

            aurora_platform_free_thread(albedo_thread);
            aurora_platform_free_thread(normal_thread);
            aurora_platform_free_thread(pbr_thread);

            rhi_upload_image(&m->materials[pri->material_index].albedo, &m->materials[pri->material_index].raw_color, 1);
            rhi_free_raw_image(&m->materials[pri->material_index].raw_color);
            m->materials[pri->material_index].albedo_bindless_index = rhi_find_available_descriptor(s_image_heap);
            rhi_push_descriptor_heap_image(s_image_heap, &m->materials[pri->material_index].albedo, m->materials[pri->material_index].albedo_bindless_index);

            m->materials[pri->material_index].albedo_sampler.filter = VK_FILTER_LINEAR;
            m->materials[pri->material_index].albedo_sampler.address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;

            rhi_init_sampler(&m->materials[pri->material_index].albedo_sampler, m->materials[pri->material_index].albedo.mip_levels);
            m->materials[pri->material_index].albedo_sampler_index = rhi_find_available_descriptor(s_sampler_heap);
            rhi_push_descriptor_heap_sampler(s_sampler_heap, &m->materials[pri->material_index].albedo_sampler, m->materials[pri->material_index].albedo_sampler_index);
            
            m->materials[pri->material_index].base_color_factor.X = cgltf_primitive->material->pbr_metallic_roughness.base_color_factor[0];
            m->materials[pri->material_index].base_color_factor.Y = cgltf_primitive->material->pbr_metallic_roughness.base_color_factor[1];
            m->materials[pri->material_index].base_color_factor.Z = cgltf_primitive->material->pbr_metallic_roughness.base_color_factor[2];

            if (cgltf_primitive->material->normal_texture.texture)
            {     
                rhi_upload_image(&m->materials[pri->material_index].normal, &m->materials[pri->material_index].raw_normal, 0);
                rhi_free_raw_image(&m->materials[pri->material_index].raw_normal);
                m->materials[pri->material_index].normal_bindless_index = rhi_find_available_descriptor(s_image_heap);
                rhi_push_descriptor_heap_image(s_image_heap, &m->materials[pri->material_index].normal, m->materials[pri->material_index].normal_bindless_index);
            }

            
            if (cgltf_primitive->material->pbr_metallic_roughness.metallic_roughness_texture.texture)
            {
                rhi_upload_image(&m->materials[pri->material_index].metallic_roughness, &m->materials[pri->material_index].raw_pbr, 0);
                rhi_free_raw_image(&m->materials[pri->material_index].raw_pbr);

                m->materials[pri->material_index].metallic_roughness_index = rhi_find_available_descriptor(s_image_heap);
                rhi_push_descriptor_heap_image(s_image_heap, &m->materials[pri->material_index].metallic_roughness, m->materials[pri->material_index].metallic_roughness_index);
            
                m->materials[pri->material_index].metallic_factor = cgltf_primitive->material->pbr_metallic_roughness.metallic_factor;
                m->materials[pri->material_index].roughness_factor = cgltf_primitive->material->pbr_metallic_roughness.roughness_factor;
            }
            
        
            temp_mat temp;
            temp.albedo_idx = m->materials[pri->material_index].albedo_bindless_index;
            temp.sampler_idx = m->materials[pri->material_index].albedo_sampler_index;
            temp.normal_idx = m->materials[pri->material_index].normal_bindless_index;
            temp.mr_idx = m->materials[pri->material_index].metallic_roughness_index;
            temp.bc_factor = m->materials[pri->material_index].base_color_factor;
            temp.m_factor = m->materials[pri->material_index].metallic_factor;
            temp.r_factor = m->materials[pri->material_index].roughness_factor;

            rhi_allocate_buffer(&m->materials[pri->material_index].material_buffer, sizeof(temp_mat), BUFFER_UNIFORM);
            rhi_upload_buffer(&m->materials[pri->material_index].material_buffer, &temp, sizeof(temp_mat));

            rhi_init_descriptor_set(&m->materials[pri->material_index].material_set, &s_descriptor_set_layout);
            rhi_descriptor_set_write_buffer(&m->materials[pri->material_index].material_set, &m->materials[pri->material_index].material_buffer, sizeof(temp_mat), 0);

            m->material_count++;
        }
    }

    pri->vertex_count = vertex_count;
    pri->triangle_count = pri->index_count / 3;
    pri->vertex_size = vertices_size;
    pri->index_size = index_size;
    pri->meshlet_count = vec.used;

    m->total_vertex_count += pri->vertex_count;
    m->total_index_count += pri->index_count;
    m->total_triangle_count += pri->triangle_count;

    free_meshlet_vector(&vec);
    free(indices);
    free(vertices);
}

void cgltf_process_node(cgltf_node* node, u32* primitive_index, Mesh* m)
{
    if (node->mesh)
    {
        hmm_mat4 pri_transform = HMM_Mat4d(1.0f);

        if (node->has_translation)
        {
            hmm_vec3 translation = HMM_Vec3(node->translation[0], node->translation[1], node->translation[2]);
            pri_transform = HMM_MultiplyMat4(pri_transform, HMM_Translate(translation));
        }
        if (node->has_rotation)
        {
            hmm_quaternion rotation = HMM_Quaternion(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
            pri_transform = HMM_MultiplyMat4(pri_transform, HMM_QuaternionToMat4(rotation));
        }
        if (node->has_scale)
        {
            hmm_vec3 scale = HMM_Vec3(node->scale[0], node->scale[1], node->scale[2]);
            pri_transform = HMM_MultiplyMat4(pri_transform, HMM_Scale(scale));
        }

        pri_transform = HMM_MultiplyMat4(pri_transform, HMM_Rotate(180.0f, HMM_Vec3(0.0f, 1.0f, 0.0f))); // Flip y-axis

        for (i32 p = 0; p < node->mesh->primitives_count; p++)
        {
            cgltf_process_primitive(&node->mesh->primitives[p], primitive_index, m, pri_transform);
            m->primitive_count++;
        }
    }

    for (i32 c = 0; c < node->children_count; c++)
        cgltf_process_node(node->children[c], primitive_index, m);
}

void mesh_load(Mesh* out, const char* path)
{
    memset(out, 0, sizeof(Mesh));

    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data* data = 0;

    cgltf_call(cgltf_parse_file(&options, path, &data));
    cgltf_call(cgltf_load_buffers(&options, data, path));
    cgltf_scene* scene = data->scene;
    
    const char* ch = "/";
    char* ptr = strstr(path, ch);
    ptr += sizeof(char);
    strncpy(ptr, "", strlen(ptr));
    out->directory = (char*)path;

    u32 pi = 0;
    for (i32 ni = 0; ni < scene->nodes_count; ni++)
        cgltf_process_node(scene->nodes[ni], &pi, out);

    cgltf_free(data);
}

void mesh_free(Mesh* m)
{
    for (i32 i = 0; i < m->primitive_count; i++)
    {
        rhi_free_buffer(&m->primitives[i].meshlet_buffer);
        rhi_free_buffer(&m->primitives[i].index_buffer);
        rhi_free_buffer(&m->primitives[i].vertex_buffer);
        rhi_free_descriptor_set(&m->primitives[i].geometry_descriptor_set);
    }

    for (i32 i = 0; i < m->material_count; i++)
    {
        if (m->materials[i].albedo.image != VK_NULL_HANDLE)
        {
            rhi_free_image(&m->materials[i].albedo);
            rhi_free_sampler(&m->materials[i].albedo_sampler);
        }
        if (m->materials[i].normal.image != VK_NULL_HANDLE)
            rhi_free_image(&m->materials[i].normal);
        if (m->materials[i].metallic_roughness.image != VK_NULL_HANDLE)
            rhi_free_image(&m->materials[i].metallic_roughness);
        rhi_free_buffer(&m->materials[i].material_buffer);
        rhi_free_descriptor_set(&m->materials[i].material_set);
    }
}

void mesh_loader_set_sampler_heap(RHI_DescriptorHeap* heap)
{
    s_sampler_heap = heap;
}