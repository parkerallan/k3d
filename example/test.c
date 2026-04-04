#include <kos.h>
#include <stdio.h>
#include <string.h>

#include <KGL/gl.h>
#include <KGL/glu.h>
#include <KGL/glut.h>

#include "font.h"
#include "k3d_animation.h"

static GLfloat yrot;
static GLfloat z = -5.0f;
static GLfloat blinkValue = 0.0f;
static GLfloat fpsDisplay = 0.0f;
static const GLfloat cameraHeight = 1.25f;
static const GLfloat modelPitch = -90.0f;
static const GLfloat modelYawOffset = -15.0f;
static const GLfloat modelTurnStep = 1.0f;
static const GLfloat blinkInterval = 2.0f;
static const GLfloat blinkCloseDuration = 0.08f;
static const GLfloat blinkOpenDuration = 0.12f;
static const GLfloat talkWaveMax = 0.125f;
static const GLfloat talkWaveRate = 3.5f;
static const GLfloat fpsGlyphWidth = 16.0f;
static const GLfloat fpsGlyphHeight = 16.0f;

static GLuint char_tex;
static GLuint font_tex;
static K3DMesh *char_model = NULL;
static K3DSkeleton *skeleton = NULL;
static K3DSkeletalAnimation *idleClip = NULL;
static K3DSkeletalAnimation *talkingClip = NULL;
static K3DVertexAnimation *talkClip = NULL;
static K3DVertexAnimation *blinkClip = NULL;
static K3DAnimationPlayer *player = NULL;
static Font *font = NULL;
static const K3DAnimation *idleAnimation = NULL;
static const K3DAnimation *talkingAnimation = NULL;
static const K3DAnimation *talkAnimation = NULL;
static const K3DAnimation *blinkAnimation = NULL;

GLfloat fogColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

extern GLuint glTextureLoadPVR(char *fname, unsigned char isMipMapped, unsigned char glMipMap);

static GLfloat sample_talk_wave(GLfloat phase) {
    GLfloat normalizedPhase = phase - (GLfloat)((int)phase);

    if(normalizedPhase < 0.5f) {
        return talkWaveMax * (normalizedPhase * 2.0f);
    }

    return talkWaveMax * (2.0f - (normalizedPhase * 2.0f));
}

static void draw_fps_overlay(void) {
    char fpsText[16];
    size_t textLength;
    GLfloat textX;

    if(!font || !font_tex) {
        return;
    }

    snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", fpsDisplay);
    textLength = strlen(fpsText);
    textX = 640.0f - (fpsGlyphWidth * (GLfloat)textLength) - 16.0f;

    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_KOS_NEARZ_CLIPPING);
    glDisable(GL_DEPTH_TEST);

    glBindTexture(GL_TEXTURE_2D, font_tex);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_DST_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glBegin(GL_QUADS);
    font_print_string(font, fpsText, textX, 16.0f, fpsGlyphWidth, fpsGlyphHeight);
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_KOS_NEARZ_CLIPPING);
    glEnable(GL_FOG);
    glEnable(GL_LIGHTING);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

static void draw_gl(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, -cameraHeight, z);

    glRotatef(yrot, 0.0f, 1.0f, 0.0f);

    if(char_model) {
        glPushMatrix();
        glTranslatef(0.0f, 0.0f, 0.0f);
        glRotatef(modelYawOffset, 0.0f, 1.0f, 0.0f);
        glRotatef(modelPitch, 1.0f, 0.0f, 0.0f);
        glBindTexture(GL_TEXTURE_2D, char_tex);
        k3d_animation_player_render(player);
        glPopMatrix();
    }
}

