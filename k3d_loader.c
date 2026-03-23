/*
   KallistiOS 2.0.0

   k3d_loader.c
   (c)2026 K3D Binary Model Loader
   
   Loads K3D binary 3D models with indexed geometry for optimized rendering.
*/

#include "k3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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

/* Render K3D mesh using OpenGL vertex arrays */
void k3d_render(const K3DMesh *mesh) {
    GLenum primitiveMode;
    
    if(!mesh || !mesh->vertices || !mesh->indices) {
        printf("K3D Error: Invalid mesh for rendering\n");
        return;
    }
    
    /* Determine GL primitive type */
    switch(mesh->primitiveType) {
        case K3D_PRIMITIVE_QUADS:
            primitiveMode = GL_QUADS;
            break;
        case K3D_PRIMITIVE_TRIANGLES:
        default:
            primitiveMode = GL_TRIANGLES;
            break;
    }
    
    /* Enable vertex arrays */
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mesh->vertices);
    
    /* Enable normal arrays if present */
    if(mesh->normals && (mesh->flags & K3D_HAS_NORMALS)) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, mesh->normals);
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
    if(mesh->normals && (mesh->flags & K3D_HAS_NORMALS)) {
        glDisableClientState(GL_NORMAL_ARRAY);
    }
    if(mesh->texCoords && (mesh->flags & K3D_HAS_UVS)) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}
