#include "k3d_animation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int playing;
    float timeSeconds;
} PlaybackState;

typedef struct {
    K3DAnimation info;
    PlaybackState state;
    K3DSkeletalAnimation *skeletal;
    K3DVertexAnimation *vertex;
} K3DAnimationSlot;

struct K3DAnimationPlayer {
    K3DMesh *mesh;
    K3DSkeleton *skeleton;
    GLfloat *morphedVertices;
    GLfloat *animatedVertices;
    GLfloat *animatedNormals;
    GLfloat *boneMatrices;
    GLfloat *boneNormalMatrices;
    GLfloat *worldMatrices;
    K3DAnimationSlot animations[2];
    uint32_t animationCount;
};

static void reset_playback_state(PlaybackState *state) {
    state->playing = 0;
    state->timeSeconds = 0.0f;
}

static int playback_is_active(const PlaybackState *state) {
    return state->playing || state->timeSeconds > 0.0f;
}

static void toggle_playback_state(PlaybackState *state, const char *label) {
    state->playing = !state->playing;
    state->timeSeconds = 0.0f;
    printf("%s %s\n", label, state->playing ? "started" : "stopped");
}

static char *duplicate_animation_name(const char *path) {
    const char *name = path;
    const char *ext;
    size_t length;
    char *result;

    if(!path || !path[0]) {
        result = (char *)malloc(10);
        if(result) {
            memcpy(result, "animation", 10);
        }
        return result;
    }

    ext = strrchr(path, '.');
    if(strrchr(path, '/')) {
        name = strrchr(path, '/') + 1;
    }
    if(strrchr(name, '\\')) {
        name = strrchr(name, '\\') + 1;
    }
    if(!ext || ext < name) {
        ext = path + strlen(path);
    }

    length = (size_t)(ext - name);
    result = (char *)malloc(length + 1);
    if(!result) {
        return NULL;
    }

    memcpy(result, name, length);
    result[length] = '\0';
    return result;
}

static K3DAnimationSlot *find_animation_slot(K3DAnimationPlayer *player,
                                             const K3DAnimation *animation) {
    uint32_t index;

    if(!player || !animation) {
        return NULL;
    }

    for(index = 0; index < player->animationCount; ++index) {
        if(animation == &player->animations[index].info) {
            return &player->animations[index];
        }
    }

    return NULL;
}

static K3DAnimationSlot *find_animation_slot_by_type(K3DAnimationPlayer *player,
                                                     K3DAnimationType type) {
    uint32_t index;

    if(!player) {
        return NULL;
    }

    for(index = 0; index < player->animationCount; ++index) {
        if(player->animations[index].info.type == type) {
            return &player->animations[index];
        }
    }

    return NULL;
}

static int animation_slot_is_loaded(const K3DAnimationPlayer *player,
                                    const K3DAnimationSlot *slot) {
    if(!player || !slot) {
        return 0;
    }

    if(slot->info.type == K3D_ANIMATION_TYPE_SKELETAL) {
        return player->skeleton && slot->skeletal;
    }

    if(slot->info.type == K3D_ANIMATION_TYPE_VERTEX) {
        return slot->vertex != NULL;
    }

    return 0;
}

static void copy_vertex_data(GLfloat *dst, const GLfloat *src, uint32_t vertexCount) {
    memcpy(dst, src, vertexCount * 3 * sizeof(GLfloat));
}

static void sync_base_pose_buffers(K3DAnimationPlayer *player) {
    copy_vertex_data(player->morphedVertices, player->mesh->vertices,
                     player->mesh->vertexCount);
    copy_vertex_data(player->animatedVertices, player->morphedVertices,
                     player->mesh->vertexCount);

    if(player->animatedNormals && player->mesh->normals) {
        copy_vertex_data(player->animatedNormals, player->mesh->normals,
                         player->mesh->vertexCount);
    }
}

static void sample_frame_window(uint16_t frameCount, uint16_t fps,
                                float timeSeconds, uint16_t *frameA,
                                uint16_t *frameB, float *frameFraction) {
    float framePosition;

    if(frameCount <= 1 || fps == 0) {
        *frameA = 0;
        *frameB = 0;
        *frameFraction = 0.0f;
        return;
    }

    framePosition = timeSeconds * (float)fps;
    if(framePosition < 0.0f) {
        framePosition = 0.0f;
    }

    *frameA = (uint16_t)framePosition;
    if(*frameA >= frameCount - 1) {
        *frameA = (uint16_t)(frameCount - 1);
        *frameB = *frameA;
        *frameFraction = 0.0f;
        return;
    }

    *frameB = (uint16_t)(*frameA + 1);
    *frameFraction = framePosition - (float)(*frameA);
}

