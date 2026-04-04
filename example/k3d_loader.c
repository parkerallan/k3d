/*
   KallistiOS 2.0.0

   k3d_loader.c
   K3D Binary Model Loader
   
   Loads K3D binary 3D models with indexed geometry for optimized rendering.
*/

#include "k3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t boneCount;
    uint32_t vertexCount;
} __attribute__((packed)) K3DSkeletonHeader;

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t boneCount;
    uint16_t frameCount;
    uint16_t fps;
} __attribute__((packed)) K3DSkeletalAnimationHeader;

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t frameCount;
    uint16_t fps;
    uint32_t vertexCount;
} __attribute__((packed)) K3DVertexAnimationHeader;

static int k3d_read_exact(FILE *file, void *buffer, size_t size,
                          const char *label) {
    if(fread(buffer, 1, size, file) != size) {
        printf("K3D Error: Could not read %s\n", label);
        return 0;
    }

    return 1;
}

static int k3d_validate_sidecar_magic(const char magic[4], const char *expected,
                                      const char *label) {
    if(memcmp(magic, expected, 4) != 0) {
        printf("K3D Error: Invalid %s magic\n", label);
        return 0;
    }

    return 1;
}

static GLenum k3d_get_primitive_mode(uint8_t primitiveType) {
    switch(primitiveType) {
        case K3D_PRIMITIVE_QUADS:
            return GL_QUADS;
        case K3D_PRIMITIVE_TRIANGLES:
        default:
            return GL_TRIANGLES;
    }
}

/* Helper function to validate K3D header */
static int k3d_validate_header(const K3DHeader *header) {
    if(memcmp(header->magic, K3D_MAGIC, 3) != 0) {
        printf("K3D Error: Invalid magic number\n");
        return 0;
    }
    
    if(header->magic[3] != '\0') {
        printf("K3D Error: Invalid magic terminator\n");
        return 0;
    }
    
    if(header->version != K3D_VERSION) {
        printf("K3D Error: Unsupported version %d (expected %d)\n", 
               header->version, K3D_VERSION);
        return 0;
    }
    
    if(header->vertexCount == 0 || header->indexCount == 0) {
        printf("K3D Error: Invalid vertex/index count\n");
        return 0;
    }
    
    if(header->indexCount % 3 != 0) {
        printf("K3D Warning: Index count not divisible by 3 (triangles)\n");
    }
    
    return 1;
}