int main(int argc, char **argv) {
    maple_device_t *cont;
    cont_state_t *state;
    uint64_t lastTicks;
    float blinkTimer = blinkInterval;
    float talkPhase = 0.0f;
    int blinkState = 0;
    GLboolean talkingActive = GL_FALSE;
    GLboolean lp = GL_FALSE;

    (void)argc;
    (void)argv;

    glKosInit();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, 640.0f / 480.0f, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClearDepth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_KOS_NEARZ_CLIPPING);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_NORMALIZE);
    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_DENSITY, 0.15f);
    glHint(GL_FOG_HINT, GL_DONT_CARE);
    glFogf(GL_FOG_START, 1.5f);
    glFogf(GL_FOG_END, 7.5f);
    glEnable(GL_FOG);

    char_tex = glTextureLoadPVR("/rd/char.pvr", 0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_FILTER, GL_FILTER_BILINEAR);
    font_tex = glTextureLoadPVR("/rd/FONT0.PVR", 0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_FILTER, GL_LINEAR);
    font = font_init(512.0f, 512.0f, 10, 10,
                     PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

    char_model = k3d_load("/rd/char-model.k3d");
    skeleton = k3d_skeleton_load("/rd/char-model.k3sk");
    idleClip = k3d_skeletal_animation_load("/rd/char-model_Idle.k3sa");
    talkingClip = k3d_skeletal_animation_load("/rd/char-model_Talking.k3sa");
    talkClip = k3d_vertex_animation_load("/rd/char-model_Talk.k3va");
    blinkClip = k3d_vertex_animation_load("/rd/char-model_Blink.k3va");

    player = k3d_animation_player_create(char_model, skeleton);

    idleAnimation = k3d_animation_player_add_skeletal(player, "Idle", idleClip);
    talkingAnimation = k3d_animation_player_add_skeletal(player, "Talking", talkingClip);
    talkAnimation = k3d_animation_player_add_vertex(player, "Talk", talkClip);
    blinkAnimation = k3d_animation_player_add_vertex(player, "Blink", blinkClip);

    // Enable blending
    k3d_animation_player_set_skeletal_blending(player, 1);
    k3d_animation_player_set_skeletal_blend_duration(player, 0.25f);
    k3d_animation_player_play(player, idleAnimation);
    k3d_animation_player_set_value(player, blinkAnimation, blinkValue);

    lastTicks = timer_ms_gettime64();

    while(1) {
        float deltaSeconds;
        uint64_t nowTicks;

        cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        state = (cont_state_t *)maple_dev_status(cont);

        if(!state) {
            printf("Error reading controller\n");
            break;
        }

        if(state->buttons & CONT_START) {
            break;
        }

        nowTicks = timer_ms_gettime64();
        deltaSeconds = (float)(nowTicks - lastTicks) / 1000.0f;
        lastTicks = nowTicks;

        if(deltaSeconds > 0.0f) {
            float currentFps = 1.0f / deltaSeconds;

            if(fpsDisplay <= 0.0f) {
                fpsDisplay = currentFps;
            }
            else {
                fpsDisplay = fpsDisplay * 0.9f + currentFps * 0.1f;
            }
        }

        if(state->buttons & CONT_Y) {
            if(z >= -15.0f) {
                z -= 0.1f;
            }
        }

        if(state->buttons & CONT_B) {
            if(z <= 0.0f) {
                z += 0.1f;
            }
        }

        if(state->buttons & CONT_DPAD_LEFT) {
            yrot -= modelTurnStep;
        }

        if(state->buttons & CONT_DPAD_RIGHT) {
            yrot += modelTurnStep;
        }

        if(blinkState == 0) {
            blinkTimer -= deltaSeconds;
            if(blinkTimer <= 0.0f) {
                blinkState = 1;
                blinkTimer = 0.0f;
            }
        }
        else if(blinkState == 1) {
            blinkTimer += deltaSeconds / blinkCloseDuration;
            if(blinkTimer >= 1.0f) {
                blinkTimer = 1.0f;
                blinkState = 2;
            }
        }
        else {
            blinkTimer -= deltaSeconds / blinkOpenDuration;
            if(blinkTimer <= 0.0f) {
                blinkTimer = blinkInterval;
                blinkState = 0;
            }
        }

        if(blinkState == 0) {
            blinkValue = 0.0f;
        }
        else {
            blinkValue = blinkTimer;
        }

        if(state->buttons & CONT_A) {
            if(!lp) {
                lp = GL_TRUE;
                if(talkingActive) {
                    k3d_animation_player_play(player, idleAnimation);
                    k3d_animation_player_stop(player, talkAnimation);
                    k3d_animation_player_set_value(player, talkAnimation, 0.0f);
                    talkingActive = GL_FALSE;
                }
                else {
                    k3d_animation_player_play(player, talkingAnimation);
                    k3d_animation_player_play(player, talkAnimation);
                    talkPhase = 0.0f;
                    talkingActive = GL_TRUE;
                }
            }
        }
        else {
            lp = GL_FALSE;
        }

        k3d_animation_player_set_value(player, blinkAnimation, blinkValue);

        if(talkingActive) {
            talkPhase += deltaSeconds * talkWaveRate;
            if(talkPhase >= 1.0f) {
                talkPhase -= (float)((int)talkPhase);
            }

            k3d_animation_player_set_value(player, talkAnimation,
                                           sample_talk_wave(talkPhase));
        }
        else {
            k3d_animation_player_set_value(player, talkAnimation, 0.0f);
        }

        // Apply animations
        k3d_animation_player_update(player, deltaSeconds);

        draw_gl();
        draw_fps_overlay();
        glutSwapBuffers();
    }

    k3d_animation_player_free(player);
    font_free(font);
    k3d_vertex_animation_free(blinkClip);
    k3d_vertex_animation_free(talkClip);
    k3d_skeletal_animation_free(talkingClip);
    k3d_skeletal_animation_free(idleClip);
    k3d_skeleton_free(skeleton);
    k3d_free(char_model);
    return 0;
}