static float animation_duration_seconds(uint16_t frameCount, uint16_t fps) {
    if(fps == 0 || frameCount <= 1) {
        return 0.0f;
    }

    return (float)(frameCount - 1) / (float)fps;
}

static void free_skeletal_runtime(K3DAnimationPlayer *player) {
    K3DAnimationSlot *slot = find_animation_slot_by_type(player, K3D_ANIMATION_TYPE_SKELETAL);

    if(slot) {
        k3d_skeletal_animation_free(slot->skeletal);
        slot->skeletal = NULL;
        reset_playback_state(&slot->state);
    }

    k3d_skeleton_free(player->skeleton);
    free(player->boneMatrices);
    free(player->boneNormalMatrices);
    free(player->worldMatrices);
    player->skeleton = NULL;
    player->boneMatrices = NULL;
    player->boneNormalMatrices = NULL;
    player->worldMatrices = NULL;
}

static void free_vertex_runtime(K3DAnimationPlayer *player) {
    K3DAnimationSlot *slot = find_animation_slot_by_type(player, K3D_ANIMATION_TYPE_VERTEX);

    if(!slot) {
        return;
    }

    k3d_vertex_animation_free(slot->vertex);
    slot->vertex = NULL;
    reset_playback_state(&slot->state);
}

static void free_animation_names(K3DAnimationPlayer *player) {
    uint32_t index;

    for(index = 0; index < player->animationCount; ++index) {
        free((void *)player->animations[index].info.name);
        player->animations[index].info.name = NULL;
    }
}

static void free_player_buffers(K3DAnimationPlayer *player) {
    free(player->morphedVertices);
    free(player->animatedVertices);
    free(player->animatedNormals);
    player->morphedVertices = NULL;
    player->animatedVertices = NULL;
    player->animatedNormals = NULL;
}

static float clamp01(float value) {
    if(value < 0.0f) {
        return 0.0f;
    }

    if(value > 1.0f) {
        return 1.0f;
    }

    return value;
}

static void mat4_multiply(GLfloat *out, const GLfloat *a, const GLfloat *b) {
    GLfloat result[16];
    int row;
    int col;
    int index;

    for(col = 0; col < 4; ++col) {
        for(row = 0; row < 4; ++row) {
            GLfloat sum = 0.0f;

            for(index = 0; index < 4; ++index) {
                sum += a[index * 4 + row] * b[col * 4 + index];
            }

            result[col * 4 + row] = sum;
        }
    }

    memcpy(out, result, sizeof(result));
}

static void quat_normalize(GLfloat *quat) {
    GLfloat length = sqrtf(quat[0] * quat[0] + quat[1] * quat[1] +
                           quat[2] * quat[2] + quat[3] * quat[3]);

    if(length > 0.00001f) {
        quat[0] /= length;
        quat[1] /= length;
        quat[2] /= length;
        quat[3] /= length;
    }
    else {
        quat[0] = 0.0f;
        quat[1] = 0.0f;
        quat[2] = 0.0f;
        quat[3] = 1.0f;
    }
}

static void quat_nlerp(GLfloat *out, const GLfloat *a, const GLfloat *b, GLfloat t) {
    GLfloat endQuat[4];
    GLfloat dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];

    memcpy(endQuat, b, sizeof(endQuat));
    if(dot < 0.0f) {
        endQuat[0] = -endQuat[0];
        endQuat[1] = -endQuat[1];
        endQuat[2] = -endQuat[2];
        endQuat[3] = -endQuat[3];
    }

    out[0] = a[0] + (endQuat[0] - a[0]) * t;
    out[1] = a[1] + (endQuat[1] - a[1]) * t;
    out[2] = a[2] + (endQuat[2] - a[2]) * t;
    out[3] = a[3] + (endQuat[3] - a[3]) * t;
    quat_normalize(out);
}