/* Load K3D model from file */
K3DMesh *k3d_load(const char *filename) {
    FILE *file;
    K3DHeader header;
    K3DMesh *mesh = NULL;
    size_t read_count;
    
    /* Open file */
    file = fopen(filename, "rb");
    if(!file) {
        printf("K3D Error: Could not open file: %s\n", filename);
        return NULL;
    }
    
    /* Read header */
    read_count = fread(&header, sizeof(K3DHeader), 1, file);
    if(read_count != 1) {
        printf("K3D Error: Could not read header\n");
        fclose(file);
        return NULL;
    }
    
    /* Validate header */
    if(!k3d_validate_header(&header)) {
        fclose(file);
        return NULL;
    }
    
    /* Allocate mesh structure */
    mesh = (K3DMesh *)malloc(sizeof(K3DMesh));
    if(!mesh) {
        printf("K3D Error: Could not allocate mesh structure\n");
        fclose(file);
        return NULL;
    }
    
    /* Initialize mesh */
    memset(mesh, 0, sizeof(K3DMesh));
    mesh->vertexCount = header.vertexCount;
    mesh->indexCount = header.indexCount;
    mesh->flags = header.flags;
    mesh->primitiveType = header.primitiveType;
    
    /* Allocate vertex positions (always present) */
    mesh->vertices = (GLfloat *)malloc(header.vertexCount * 3 * sizeof(GLfloat));
    if(!mesh->vertices) {
        printf("K3D Error: Could not allocate vertex data\n");
        k3d_free(mesh);
        fclose(file);
        return NULL;
    }
    
    /* Read vertex positions */
    read_count = fread(mesh->vertices, sizeof(GLfloat), header.vertexCount * 3, file);
    if(read_count != header.vertexCount * 3) {
        printf("K3D Error: Could not read vertex data\n");
        k3d_free(mesh);
        fclose(file);
        return NULL;
    }
    
    /* Allocate and read normals if present */
    if(header.flags & K3D_HAS_NORMALS) {
        mesh->normals = (GLfloat *)malloc(header.vertexCount * 3 * sizeof(GLfloat));
        if(!mesh->normals) {
            printf("K3D Error: Could not allocate normal data\n");
            k3d_free(mesh);
            fclose(file);
            return NULL;
        }
        
        read_count = fread(mesh->normals, sizeof(GLfloat), header.vertexCount * 3, file);
        if(read_count != header.vertexCount * 3) {
            printf("K3D Error: Could not read normal data\n");
            k3d_free(mesh);
            fclose(file);
            return NULL;
        }
    }
    
    /* Allocate and read UVs if present */
    if(header.flags & K3D_HAS_UVS) {
        mesh->texCoords = (GLfloat *)malloc(header.vertexCount * 2 * sizeof(GLfloat));
        if(!mesh->texCoords) {
            printf("K3D Error: Could not allocate UV data\n");
            k3d_free(mesh);
            fclose(file);
            return NULL;
        }
        
        read_count = fread(mesh->texCoords, sizeof(GLfloat), header.vertexCount * 2, file);
        if(read_count != header.vertexCount * 2) {
            printf("K3D Error: Could not read UV data\n");
            k3d_free(mesh);
            fclose(file);
            return NULL;
        }
    }
    
    /* Allocate indices (always present) */
    mesh->indices = (uint16_t *)malloc(header.indexCount * sizeof(uint16_t));
    if(!mesh->indices) {
        printf("K3D Error: Could not allocate index data\n");
        k3d_free(mesh);
        fclose(file);
        return NULL;
    }
    
    /* Read indices */
    read_count = fread(mesh->indices, sizeof(uint16_t), header.indexCount, file);
    if(read_count != header.indexCount) {
        printf("K3D Error: Could not read index data\n");
        k3d_free(mesh);
        fclose(file);
        return NULL;
    }
    
    fclose(file);
    
    const char *primType = (mesh->primitiveType == K3D_PRIMITIVE_QUADS) ? "quads" : "triangles";
    uint32_t primCount = (mesh->primitiveType == K3D_PRIMITIVE_QUADS) ? 
                         (mesh->indexCount / 4) : (mesh->indexCount / 3);
    printf("K3D: Loaded %s - %" PRIu32 " vertices, %" PRIu32 " indices (%" PRIu32 " %s)\n", 
           filename, mesh->vertexCount, mesh->indexCount, primCount, primType);
    
    return mesh;
}

