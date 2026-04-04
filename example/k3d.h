/*
   KallistiOS 2.0.0

   k3d.h
   K3D Format Specification v1.0
   
   Binary 3D model format optimized for Dreamcast/KGL rendering
   with indexed geometry, normals, and UV coordinates.
*/

#ifndef __K3D_H
#define __K3D_H

#include <stdint.h>
#include <KGL/gl.h>

/* K3D Format Magic Number */
#define K3D_MAGIC "K3D"
#define K3D_VERSION 1

/* Primitive Types */
#define K3D_PRIMITIVE_TRIANGLES  0  /* GL_TRIANGLES - 3 indices per primitive */
#define K3D_PRIMITIVE_QUADS      1  /* GL_QUADS - 4 indices per primitive */

/* Feature Flags */
#define K3D_HAS_NORMALS  0x0001  /* Mesh includes normal data */
#define K3D_HAS_UVS      0x0002  /* Mesh includes texture coordinates */
#define K3D_HAS_COLORS   0x0004  /* Mesh includes vertex colors (reserved) */

/* Animation Sidecar Constants */
#define K3D_MAX_BONE_INFLUENCES 4
#define K3D_INVALID_BONE_INDEX  0xff
#define K3SK_MAGIC "K3SK"
#define K3SA_MAGIC "K3SA"
#define K3VA_MAGIC "K3VA"
#define K3_ANIM_VERSION 1

/* K3D File Header (16 bytes) */
typedef struct {
    char magic[4];           /* "K3D\0" magic identifier */
    uint16_t version;        /* Format version (currently 1) */
    uint16_t flags;          /* Feature flags (bitfield) */
    uint8_t primitiveType;   /* Primitive type (K3D_PRIMITIVE_*) */
    uint8_t reserved;        /* Reserved for alignment */
    uint32_t vertexCount;    /* Number of unique vertices */
    uint32_t indexCount;     /* Number of indices */
} __attribute__((packed)) K3DHeader;

/* K3D Mesh Structure (in-memory representation) */
typedef struct {
    uint32_t vertexCount;    /* Number of vertices */
    uint32_t indexCount;     /* Number of indices */
    uint16_t flags;          /* Feature flags from header */
    uint8_t primitiveType;   /* Primitive type (K3D_PRIMITIVE_*) */
    
    GLfloat *vertices;       /* Vertex positions [vertexCount * 3] (x,y,z) */
    GLfloat *normals;        /* Vertex normals [vertexCount * 3] (nx,ny,nz) */
    GLfloat *texCoords;      /* Texture coords [vertexCount * 2] (u,v) */
    uint16_t *indices;       /* Primitive indices [indexCount] */
} K3DMesh;

typedef struct {
    int16_t parentIndex;     /* Parent bone index or -1 for root */
    int16_t reserved;        /* Reserved for alignment */
    GLfloat inverseBind[16]; /* Inverse bind matrix in mesh local space */
} K3DBone;

typedef struct {
    uint8_t boneIndex[K3D_MAX_BONE_INFLUENCES];
    uint8_t boneWeight[K3D_MAX_BONE_INFLUENCES];
} K3DVertexInfluence;

typedef struct {
    uint16_t boneCount;
    uint16_t reserved;
    uint32_t vertexCount;
    K3DBone *bones;
    K3DVertexInfluence *influences;
} K3DSkeleton;

typedef struct {
    GLfloat translation[3];
    GLfloat rotation[4];
    GLfloat scale[3];
} K3DTransform;

typedef struct {
    uint16_t boneCount;
    uint16_t frameCount;
    uint16_t fps;
    uint16_t reserved;
    K3DTransform *frames;    /* [frameCount * boneCount] */
} K3DSkeletalAnimation;

typedef struct {
    uint32_t vertexCount;
    uint16_t frameCount;
    uint16_t fps;
    GLfloat *deltas;         /* [vertexCount * 3] */
    GLfloat *weights;        /* [frameCount] */
} K3DVertexAnimation;

/* K3D Loading Functions */

/*
 * Load a K3D model from file
 * Returns: Pointer to K3DMesh on success, NULL on failure
 * Note: Caller must free the mesh with k3d_free() when done
 */
K3DMesh *k3d_load(const char *filename);

/*
 * Load a K3D model from memory buffer
 * Returns: Pointer to K3DMesh on success, NULL on failure
 * Note: Caller must free the mesh with k3d_free() when done
 */
K3DMesh *k3d_load_memory(const void *data, size_t size);

/* Load a skeleton sidecar from file. */
K3DSkeleton *k3d_skeleton_load(const char *filename);

/* Load a skeletal animation sidecar from file. */
K3DSkeletalAnimation *k3d_skeletal_animation_load(const char *filename);

/* Load a shapekey animation sidecar from file. */
K3DVertexAnimation *k3d_vertex_animation_load(const char *filename);

/*
 * Free a K3D mesh and all associated data
 */
void k3d_free(K3DMesh *mesh);

/* Free animation sidecars. */
void k3d_skeleton_free(K3DSkeleton *skeleton);
void k3d_skeletal_animation_free(K3DSkeletalAnimation *animation);
void k3d_vertex_animation_free(K3DVertexAnimation *animation);

/*
 * Render a K3D mesh using OpenGL vertex arrays
 * Note: Texture must be bound before calling this function
 */
void k3d_render(const K3DMesh *mesh);

/* Render a K3D mesh using optional override vertex and normal buffers. */
void k3d_render_override(const K3DMesh *mesh, const GLfloat *vertices,
                         const GLfloat *normals);

#endif /* __K3D_H */