static void compose_transform_matrix(GLfloat *matrix, const K3DTransform *transform) {
    GLfloat x = transform->rotation[0];
    GLfloat y = transform->rotation[1];
    GLfloat zq = transform->rotation[2];
    GLfloat w = transform->rotation[3];
    GLfloat sx = transform->scale[0];
    GLfloat sy = transform->scale[1];
    GLfloat sz = transform->scale[2];

    GLfloat xx = x * x;
    GLfloat yy = y * y;
    GLfloat zz = zq * zq;
    GLfloat xy = x * y;
    GLfloat xz = x * zq;
    GLfloat yz = y * zq;
    GLfloat wx = w * x;
    GLfloat wy = w * y;
    GLfloat wz = w * zq;

    matrix[0] = (1.0f - 2.0f * (yy + zz)) * sx;
    matrix[1] = (2.0f * (xy + wz)) * sx;
    matrix[2] = (2.0f * (xz - wy)) * sx;
    matrix[3] = 0.0f;

    matrix[4] = (2.0f * (xy - wz)) * sy;
    matrix[5] = (1.0f - 2.0f * (xx + zz)) * sy;
    matrix[6] = (2.0f * (yz + wx)) * sy;
    matrix[7] = 0.0f;

    matrix[8] = (2.0f * (xz + wy)) * sz;
    matrix[9] = (2.0f * (yz - wx)) * sz;
    matrix[10] = (1.0f - 2.0f * (xx + yy)) * sz;
    matrix[11] = 0.0f;

    matrix[12] = transform->translation[0];
    matrix[13] = transform->translation[1];
    matrix[14] = transform->translation[2];
    matrix[15] = 1.0f;
}

static void build_normal_matrix(GLfloat *out, const GLfloat *matrix) {
    GLfloat a00 = matrix[0];
    GLfloat a01 = matrix[4];
    GLfloat a02 = matrix[8];
    GLfloat a10 = matrix[1];
    GLfloat a11 = matrix[5];
    GLfloat a12 = matrix[9];
    GLfloat a20 = matrix[2];
    GLfloat a21 = matrix[6];
    GLfloat a22 = matrix[10];
    GLfloat cofactor00 = a11 * a22 - a12 * a21;
    GLfloat cofactor01 = a12 * a20 - a10 * a22;
    GLfloat cofactor02 = a10 * a21 - a11 * a20;
    GLfloat cofactor10 = a02 * a21 - a01 * a22;
    GLfloat cofactor11 = a00 * a22 - a02 * a20;
    GLfloat cofactor12 = a01 * a20 - a00 * a21;
    GLfloat cofactor20 = a01 * a12 - a02 * a11;
    GLfloat cofactor21 = a02 * a10 - a00 * a12;
    GLfloat cofactor22 = a00 * a11 - a01 * a10;
    GLfloat determinant = a00 * cofactor00 + a01 * cofactor01 + a02 * cofactor02;

    if(fabsf(determinant) < 0.000001f) {
        out[0] = 1.0f; out[1] = 0.0f; out[2] = 0.0f;
        out[3] = 0.0f; out[4] = 1.0f; out[5] = 0.0f;
        out[6] = 0.0f; out[7] = 0.0f; out[8] = 1.0f;
        return;
    }

    determinant = 1.0f / determinant;
    out[0] = cofactor00 * determinant;
    out[1] = cofactor10 * determinant;
    out[2] = cofactor20 * determinant;
    out[3] = cofactor01 * determinant;
    out[4] = cofactor11 * determinant;
    out[5] = cofactor21 * determinant;
    out[6] = cofactor02 * determinant;
    out[7] = cofactor12 * determinant;
    out[8] = cofactor22 * determinant;
}

static void sample_skeletal_transform(K3DTransform *out,
                                      const K3DSkeletalAnimation *animation,
                                      uint16_t boneIndex, float timeSeconds) {
    uint16_t frameA;
    uint16_t frameB;
    float frameFraction;
    const K3DTransform *transformA;
    const K3DTransform *transformB;
    int component;

    if(animation->frameCount == 1) {
        *out = animation->frames[boneIndex];
        return;
    }

    sample_frame_window(animation->frameCount, animation->fps, timeSeconds,
                        &frameA, &frameB, &frameFraction);

    transformA = &animation->frames[frameA * animation->boneCount + boneIndex];
    transformB = &animation->frames[frameB * animation->boneCount + boneIndex];

    for(component = 0; component < 3; ++component) {
        out->translation[component] = transformA->translation[component] +
            (transformB->translation[component] - transformA->translation[component]) * frameFraction;
        out->scale[component] = transformA->scale[component] +
            (transformB->scale[component] - transformA->scale[component]) * frameFraction;
    }

    quat_nlerp(out->rotation, transformA->rotation, transformB->rotation, frameFraction);
}