/* Load K3D model from memory buffer */
K3DMesh *k3d_load_memory(const void *data, size_t size) {
    const uint8_t *ptr = (const uint8_t *)data;
    K3DHeader header;
    K3DMesh *mesh = NULL;
    size_t offset = 0;
    size_t required_size = sizeof(K3DHeader);
    
    /* Check minimum size */
    if(size < required_size) {
        printf("K3D Error: Buffer too small for header\n");
        return NULL;
    }
    
    /* Copy header */
    memcpy(&header, ptr, sizeof(K3DHeader));
    offset += sizeof(K3DHeader);
    
    /* Validate header */
    if(!k3d_validate_header(&header)) {
        return NULL;
    }
    
    /* Calculate required buffer size */
    required_size += header.vertexCount * 3 * sizeof(GLfloat);  /* Vertices */
    if(header.flags & K3D_HAS_NORMALS)
        required_size += header.vertexCount * 3 * sizeof(GLfloat);  /* Normals */
    if(header.flags & K3D_HAS_UVS)
        required_size += header.vertexCount * 2 * sizeof(GLfloat);  /* UVs */
    required_size += header.indexCount * sizeof(uint16_t);  /* Indices */
    
    if(size < required_size) {
        printf("K3D Error: Buffer too small (need %zu, have %zu)\n", required_size, size);
        return NULL;
    }
    
    /* Allocate mesh structure */
    mesh = (K3DMesh *)malloc(sizeof(K3DMesh));
    if(!mesh) {
        printf("K3D Error: Could not allocate mesh structure\n");
        return NULL;
    }
    
    /* Initialize mesh */
    memset(mesh, 0, sizeof(K3DMesh));
    mesh->vertexCount = header.vertexCount;
    mesh->indexCount = header.indexCount;
    mesh->flags = header.flags;
    mesh->primitiveType = header.primitiveType;
    
    /* Allocate and copy vertex positions */
    mesh->vertices = (GLfloat *)malloc(header.vertexCount * 3 * sizeof(GLfloat));
    if(!mesh->vertices) {
        k3d_free(mesh);
        return NULL;
    }
    memcpy(mesh->vertices, ptr + offset, header.vertexCount * 3 * sizeof(GLfloat));
    offset += header.vertexCount * 3 * sizeof(GLfloat);
    
    /* Allocate and copy normals if present */
    if(header.flags & K3D_HAS_NORMALS) {
        mesh->normals = (GLfloat *)malloc(header.vertexCount * 3 * sizeof(GLfloat));
        if(!mesh->normals) {
            k3d_free(mesh);
            return NULL;
        }
        memcpy(mesh->normals, ptr + offset, header.vertexCount * 3 * sizeof(GLfloat));
        offset += header.vertexCount * 3 * sizeof(GLfloat);
    }
    
    /* Allocate and copy UVs if present */
    if(header.flags & K3D_HAS_UVS) {
        mesh->texCoords = (GLfloat *)malloc(header.vertexCount * 2 * sizeof(GLfloat));
        if(!mesh->texCoords) {
            k3d_free(mesh);
            return NULL;
        }
        memcpy(mesh->texCoords, ptr + offset, header.vertexCount * 2 * sizeof(GLfloat));
        offset += header.vertexCount * 2 * sizeof(GLfloat);
    }
    
    /* Allocate and copy indices */
    mesh->indices = (uint16_t *)malloc(header.indexCount * sizeof(uint16_t));
    if(!mesh->indices) {
        k3d_free(mesh);
        return NULL;
    }
    memcpy(mesh->indices, ptr + offset, header.indexCount * sizeof(uint16_t));
    
    const char *primType = (mesh->primitiveType == K3D_PRIMITIVE_QUADS) ? "quads" : "triangles";
    uint32_t primCount = (mesh->primitiveType == K3D_PRIMITIVE_QUADS) ? 
                         (mesh->indexCount / 4) : (mesh->indexCount / 3);
    printf("K3D: Loaded from memory - %" PRIu32 " vertices, %" PRIu32 " indices (%" PRIu32 " %s)\n", 
           mesh->vertexCount, mesh->indexCount, primCount, primType);
    
    return mesh;
}

/* Free K3D mesh */
void k3d_free(K3DMesh *mesh) {
    if(!mesh) return;
    
    if(mesh->vertices) free(mesh->vertices);
    if(mesh->normals) free(mesh->normals);
    if(mesh->texCoords) free(mesh->texCoords);
    if(mesh->indices) free(mesh->indices);
    
    free(mesh);
}

void k3d_skeleton_free(K3DSkeleton *skeleton) {
    if(!skeleton) return;

    if(skeleton->bones) free(skeleton->bones);
    if(skeleton->influences) free(skeleton->influences);

    free(skeleton);
}

void k3d_skeletal_animation_free(K3DSkeletalAnimation *animation) {
    if(!animation) return;

    if(animation->frames) free(animation->frames);

    free(animation);
}

void k3d_vertex_animation_free(K3DVertexAnimation *animation) {
    if(!animation) return;

    if(animation->deltas) free(animation->deltas);
    if(animation->weights) free(animation->weights);

    free(animation);
}

