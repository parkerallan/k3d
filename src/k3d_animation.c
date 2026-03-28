#include "k3d_animation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int playing;
    float timeSeconds;
    float value;
    float weight;
    float fadeStartWeight;
    float fadeTargetWeight;
    float fadeElapsedSeconds;
    float fadeDurationSeconds;
} PlaybackState;

typedef struct {
    K3DAnimation info;
    PlaybackState state;
    union {
        K3DSkeletalAnimation *skeletal;
        K3DVertexAnimation *vertex;
    } clip;
} K3DAnimationRecord;

struct K3DAnimationPlayer {
    K3DMesh *mesh;
    K3DSkeleton *skeleton;
    K3DTransform *bindPoseTransforms;
    GLfloat *morphedVertices;
    GLfloat *animatedVertices;
    GLfloat *animatedNormals;
    GLfloat *boneMatrices;
    GLfloat *boneNormalMatrices;
    GLfloat *worldMatrices;
    K3DAnimationRecord *animations;
    uint32_t animationCount;
    uint32_t animationCapacity;
    int skeletalBlendEnabled;
    float skeletalBlendDurationSeconds;
};

static float clamp01(float value);
static void sample_skeletal_transform(K3DTransform *out,
                                      const K3DSkeletalAnimation *animation,
                                      uint16_t boneIndex, float timeSeconds);

static void reset_playback_state(PlaybackState *state) {
    state->playing = 0;
    state->timeSeconds = 0.0f;
    state->value = 0.0f;
    state->weight = 0.0f;
    state->fadeStartWeight = 0.0f;
    state->fadeTargetWeight = 0.0f;
    state->fadeElapsedSeconds = 0.0f;
    state->fadeDurationSeconds = 0.0f;
}

static int playback_is_active(const PlaybackState *state) {
    return state->playing || state->timeSeconds > 0.0f || state->value > 0.0f ||
        state->weight > 0.0001f ||
        fabsf(state->fadeTargetWeight - state->weight) > 0.0001f;
}

// This is optional for vertex/shapekey animations, if you are using a wave function you would only use set_value()
float k3d_animation_accumulate_value(float currentValue,
                                     float inputValue,
                                     float riseRate,
                                     float fallRate,
                                     float deltaSeconds) {
    currentValue = clamp01(currentValue);
    inputValue = clamp01(inputValue);

    if(deltaSeconds <= 0.0f) {
        return currentValue;
    }

    if(inputValue > 0.0f) {
        currentValue += inputValue * riseRate * deltaSeconds;
    }
    else {
        currentValue -= fallRate * deltaSeconds;
    }

    return clamp01(currentValue);
}

static void start_playback_state(PlaybackState *state, const char *label) {
    state->playing = 1;
    state->timeSeconds = 0.0f;
    state->weight = 1.0f;
    state->fadeStartWeight = 1.0f;
    state->fadeTargetWeight = 1.0f;
    state->fadeElapsedSeconds = 0.0f;
    state->fadeDurationSeconds = 0.0f;
    printf("%s started\n", label);
}

static void stop_playback_state(PlaybackState *state, const char *label) {
    state->playing = 0;
    state->timeSeconds = 0.0f;
    state->value = 0.0f;
    state->weight = 0.0f;
    state->fadeStartWeight = 0.0f;
    state->fadeTargetWeight = 0.0f;
    state->fadeElapsedSeconds = 0.0f;
    state->fadeDurationSeconds = 0.0f;
    printf("%s stopped\n", label);
}

static void begin_weight_fade(PlaybackState *state, float targetWeight,
                              float durationSeconds) {
    state->fadeStartWeight = clamp01(state->weight);
    state->fadeTargetWeight = clamp01(targetWeight);
    state->fadeElapsedSeconds = 0.0f;
    state->fadeDurationSeconds = durationSeconds > 0.0f ? durationSeconds : 0.0f;

    if(state->fadeDurationSeconds == 0.0f) {
        state->weight = state->fadeTargetWeight;
        state->fadeElapsedSeconds = 0.0f;
    }
}

static void start_skeletal_playback(PlaybackState *state, float startWeight,
                                    float targetWeight, float durationSeconds) {
    state->playing = 1;
    state->timeSeconds = 0.0f;
    state->weight = clamp01(startWeight);
    begin_weight_fade(state, targetWeight, durationSeconds);
}