static float sample_vertex_weight(const K3DVertexAnimation *animation, float timeSeconds) {
    uint16_t frameA;
    uint16_t frameB;
    float frameFraction;

    if(animation->frameCount == 1) {
        return animation->weights[0];
    }

    sample_frame_window(animation->frameCount, animation->fps, timeSeconds,
                        &frameA, &frameB, &frameFraction);
    if(frameA == frameB) {
        return animation->weights[animation->frameCount - 1];
    }

    return animation->weights[frameA] +
        (animation->weights[frameB] - animation->weights[frameA]) * frameFraction;
}

static void update_playback_state(PlaybackState *state, float deltaSeconds, float durationSeconds) {
    if(!state->playing) {
        return;
    }

    state->timeSeconds += deltaSeconds;
    if(durationSeconds <= 0.0f) {
        return;
    }

    while(state->timeSeconds > durationSeconds) {
        state->timeSeconds -= durationSeconds;
    }
}

static void apply_vertex_animation(K3DAnimationPlayer *player) {
    K3DAnimationSlot *slot;
    uint32_t vertexIndex;
    float weight = 0.0f;

    if(!player->mesh || !player->morphedVertices) {
        return;
    }

    copy_vertex_data(player->morphedVertices, player->mesh->vertices,
                     player->mesh->vertexCount);

    slot = find_animation_slot_by_type(player, K3D_ANIMATION_TYPE_VERTEX);
    if(!slot || !slot->vertex || slot->vertex->vertexCount != player->mesh->vertexCount) {
        return;
    }

    if(playback_is_active(&slot->state)) {
        weight = clamp01(sample_vertex_weight(slot->vertex,
                                              slot->state.timeSeconds));
    }

    if(weight <= 0.0001f) {
        return;
    }

    for(vertexIndex = 0; vertexIndex < player->mesh->vertexCount; ++vertexIndex) {
        uint32_t base = vertexIndex * 3;
        player->morphedVertices[base + 0] += slot->vertex->deltas[base + 0] * weight;
        player->morphedVertices[base + 1] += slot->vertex->deltas[base + 1] * weight;
        player->morphedVertices[base + 2] += slot->vertex->deltas[base + 2] * weight;
    }
}