K3DSkeleton *k3d_skeleton_load(const char *filename) {
    FILE *file;
    K3DSkeletonHeader header;
    K3DSkeleton *skeleton = NULL;
    size_t boneBytes;
    size_t influenceBytes;

    file = fopen(filename, "rb");
    if(!file) {
        printf("K3D Error: Could not open skeleton file: %s\n", filename);
        return NULL;
    }

    if(!k3d_read_exact(file, &header, sizeof(header), "skeleton header")) {
        fclose(file);
        return NULL;
    }

    if(!k3d_validate_sidecar_magic(header.magic, K3SK_MAGIC, "skeleton") ||
       header.version != K3_ANIM_VERSION || header.boneCount == 0 ||
       header.vertexCount == 0) {
        printf("K3D Error: Invalid skeleton header\n");
        fclose(file);
        return NULL;
    }

    skeleton = (K3DSkeleton *)calloc(1, sizeof(K3DSkeleton));
    if(!skeleton) {
        fclose(file);
        return NULL;
    }

    skeleton->boneCount = header.boneCount;
    skeleton->vertexCount = header.vertexCount;
    boneBytes = header.boneCount * sizeof(K3DBone);
    influenceBytes = header.vertexCount * sizeof(K3DVertexInfluence);

    skeleton->bones = (K3DBone *)malloc(boneBytes);
    skeleton->influences = (K3DVertexInfluence *)malloc(influenceBytes);
    if(!skeleton->bones || !skeleton->influences) {
        printf("K3D Error: Could not allocate skeleton data\n");
        k3d_skeleton_free(skeleton);
        fclose(file);
        return NULL;
    }

    if(!k3d_read_exact(file, skeleton->bones, boneBytes, "skeleton bones") ||
       !k3d_read_exact(file, skeleton->influences, influenceBytes,
                       "skeleton influences")) {
        k3d_skeleton_free(skeleton);
        fclose(file);
        return NULL;
    }

    fclose(file);
    printf("K3D: Loaded skeleton %s - %u bones, %" PRIu32 " weighted vertices\n",
           filename, skeleton->boneCount, skeleton->vertexCount);
    return skeleton;
}

K3DSkeletalAnimation *k3d_skeletal_animation_load(const char *filename) {
    FILE *file;
    K3DSkeletalAnimationHeader header;
    K3DSkeletalAnimation *animation = NULL;
    size_t frameBytes;

    file = fopen(filename, "rb");
    if(!file) {
        printf("K3D Error: Could not open skeletal animation file: %s\n", filename);
        return NULL;
    }

    if(!k3d_read_exact(file, &header, sizeof(header), "skeletal animation header")) {
        fclose(file);
        return NULL;
    }

    if(!k3d_validate_sidecar_magic(header.magic, K3SA_MAGIC, "skeletal animation") ||
       header.version != K3_ANIM_VERSION || header.boneCount == 0 ||
       header.frameCount == 0 || header.fps == 0) {
        printf("K3D Error: Invalid skeletal animation header\n");
        fclose(file);
        return NULL;
    }

    animation = (K3DSkeletalAnimation *)calloc(1, sizeof(K3DSkeletalAnimation));
    if(!animation) {
        fclose(file);
        return NULL;
    }

    animation->boneCount = header.boneCount;
    animation->frameCount = header.frameCount;
    animation->fps = header.fps;
    frameBytes = (size_t)header.boneCount * header.frameCount * sizeof(K3DTransform);
    animation->frames = (K3DTransform *)malloc(frameBytes);
    if(!animation->frames) {
        printf("K3D Error: Could not allocate skeletal animation frames\n");
        k3d_skeletal_animation_free(animation);
        fclose(file);
        return NULL;
    }

    if(!k3d_read_exact(file, animation->frames, frameBytes,
                       "skeletal animation frames")) {
        k3d_skeletal_animation_free(animation);
        fclose(file);
        return NULL;
    }

    fclose(file);
    printf("K3D: Loaded skeletal animation %s - %u bones, %u frames @ %u fps\n",
           filename, animation->boneCount, animation->frameCount, animation->fps);
    return animation;
}