static void fade_skeletal_playback(PlaybackState *state, float targetWeight,
                                   float durationSeconds) {
    if(!playback_is_active(state) && targetWeight <= 0.0f) {
        return;
    }

    state->playing = 1;
    begin_weight_fade(state, targetWeight, durationSeconds);
}

static char *duplicate_animation_name(const char *name) {
    size_t length;
    char *result;

    if(!name || !name[0]) {
        result = (char *)malloc(10);
        if(result) {
            memcpy(result, "animation", 10);
        }
        return result;
    }

    length = strlen(name);
    result = (char *)malloc(length + 1);
    if(!result) {
        return NULL;
    }

    memcpy(result, name, length);
    result[length] = '\0';
    return result;
}

static K3DAnimationRecord *find_animation_record(K3DAnimationPlayer *player,
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

static K3DAnimationRecord *find_animation_record_by_name(const K3DAnimationPlayer *player,
                                                         const char *name) {
    uint32_t index;

    if(!player || !name) {
        return NULL;
    }

    for(index = 0; index < player->animationCount; ++index) {
        if(player->animations[index].info.name &&
           strcmp(player->animations[index].info.name, name) == 0) {
            return &player->animations[index];
        }
    }

    return NULL;
}

static K3DAnimationRecord *find_active_animation_by_type(K3DAnimationPlayer *player,
                                                         K3DAnimationType type) {
    uint32_t index;

    if(!player) {
        return NULL;
    }

    for(index = 0; index < player->animationCount; ++index) {
        K3DAnimationRecord *record = &player->animations[index];

        if(record->info.type == type && playback_is_active(&record->state)) {
            return record;
        }
    }

    return NULL;
}

static int animation_record_is_loaded(const K3DAnimationPlayer *player,
                                      const K3DAnimationRecord *record) {
    if(!player || !record) {
        return 0;
    }

    if(record->info.type == K3D_ANIMATION_TYPE_SKELETAL) {
        return player->skeleton && record->clip.skeletal;
    }

    if(record->info.type == K3D_ANIMATION_TYPE_VERTEX) {
        return record->clip.vertex != NULL;
    }

    return 0;
}

static void stop_other_animations_of_type(K3DAnimationPlayer *player,
                                          const K3DAnimation *animation) {
    uint32_t index;

    if(!player || !animation) {
        return;
    }

    for(index = 0; index < player->animationCount; ++index) {
        K3DAnimationRecord *record = &player->animations[index];

        if(record->info.type == animation->type && &record->info != animation) {
            reset_playback_state(&record->state);
        }
    }
}

static float get_skeletal_blend_duration(const K3DAnimationPlayer *player) {
    if(!player || !player->skeletalBlendEnabled) {
        return 0.0f;
    }

    return player->skeletalBlendDurationSeconds;
}

static int skeletal_record_is_active(const K3DAnimationPlayer *player,
                                     const K3DAnimationRecord *record) {
    return player && record && record->info.type == K3D_ANIMATION_TYPE_SKELETAL &&
        animation_record_is_loaded(player, record) && playback_is_active(&record->state) &&
        record->state.weight > 0.0001f;
}

static void fade_other_skeletal_animations(K3DAnimationPlayer *player,
                                           const K3DAnimation *animation,
                                           float durationSeconds) {
    uint32_t index;

    if(!player || !animation) {
        return;
    }

    for(index = 0; index < player->animationCount; ++index) {
        K3DAnimationRecord *record = &player->animations[index];

        if(record->info.type != K3D_ANIMATION_TYPE_SKELETAL || &record->info == animation) {
            continue;
        }

        if(playback_is_active(&record->state)) {
            fade_skeletal_playback(&record->state, 0.0f, durationSeconds);
        }
    }
}

static void copy_vertex_data(GLfloat *dst, const GLfloat *src, uint32_t vertexCount) {
    memcpy(dst, src, vertexCount * 3 * sizeof(GLfloat));
}

static void sync_base_pose_buffers(K3DAnimationPlayer *player) {
    if(!player->morphedVertices) {
        return;
    }

    copy_vertex_data(player->morphedVertices, player->mesh->vertices,
                     player->mesh->vertexCount);

    if(player->animatedVertices) {
        copy_vertex_data(player->animatedVertices, player->mesh->vertices,
                         player->mesh->vertexCount);
    }

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
    free(player->bindPoseTransforms);
    free(player->boneMatrices);
    free(player->boneNormalMatrices);
    free(player->worldMatrices);
    player->bindPoseTransforms = NULL;
    player->boneMatrices = NULL;
    player->boneNormalMatrices = NULL;
    player->worldMatrices = NULL;
}

static void free_vertex_runtime(K3DAnimationPlayer *player) {
    (void)player;
}

static void free_animation_names(K3DAnimationPlayer *player) {
    uint32_t index;

    for(index = 0; index < player->animationCount; ++index) {
        free((void *)player->animations[index].info.name);
        player->animations[index].info.name = NULL;
    }
}

static void free_animation_records(K3DAnimationPlayer *player) {
    free_animation_names(player);
    free(player->animations);
    player->animations = NULL;
    player->animationCount = 0;
    player->animationCapacity = 0;
}

static void free_player_buffers(K3DAnimationPlayer *player) {
    free(player->morphedVertices);
    free(player->animatedVertices);
    free(player->animatedNormals);
    player->morphedVertices = NULL;
    player->animatedVertices = NULL;
    player->animatedNormals = NULL;
}

static int ensure_base_buffers(K3DAnimationPlayer *player) {
    if(player->morphedVertices) {
        return 1;
    }

    player->morphedVertices = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
    if(!player->morphedVertices) {
        return 0;
    }

    if(player->mesh->normals) {
        player->animatedNormals = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
        if(!player->animatedNormals) {
            free_player_buffers(player);
            return 0;
        }
    }

    sync_base_pose_buffers(player);
    return 1;
}

static int ensure_skeletal_buffers(K3DAnimationPlayer *player) {
    if(!ensure_base_buffers(player)) {
        return 0;
    }

    if(!player->animatedVertices) {
        player->animatedVertices = (GLfloat *)malloc(player->mesh->vertexCount * 3 * sizeof(GLfloat));
        if(!player->animatedVertices) {
            return 0;
        }
    }

    if(player->boneMatrices) {
        sync_base_pose_buffers(player);
        return 1;
    }

    player->boneMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 16 * sizeof(GLfloat));
    player->boneNormalMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 9 * sizeof(GLfloat));
    player->worldMatrices = (GLfloat *)malloc(player->skeleton->boneCount * 16 * sizeof(GLfloat));
    player->bindPoseTransforms = (K3DTransform *)malloc(player->skeleton->boneCount *
                                                        sizeof(K3DTransform));

    if(!player->boneMatrices || !player->boneNormalMatrices || !player->worldMatrices ||
       !player->bindPoseTransforms) {
        free_skeletal_runtime(player);
        return 0;
    }

    sync_base_pose_buffers(player);
    return 1;
}