static void apply_skeletal_animation(K3DAnimationPlayer *player) {
    K3DAnimationSlot *slot;
    uint16_t boneIndex;
    uint32_t vertexIndex;

    if(!player->mesh || !player->animatedVertices || !player->morphedVertices) {
        return;
    }

    copy_vertex_data(player->animatedVertices, player->morphedVertices,
                     player->mesh->vertexCount);
    if(player->animatedNormals && player->mesh->normals) {
        copy_vertex_data(player->animatedNormals, player->mesh->normals,
                         player->mesh->vertexCount);
    }

    slot = find_animation_slot_by_type(player, K3D_ANIMATION_TYPE_SKELETAL);
    if(!player->skeleton || !slot || !slot->skeletal || !player->boneMatrices ||
       !player->boneNormalMatrices || !player->worldMatrices ||
       player->skeleton->vertexCount != player->mesh->vertexCount ||
       player->skeleton->boneCount != slot->skeletal->boneCount) {
        return;
    }

    if(!playback_is_active(&slot->state)) {
        return;
    }

    for(boneIndex = 0; boneIndex < player->skeleton->boneCount; ++boneIndex) {
        K3DTransform sampledTransform;
        GLfloat localMatrix[16];
        GLfloat *worldMatrix = &player->worldMatrices[boneIndex * 16];
        GLfloat *skinMatrix = &player->boneMatrices[boneIndex * 16];
        int16_t parentIndex = player->skeleton->bones[boneIndex].parentIndex;

        sample_skeletal_transform(&sampledTransform, slot->skeletal, boneIndex,
                      slot->state.timeSeconds);
        compose_transform_matrix(localMatrix, &sampledTransform);

        if(parentIndex >= 0 && parentIndex < (int16_t)player->skeleton->boneCount) {
            mat4_multiply(worldMatrix, &player->worldMatrices[parentIndex * 16], localMatrix);
        }
        else {
            memcpy(worldMatrix, localMatrix, sizeof(localMatrix));
        }

        mat4_multiply(skinMatrix, worldMatrix,
                      player->skeleton->bones[boneIndex].inverseBind);
        build_normal_matrix(&player->boneNormalMatrices[boneIndex * 9], skinMatrix);
    }

    for(vertexIndex = 0; vertexIndex < player->mesh->vertexCount; ++vertexIndex) {
        const K3DVertexInfluence *influence = &player->skeleton->influences[vertexIndex];
        const GLfloat *sourceVertex = &player->morphedVertices[vertexIndex * 3];
        const GLfloat *sourceNormal = player->mesh->normals ?
            &player->mesh->normals[vertexIndex * 3] : NULL;
        GLfloat blendedVertex[3] = {0.0f, 0.0f, 0.0f};
        GLfloat blendedNormal[3] = {0.0f, 0.0f, 0.0f};
        float totalWeight = 0.0f;
        int slot;

        for(slot = 0; slot < K3D_MAX_BONE_INFLUENCES; ++slot) {
            uint8_t index = influence->boneIndex[slot];
            float weight;
            const GLfloat *matrix;
            const GLfloat *normalMatrix;

            if(index == K3D_INVALID_BONE_INDEX || index >= player->skeleton->boneCount) {
                continue;
            }

            weight = (float)influence->boneWeight[slot] / 255.0f;
            if(weight <= 0.0f) {
                continue;
            }

            matrix = &player->boneMatrices[index * 16];
            normalMatrix = &player->boneNormalMatrices[index * 9];
            blendedVertex[0] += (matrix[0] * sourceVertex[0] + matrix[4] * sourceVertex[1] +
                                 matrix[8] * sourceVertex[2] + matrix[12]) * weight;
            blendedVertex[1] += (matrix[1] * sourceVertex[0] + matrix[5] * sourceVertex[1] +
                                 matrix[9] * sourceVertex[2] + matrix[13]) * weight;
            blendedVertex[2] += (matrix[2] * sourceVertex[0] + matrix[6] * sourceVertex[1] +
                                 matrix[10] * sourceVertex[2] + matrix[14]) * weight;

            if(sourceNormal && player->animatedNormals) {
                blendedNormal[0] += (normalMatrix[0] * sourceNormal[0] +
                                     normalMatrix[3] * sourceNormal[1] +
                                     normalMatrix[6] * sourceNormal[2]) * weight;
                blendedNormal[1] += (normalMatrix[1] * sourceNormal[0] +
                                     normalMatrix[4] * sourceNormal[1] +
                                     normalMatrix[7] * sourceNormal[2]) * weight;
                blendedNormal[2] += (normalMatrix[2] * sourceNormal[0] +
                                     normalMatrix[5] * sourceNormal[1] +
                                     normalMatrix[8] * sourceNormal[2]) * weight;
            }

            totalWeight += weight;
        }

        if(totalWeight > 0.0f) {
            GLfloat invWeight = 1.0f / totalWeight;
            player->animatedVertices[vertexIndex * 3 + 0] = blendedVertex[0] * invWeight;
            player->animatedVertices[vertexIndex * 3 + 1] = blendedVertex[1] * invWeight;
            player->animatedVertices[vertexIndex * 3 + 2] = blendedVertex[2] * invWeight;

            if(sourceNormal && player->animatedNormals) {
                GLfloat normalLength = sqrtf(blendedNormal[0] * blendedNormal[0] +
                                             blendedNormal[1] * blendedNormal[1] +
                                             blendedNormal[2] * blendedNormal[2]);
                if(normalLength > 0.00001f) {
                    player->animatedNormals[vertexIndex * 3 + 0] = blendedNormal[0] / normalLength;
                    player->animatedNormals[vertexIndex * 3 + 1] = blendedNormal[1] / normalLength;
                    player->animatedNormals[vertexIndex * 3 + 2] = blendedNormal[2] / normalLength;
                }
            }
        }
    }
}

static void disable_skeletal_animation(K3DAnimationPlayer *player, const char *message) {
    printf("%s\n", message);
    free_skeletal_runtime(player);
}

static void disable_vertex_animation(K3DAnimationPlayer *player, const char *message) {
    printf("%s\n", message);
    free_vertex_runtime(player);
}