K3DVertexAnimation *k3d_vertex_animation_load(const char *filename) {
    FILE *file;
    K3DVertexAnimationHeader header;
    K3DVertexAnimation *animation = NULL;
    size_t deltaBytes;
    size_t weightBytes;

    file = fopen(filename, "rb");
    if(!file) {
        printf("K3D Error: Could not open vertex animation file: %s\n", filename);
        return NULL;
    }

    if(!k3d_read_exact(file, &header, sizeof(header), "vertex animation header")) {
        fclose(file);
        return NULL;
    }

    if(!k3d_validate_sidecar_magic(header.magic, K3VA_MAGIC, "vertex animation") ||
       header.version != K3_ANIM_VERSION || header.vertexCount == 0 ||
       header.frameCount == 0 || header.fps == 0) {
        printf("K3D Error: Invalid vertex animation header\n");
        fclose(file);
        return NULL;
    }

    animation = (K3DVertexAnimation *)calloc(1, sizeof(K3DVertexAnimation));
    if(!animation) {
        fclose(file);
        return NULL;
    }

    animation->vertexCount = header.vertexCount;
    animation->frameCount = header.frameCount;
    animation->fps = header.fps;

    deltaBytes = (size_t)header.vertexCount * 3 * sizeof(GLfloat);
    weightBytes = (size_t)header.frameCount * sizeof(GLfloat);
    animation->deltas = (GLfloat *)malloc(deltaBytes);
    animation->weights = (GLfloat *)malloc(weightBytes);
    if(!animation->deltas || !animation->weights) {
        printf("K3D Error: Could not allocate vertex animation data\n");
        k3d_vertex_animation_free(animation);
        fclose(file);
        return NULL;
    }

    if(!k3d_read_exact(file, animation->deltas, deltaBytes, "vertex deltas") ||
       !k3d_read_exact(file, animation->weights, weightBytes, "vertex weights")) {
        k3d_vertex_animation_free(animation);
        fclose(file);
        return NULL;
    }

    fclose(file);
    printf("K3D: Loaded vertex animation %s - %" PRIu32 " vertices, %u frames @ %u fps\n",
           filename, animation->vertexCount, animation->frameCount, animation->fps);
    return animation;
}

/* Render K3D mesh using OpenGL vertex arrays */
void k3d_render_override(const K3DMesh *mesh, const GLfloat *vertices,
                         const GLfloat *normals) {
    GLenum primitiveMode;
    const GLfloat *vertexData = vertices;
    const GLfloat *normalData = normals;

    if(!mesh || !mesh->vertices || !mesh->indices) {
        printf("K3D Error: Invalid mesh for rendering\n");
        return;
    }

    if(!vertexData) {
        vertexData = mesh->vertices;
    }

    if(!normalData) {
        normalData = mesh->normals;
    }

    primitiveMode = k3d_get_primitive_mode(mesh->primitiveType);
    
    /* Enable vertex arrays */
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertexData);
    
    /* Enable normal arrays if present */
    if(normalData && (mesh->flags & K3D_HAS_NORMALS)) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, normalData);
    }
    
    /* Enable texture coordinate arrays if present */
    if(mesh->texCoords && (mesh->flags & K3D_HAS_UVS)) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, mesh->texCoords);
    }
    
    /* Draw indexed primitives */
    glDrawElements(primitiveMode, mesh->indexCount, GL_UNSIGNED_SHORT, mesh->indices);
    
    /* Disable arrays */
    glDisableClientState(GL_VERTEX_ARRAY);
    if(normalData && (mesh->flags & K3D_HAS_NORMALS)) {
        glDisableClientState(GL_NORMAL_ARRAY);
    }
    if(mesh->texCoords && (mesh->flags & K3D_HAS_UVS)) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

void k3d_render(const K3DMesh *mesh) {
    k3d_render_override(mesh, NULL, NULL);
}
