#ifndef __K3D_ANIMATION_H
#define __K3D_ANIMATION_H

#include "k3d.h"

typedef struct K3DAnimationPlayer K3DAnimationPlayer;

typedef enum {
    K3D_ANIMATION_TYPE_SKELETAL = 0,
    K3D_ANIMATION_TYPE_VERTEX = 1,
} K3DAnimationType;

typedef struct {
    const char *name;
    K3DAnimationType type;
} K3DAnimation;

K3DAnimationPlayer *k3d_animation_player_create(K3DMesh *mesh,
                                                K3DSkeleton *skeleton);

K3DAnimation *k3d_animation_player_add_skeletal(K3DAnimationPlayer *player,
                                                const char *name,
                                                K3DSkeletalAnimation *animation);

K3DAnimation *k3d_animation_player_add_vertex(K3DAnimationPlayer *player,
                                              const char *name,
                                              K3DVertexAnimation *animation);

const K3DAnimation *k3d_animation_player_find(const K3DAnimationPlayer *player,
                                              const char *name);

void k3d_animation_player_free(K3DAnimationPlayer *player);

void k3d_animation_player_update(K3DAnimationPlayer *player, float deltaSeconds);

void k3d_animation_player_render(const K3DAnimationPlayer *player);

void k3d_animation_player_play(K3DAnimationPlayer *player,
                               const K3DAnimation *animation);

void k3d_animation_player_stop(K3DAnimationPlayer *player,
                               const K3DAnimation *animation);

void k3d_animation_player_toggle(K3DAnimationPlayer *player,
                                 const K3DAnimation *animation);

void k3d_animation_player_reset(K3DAnimationPlayer *player,
                                const K3DAnimation *animation);

#endif /* __K3D_ANIMATION_H */