K3DAnimationPlayer *k3d_animation_player_load(const char *meshPath,
                                              const char *skeletonPath,
                                              const char *skeletalAnimPath,
                                              const char *vertexAnimPath) {
    K3DAnimationPlayer *player = (K3DAnimationPlayer *)calloc(1, sizeof(K3DAnimationPlayer));

    if(!player) {
        return NULL;
    }

    player->animationCount = 2;
    player->animations[0].info.type = K3D_ANIMATION_TYPE_SKELETAL;
    player->animations[0].info.name = duplicate_animation_name(skeletalAnimPath);
    player->animations[1].info.type = K3D_ANIMATION_TYPE_VERTEX;
    player->animations[1].info.name = duplicate_animation_name(vertexAnimPath);

    player->mesh = k3d_load(meshPath);
    if(!player->mesh) {
        free_animation_names(player);
        free(player);
        return NULL;
    }

    player->skeleton = k3d_skeleton_load(skeletonPath);
    player->animations[0].skeletal = k3d_skeletal_animation_load(skeletalAnimPath);
    player->animations[1].vertex = k3d_vertex_animation_load(vertexAnimPath);

    player->morphedVertices = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
    player->animatedVertices = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
    if(player->mesh->normals) {
        player->animatedNormals = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
    }

    if(player->skeleton && player->animations[0].skeletal) {
        player->boneMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 16 * sizeof(GLfloat));
        player->boneNormalMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 9 * sizeof(GLfloat));
        player->worldMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 16 * sizeof(GLfloat));
    }

    if(!player->morphedVertices || !player->animatedVertices ||
       (player->mesh->normals && !player->animatedNormals) ||
       ((player->skeleton && player->animations[0].skeletal) &&
        (!player->boneMatrices || !player->boneNormalMatrices || !player->worldMatrices))) {
        printf("Failed to allocate animation buffers\n");
        k3d_animation_player_free(player);
        return NULL;
    }

    if(player->skeleton && player->skeleton->vertexCount != player->mesh->vertexCount) {
        disable_skeletal_animation(player, "K3D Warning: Skeleton vertex count does not match mesh, disabling skeletal animation");
    }

    if(player->animations[1].vertex && player->animations[1].vertex->vertexCount != player->mesh->vertexCount) {
        disable_vertex_animation(player, "K3D Warning: Vertex animation count does not match mesh, disabling vertex animation");
    }

    if(player->skeleton && player->animations[0].skeletal &&
       player->skeleton->boneCount != player->animations[0].skeletal->boneCount) {
        disable_skeletal_animation(player, "K3D Warning: Skeleton bone count does not match skeletal clip, disabling skeletal animation");
    }

    sync_base_pose_buffers(player);

    return player;
}

void k3d_animation_player_free(K3DAnimationPlayer *player) {
    if(!player) return;

    free_player_buffers(player);
    free_vertex_runtime(player);
    free_skeletal_runtime(player);
    free_animation_names(player);
    k3d_free(player->mesh);
    free(player);
}

void k3d_animation_player_update(K3DAnimationPlayer *player, float deltaSeconds) {
    float bounceDuration = 0.0f;
    float spikeDuration = 0.0f;

    if(!player) {
        return;
    }

    if(player->animations[0].skeletal) {
        bounceDuration = animation_duration_seconds(player->animations[0].skeletal->frameCount,
                                                    player->animations[0].skeletal->fps);
    }

    if(player->animations[1].vertex) {
        spikeDuration = animation_duration_seconds(player->animations[1].vertex->frameCount,
                                                   player->animations[1].vertex->fps);
    }

    update_playback_state(&player->animations[0].state, deltaSeconds, bounceDuration);
    update_playback_state(&player->animations[1].state, deltaSeconds, spikeDuration);
    apply_vertex_animation(player);
    apply_skeletal_animation(player);
}

void k3d_animation_player_render(const K3DAnimationPlayer *player) {
    const GLfloat *vertexData;
    const GLfloat *normalData;

    if(!player || !player->mesh) {
        return;
    }

    vertexData = player->animatedVertices ? player->animatedVertices : player->mesh->vertices;
    normalData = player->animatedNormals ? player->animatedNormals : player->mesh->normals;
    k3d_render_override(player->mesh, vertexData, normalData);
}

uint32_t k3d_animation_player_get_animation_count(const K3DAnimationPlayer *player) {
    return player ? player->animationCount : 0;
}

const K3DAnimation *k3d_animation_player_get_animation(const K3DAnimationPlayer *player,
                                                       uint32_t index) {
    if(!player || index >= player->animationCount) {
        return NULL;
    }

    return &player->animations[index].info;
}

void k3d_animation_player_toggle(K3DAnimationPlayer *player,
                                 const K3DAnimation *animation) {
    K3DAnimationSlot *slot = find_animation_slot(player, animation);

    if(!animation_slot_is_loaded(player, slot)) {
        return;
    }

    toggle_playback_state(&slot->state, slot->info.name ? slot->info.name : "animation");
}