static int ensure_animation_capacity(K3DAnimationPlayer *player) {
    uint32_t newCapacity;
    K3DAnimationRecord *records;

    if(player->animationCount < player->animationCapacity) {
        return 1;
    }

    newCapacity = player->animationCapacity ? player->animationCapacity * 2 : 4;
    records = (K3DAnimationRecord *)realloc(player->animations,
                                            newCapacity * sizeof(K3DAnimationRecord));
    if(!records) {
        return 0;
    }

    memset(&records[player->animationCapacity], 0,
           (newCapacity - player->animationCapacity) * sizeof(K3DAnimationRecord));
    player->animations = records;
    player->animationCapacity = newCapacity;
    return 1;
}

static K3DAnimation *add_animation_record(K3DAnimationPlayer *player,
                                          const char *name,
                                          K3DAnimationType type) {
    K3DAnimationRecord *record;
    char *duplicateName;

    if(!ensure_animation_capacity(player)) {
        return NULL;
    }

    duplicateName = duplicate_animation_name(name);
    if(!duplicateName) {
        return NULL;
    }

    record = &player->animations[player->animationCount++];
    memset(record, 0, sizeof(*record));
    record->info.name = duplicateName;
    record->info.type = type;
    reset_playback_state(&record->state);
    return &record->info;
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

static int invert_affine_matrix(GLfloat *out, const GLfloat *matrix) {
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
    GLfloat cofactor01 = a02 * a21 - a01 * a22;
    GLfloat cofactor02 = a01 * a12 - a02 * a11;
    GLfloat cofactor10 = a12 * a20 - a10 * a22;
    GLfloat cofactor11 = a00 * a22 - a02 * a20;
    GLfloat cofactor12 = a02 * a10 - a00 * a12;
    GLfloat cofactor20 = a10 * a21 - a11 * a20;
    GLfloat cofactor21 = a01 * a20 - a00 * a21;
    GLfloat cofactor22 = a00 * a11 - a01 * a10;
    GLfloat determinant = a00 * cofactor00 + a01 * cofactor10 + a02 * cofactor20;
    GLfloat tx;
    GLfloat ty;
    GLfloat tz;

    if(fabsf(determinant) < 0.000001f) {
        return 0;
    }

    determinant = 1.0f / determinant;
    out[0] = cofactor00 * determinant;
    out[1] = cofactor10 * determinant;
    out[2] = cofactor20 * determinant;
    out[3] = 0.0f;
    out[4] = cofactor01 * determinant;
    out[5] = cofactor11 * determinant;
    out[6] = cofactor21 * determinant;
    out[7] = 0.0f;
    out[8] = cofactor02 * determinant;
    out[9] = cofactor12 * determinant;
    out[10] = cofactor22 * determinant;
    out[11] = 0.0f;

    tx = matrix[12];
    ty = matrix[13];
    tz = matrix[14];
    out[12] = -(out[0] * tx + out[4] * ty + out[8] * tz);
    out[13] = -(out[1] * tx + out[5] * ty + out[9] * tz);
    out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
    out[15] = 1.0f;
    return 1;
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

static float quat_dot(const GLfloat *a, const GLfloat *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static void matrix_to_quaternion(GLfloat *quat, const GLfloat *matrix) {
    GLfloat trace = matrix[0] + matrix[5] + matrix[10];

    if(trace > 0.0f) {
        GLfloat scale = sqrtf(trace + 1.0f) * 2.0f;

        quat[3] = 0.25f * scale;
        quat[0] = (matrix[6] - matrix[9]) / scale;
        quat[1] = (matrix[8] - matrix[2]) / scale;
        quat[2] = (matrix[1] - matrix[4]) / scale;
    }
    else if(matrix[0] > matrix[5] && matrix[0] > matrix[10]) {
        GLfloat scale = sqrtf(1.0f + matrix[0] - matrix[5] - matrix[10]) * 2.0f;

        quat[3] = (matrix[6] - matrix[9]) / scale;
        quat[0] = 0.25f * scale;
        quat[1] = (matrix[4] + matrix[1]) / scale;
        quat[2] = (matrix[8] + matrix[2]) / scale;
    }
    else if(matrix[5] > matrix[10]) {
        GLfloat scale = sqrtf(1.0f + matrix[5] - matrix[0] - matrix[10]) * 2.0f;

        quat[3] = (matrix[8] - matrix[2]) / scale;
        quat[0] = (matrix[4] + matrix[1]) / scale;
        quat[1] = 0.25f * scale;
        quat[2] = (matrix[9] + matrix[6]) / scale;
    }
    else {
        GLfloat scale = sqrtf(1.0f + matrix[10] - matrix[0] - matrix[5]) * 2.0f;

        quat[3] = (matrix[1] - matrix[4]) / scale;
        quat[0] = (matrix[8] + matrix[2]) / scale;
        quat[1] = (matrix[9] + matrix[6]) / scale;
        quat[2] = 0.25f * scale;
    }

    quat_normalize(quat);
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

static void decompose_transform_matrix(K3DTransform *transform, const GLfloat *matrix) {
    GLfloat rotationMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    GLfloat scaleX;
    GLfloat scaleY;
    GLfloat scaleZ;

    transform->translation[0] = matrix[12];
    transform->translation[1] = matrix[13];
    transform->translation[2] = matrix[14];

    scaleX = sqrtf(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
    scaleY = sqrtf(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
    scaleZ = sqrtf(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10]);

    transform->scale[0] = scaleX > 0.00001f ? scaleX : 1.0f;
    transform->scale[1] = scaleY > 0.00001f ? scaleY : 1.0f;
    transform->scale[2] = scaleZ > 0.00001f ? scaleZ : 1.0f;

    if(scaleX > 0.00001f) {
        rotationMatrix[0] = matrix[0] / scaleX;
        rotationMatrix[1] = matrix[1] / scaleX;
        rotationMatrix[2] = matrix[2] / scaleX;
    }

    if(scaleY > 0.00001f) {
        rotationMatrix[4] = matrix[4] / scaleY;
        rotationMatrix[5] = matrix[5] / scaleY;
        rotationMatrix[6] = matrix[6] / scaleY;
    }

    if(scaleZ > 0.00001f) {
        rotationMatrix[8] = matrix[8] / scaleZ;
        rotationMatrix[9] = matrix[9] / scaleZ;
        rotationMatrix[10] = matrix[10] / scaleZ;
    }

    matrix_to_quaternion(transform->rotation, rotationMatrix);
}

static int build_bind_pose_transforms(K3DAnimationPlayer *player) {
    uint16_t boneIndex;

    if(!player || !player->skeleton || !player->bindPoseTransforms) {
        return 0;
    }

    for(boneIndex = 0; boneIndex < player->skeleton->boneCount; ++boneIndex) {
        GLfloat bindWorld[16];
        GLfloat bindLocal[16];
        int16_t parentIndex = player->skeleton->bones[boneIndex].parentIndex;

        if(!invert_affine_matrix(bindWorld,
                                 player->skeleton->bones[boneIndex].inverseBind)) {
            return 0;
        }

        if(parentIndex >= 0 && parentIndex < (int16_t)player->skeleton->boneCount) {
            mat4_multiply(bindLocal,
                          player->skeleton->bones[parentIndex].inverseBind,
                          bindWorld);
        }
        else {
            memcpy(bindLocal, bindWorld, sizeof(bindLocal));
        }

        decompose_transform_matrix(&player->bindPoseTransforms[boneIndex], bindLocal);
    }

    return 1;
}

static void accumulate_weighted_transform(const K3DTransform *transform, float weight,
                                          GLfloat *translationSum, GLfloat *scaleSum,
                                          GLfloat *rotationSum, GLfloat *referenceRotation,
                                          int *hasRotation) {
    GLfloat adjustedRotation[4];
    int component;

    if(weight <= 0.0001f) {
        return;
    }

    for(component = 0; component < 3; ++component) {
        translationSum[component] += transform->translation[component] * weight;
        scaleSum[component] += transform->scale[component] * weight;
    }

    memcpy(adjustedRotation, transform->rotation, sizeof(adjustedRotation));
    if(!*hasRotation) {
        memcpy(referenceRotation, adjustedRotation, sizeof(adjustedRotation));
        *hasRotation = 1;
    }
    else if(quat_dot(referenceRotation, adjustedRotation) < 0.0f) {
        adjustedRotation[0] = -adjustedRotation[0];
        adjustedRotation[1] = -adjustedRotation[1];
        adjustedRotation[2] = -adjustedRotation[2];
        adjustedRotation[3] = -adjustedRotation[3];
    }

    rotationSum[0] += adjustedRotation[0] * weight;
    rotationSum[1] += adjustedRotation[1] * weight;
    rotationSum[2] += adjustedRotation[2] * weight;
    rotationSum[3] += adjustedRotation[3] * weight;
}

static void sample_blended_skeletal_transform(K3DTransform *out,
                                              const K3DAnimationPlayer *player,
                                              uint16_t boneIndex) {
    GLfloat translationSum[3] = {0.0f, 0.0f, 0.0f};
    GLfloat scaleSum[3] = {0.0f, 0.0f, 0.0f};
    GLfloat rotationSum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLfloat referenceRotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clipWeightScale = 1.0f;
    float weightedClipSum = 0.0f;
    float residualWeight;
    int hasRotation = 0;
    uint32_t index;
    int component;

    for(index = 0; index < player->animationCount; ++index) {
        const K3DAnimationRecord *record = &player->animations[index];

        if(skeletal_record_is_active(player, record) && record->clip.skeletal) {
            weightedClipSum += record->state.weight;
        }
    }

    if(weightedClipSum > 1.0f) {
        clipWeightScale = 1.0f / weightedClipSum;
    }

    for(index = 0; index < player->animationCount; ++index) {
        const K3DAnimationRecord *record = &player->animations[index];
        K3DTransform sampledTransform;

        if(!skeletal_record_is_active(player, record) || !record->clip.skeletal) {
            continue;
        }

        sample_skeletal_transform(&sampledTransform, record->clip.skeletal, boneIndex,
                                  record->state.timeSeconds);
        accumulate_weighted_transform(&sampledTransform,
                                      record->state.weight * clipWeightScale,
                                      translationSum, scaleSum, rotationSum,
                                      referenceRotation, &hasRotation);
    }

    residualWeight = 1.0f - weightedClipSum * clipWeightScale;
    if(residualWeight > 0.0001f && player->bindPoseTransforms) {
        accumulate_weighted_transform(&player->bindPoseTransforms[boneIndex], residualWeight,
                                      translationSum, scaleSum, rotationSum,
                                      referenceRotation, &hasRotation);
    }

    for(component = 0; component < 3; ++component) {
        out->translation[component] = translationSum[component];
        out->scale[component] = scaleSum[component];
    }

    if(hasRotation) {
        memcpy(out->rotation, rotationSum, sizeof(rotationSum));
        quat_normalize(out->rotation);
    }
    else {
        out->rotation[0] = 0.0f;
        out->rotation[1] = 0.0f;
        out->rotation[2] = 0.0f;
        out->rotation[3] = 1.0f;
    }
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

static void update_playback_state(PlaybackState *state, float deltaSeconds, float durationSeconds) {
    if(!state->playing) {
        return;
    }

    state->timeSeconds += deltaSeconds;

    if(state->fadeTargetWeight != state->weight) {
        if(state->fadeDurationSeconds <= 0.0f) {
            state->weight = state->fadeTargetWeight;
        }
        else {
            float alpha;

            state->fadeElapsedSeconds += deltaSeconds;
            alpha = clamp01(state->fadeElapsedSeconds / state->fadeDurationSeconds);
            state->weight = state->fadeStartWeight +
                (state->fadeTargetWeight - state->fadeStartWeight) * alpha;
        }
    }

    if(durationSeconds <= 0.0f) {
        if(state->fadeTargetWeight <= 0.0f && state->weight <= 0.0001f) {
            reset_playback_state(state);
        }
        return;
    }

    while(state->timeSeconds > durationSeconds) {
        state->timeSeconds -= durationSeconds;
    }

    if(state->fadeTargetWeight <= 0.0f && state->weight <= 0.0001f) {
        reset_playback_state(state);
        return;
    }

    if(state->fadeTargetWeight > 0.0f && state->weight > 0.9999f) {
        state->weight = 1.0f;
    }
}

static void apply_vertex_animation(K3DAnimationPlayer *player) {
    K3DAnimationRecord *record;
    uint32_t vertexIndex;
    float weight = 0.0f;

    if(!player->mesh || !player->morphedVertices) {
        return;
    }

    copy_vertex_data(player->morphedVertices, player->mesh->vertices,
                     player->mesh->vertexCount);

    record = find_active_animation_by_type(player, K3D_ANIMATION_TYPE_VERTEX);
    if(!record || !record->clip.vertex ||
       record->clip.vertex->vertexCount != player->mesh->vertexCount) {
        return;
    }

    weight = clamp01(record->state.value);
    if(weight <= 0.0001f) {
        return;
    }

    for(vertexIndex = 0; vertexIndex < player->mesh->vertexCount; ++vertexIndex) {
        uint32_t base = vertexIndex * 3;
        player->morphedVertices[base + 0] += record->clip.vertex->deltas[base + 0] * weight;
        player->morphedVertices[base + 1] += record->clip.vertex->deltas[base + 1] * weight;
        player->morphedVertices[base + 2] += record->clip.vertex->deltas[base + 2] * weight;
    }
}

static void apply_skeletal_animation(K3DAnimationPlayer *player) {
    uint16_t boneIndex;
    uint32_t vertexIndex;
    int hasActiveSkeletal = 0;

    if(!player->mesh || !player->animatedVertices || !player->morphedVertices) {
        return;
    }

    copy_vertex_data(player->animatedVertices, player->morphedVertices,
                     player->mesh->vertexCount);
    if(player->animatedNormals && player->mesh->normals) {
        copy_vertex_data(player->animatedNormals, player->mesh->normals,
                         player->mesh->vertexCount);
    }

    if(!player->skeleton || !player->boneMatrices || !player->boneNormalMatrices ||
       !player->worldMatrices || !player->bindPoseTransforms ||
       player->skeleton->vertexCount != player->mesh->vertexCount) {
        return;
    }

    for(vertexIndex = 0; vertexIndex < player->animationCount; ++vertexIndex) {
        K3DAnimationRecord *record = &player->animations[vertexIndex];

        if(record->info.type != K3D_ANIMATION_TYPE_SKELETAL) {
            continue;
        }

        if(record->clip.skeletal && record->clip.skeletal->boneCount != player->skeleton->boneCount) {
            return;
        }

        if(skeletal_record_is_active(player, record)) {
            hasActiveSkeletal = 1;
        }
    }

    if(!hasActiveSkeletal) {
        return;
    }

    for(boneIndex = 0; boneIndex < player->skeleton->boneCount; ++boneIndex) {
        K3DTransform sampledTransform;
        GLfloat localMatrix[16];
        GLfloat *worldMatrix = &player->worldMatrices[boneIndex * 16];
        GLfloat *skinMatrix = &player->boneMatrices[boneIndex * 16];
        int16_t parentIndex = player->skeleton->bones[boneIndex].parentIndex;

        sample_blended_skeletal_transform(&sampledTransform, player, boneIndex);
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

K3DAnimationPlayer *k3d_animation_player_create(K3DMesh *mesh,
                                                K3DSkeleton *skeleton) {
    K3DAnimationPlayer *player = (K3DAnimationPlayer *)calloc(1, sizeof(K3DAnimationPlayer));

    if(!player || !mesh) {
        free(player);
        return NULL;
    }

    if(skeleton && skeleton->vertexCount != mesh->vertexCount) {
        free(player);
        return NULL;
    }

    player->mesh = mesh;
    player->skeleton = skeleton;
    player->skeletalBlendEnabled = 0;
    player->skeletalBlendDurationSeconds = 0.25f;
    return player;
}

K3DAnimation *k3d_animation_player_add_skeletal(K3DAnimationPlayer *player,
                                                const char *name,
                                                K3DSkeletalAnimation *animation) {
    K3DAnimation *handle;
    K3DAnimationRecord *record;

    if(!player || !player->mesh || !player->skeleton || !animation) {
        return NULL;
    }

    if(player->skeleton->vertexCount != player->mesh->vertexCount ||
       player->skeleton->boneCount != animation->boneCount) {
        printf("K3D Warning: rejected skeletal animation '%s' due to mesh or bone count mismatch\n",
               name ? name : "animation");
        return NULL;
    }

    if(!ensure_skeletal_buffers(player)) {
        return NULL;
    }

    if(!build_bind_pose_transforms(player)) {
        return NULL;
    }

    handle = add_animation_record(player, name, K3D_ANIMATION_TYPE_SKELETAL);
    if(!handle) {
        return NULL;
    }

    record = find_animation_record(player, handle);
    record->clip.skeletal = animation;
    return handle;
}

K3DAnimation *k3d_animation_player_add_vertex(K3DAnimationPlayer *player,
                                              const char *name,
                                              K3DVertexAnimation *animation) {
    K3DAnimation *handle;
    K3DAnimationRecord *record;

    if(!player || !player->mesh || !animation) {
        return NULL;
    }

    if(animation->vertexCount != player->mesh->vertexCount) {
        printf("K3D Warning: rejected vertex animation '%s' due to vertex count mismatch\n",
               name ? name : "animation");
        return NULL;
    }

    if(!ensure_base_buffers(player)) {
        return NULL;
    }

    handle = add_animation_record(player, name, K3D_ANIMATION_TYPE_VERTEX);
    if(!handle) {
        return NULL;
    }

    record = find_animation_record(player, handle);
    record->clip.vertex = animation;
    return handle;
}

const K3DAnimation *k3d_animation_player_find(const K3DAnimationPlayer *player,
                                              const char *name) {
    K3DAnimationRecord *record = find_animation_record_by_name(player, name);

    return record ? &record->info : NULL;
}

void k3d_animation_player_free(K3DAnimationPlayer *player) {
    if(!player) {
        return;
    }

    free_player_buffers(player);
    free_vertex_runtime(player);
    free_skeletal_runtime(player);
    free_animation_records(player);
    free(player);
}

void k3d_animation_player_update(K3DAnimationPlayer *player, float deltaSeconds) {
    uint32_t index;

    if(!player) {
        return;
    }

    if(player->morphedVertices) {
        sync_base_pose_buffers(player);
    }

    for(index = 0; index < player->animationCount; ++index) {
        K3DAnimationRecord *record = &player->animations[index];
        float durationSeconds = 0.0f;

        if(record->info.type == K3D_ANIMATION_TYPE_SKELETAL && record->clip.skeletal) {
            durationSeconds = animation_duration_seconds(record->clip.skeletal->frameCount,
                                                         record->clip.skeletal->fps);
            update_playback_state(&record->state, deltaSeconds, durationSeconds);
        }
    }

    apply_vertex_animation(player);
    apply_skeletal_animation(player);
}

void k3d_animation_player_render(const K3DAnimationPlayer *player) {
    const GLfloat *vertexData;
    const GLfloat *normalData;

    if(!player || !player->mesh) {
        return;
    }

    if(player->boneMatrices && player->animatedVertices) {
        vertexData = player->animatedVertices;
    }
    else if(player->morphedVertices) {
        vertexData = player->morphedVertices;
    }
    else {
        vertexData = player->mesh->vertices;
    }

    normalData = player->animatedNormals ? player->animatedNormals : player->mesh->normals;
    k3d_render_override(player->mesh, vertexData, normalData);
}

void k3d_animation_player_play(K3DAnimationPlayer *player,
                               const K3DAnimation *animation) {
    K3DAnimationRecord *record = find_animation_record(player, animation);
    float durationSeconds;

    if(!animation_record_is_loaded(player, record)) {
        return;
    }

    if(animation->type == K3D_ANIMATION_TYPE_SKELETAL) {
        durationSeconds = get_skeletal_blend_duration(player);
        fade_other_skeletal_animations(player, animation, durationSeconds);
        start_skeletal_playback(&record->state,
                                durationSeconds > 0.0f ? 0.0f : 1.0f,
                                1.0f, durationSeconds);
        printf("%s started\n", record->info.name ? record->info.name : "animation");
        return;
    }

    stop_other_animations_of_type(player, animation);
    start_playback_state(&record->state, record->info.name ? record->info.name : "animation");

    if(animation->type == K3D_ANIMATION_TYPE_VERTEX) {
        record->state.value = 1.0f;
    }
}

void k3d_animation_player_stop(K3DAnimationPlayer *player,
                               const K3DAnimation *animation) {
    K3DAnimationRecord *record = find_animation_record(player, animation);
    float durationSeconds;

    if(!animation_record_is_loaded(player, record)) {
        return;
    }

    if(animation->type == K3D_ANIMATION_TYPE_SKELETAL) {
        durationSeconds = get_skeletal_blend_duration(player);
        if(durationSeconds > 0.0f && playback_is_active(&record->state)) {
            fade_skeletal_playback(&record->state, 0.0f, durationSeconds);
        }
        else {
            stop_playback_state(&record->state,
                                record->info.name ? record->info.name : "animation");
        }
        return;
    }

    stop_playback_state(&record->state, record->info.name ? record->info.name : "animation");
}

void k3d_animation_player_toggle(K3DAnimationPlayer *player,
                                 const K3DAnimation *animation) {
    K3DAnimationRecord *record = find_animation_record(player, animation);

    if(!animation_record_is_loaded(player, record)) {
        return;
    }

    if(animation->type == K3D_ANIMATION_TYPE_VERTEX) {
        if(record->state.value > 0.5f) {
            k3d_animation_player_stop(player, animation);
        }
        else {
            k3d_animation_player_play(player, animation);
        }
        return;
    }

    if(record->state.weight > 0.5f || record->state.fadeTargetWeight > 0.5f) {
        k3d_animation_player_stop(player, animation);
        return;
    }

    k3d_animation_player_play(player, animation);
}

void k3d_animation_player_set_value(K3DAnimationPlayer *player,
                                    const K3DAnimation *animation,
                                    float value) {
    K3DAnimationRecord *record = find_animation_record(player, animation);

    if(!animation_record_is_loaded(player, record) ||
       animation->type != K3D_ANIMATION_TYPE_VERTEX) {
        return;
    }

    stop_other_animations_of_type(player, animation);
    record->state.value = clamp01(value);
}

void k3d_animation_player_set_skeletal_blending(K3DAnimationPlayer *player,
                                                int enabled) {
    if(!player) {
        return;
    }

    player->skeletalBlendEnabled = enabled ? 1 : 0;
}

int k3d_animation_player_get_skeletal_blending(const K3DAnimationPlayer *player) {
    if(!player) {
        return 0;
    }

    return player->skeletalBlendEnabled;
}

void k3d_animation_player_set_skeletal_blend_duration(K3DAnimationPlayer *player,
                                                      float durationSeconds) {
    if(!player) {
        return;
    }

    player->skeletalBlendDurationSeconds = durationSeconds > 0.0f ? durationSeconds : 0.0f;
}

float k3d_animation_player_get_skeletal_blend_duration(const K3DAnimationPlayer *player) {
    if(!player) {
        return 0.0f;
    }

    return player->skeletalBlendDurationSeconds;
}

void k3d_animation_player_reset(K3DAnimationPlayer *player,
                                const K3DAnimation *animation) {
    K3DAnimationRecord *record = find_animation_record(player, animation);

    if(!animation_record_is_loaded(player, record)) {
        return;
    }

    reset_playback_state(&record->state);
}