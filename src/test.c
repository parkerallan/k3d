#include <kos.h>
#include <stdio.h>

#include <KGL/gl.h>
#include <KGL/glu.h>
#include <KGL/glut.h>

#include "k3d_animation.h"

static GLfloat xrot;
static GLfloat yrot;
static GLfloat xspeed;
static GLfloat yspeed;
static GLfloat z = -5.0f;

static GLuint texture;
static K3DAnimationPlayer *player = NULL;
static const K3DAnimation *skeletalAnimation = NULL;
static const K3DAnimation *vertexAnimation = NULL;

GLuint fogType = 0;
GLuint fogMode[] = { GL_EXP, GL_EXP2, GL_LINEAR };
char cfogMode[3][10] = {"GL_EXP   ", "GL_EXP2  ", "GL_LINEAR" };
GLfloat fogColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
int fog = GL_TRUE;

extern GLuint glTextureLoadPVR(char *fname, unsigned char isMipMapped, unsigned char glMipMap);

static void draw_gl(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, z);

    glRotatef(xrot, 1.0f, 0.0f, 0.0f);
    glRotatef(yrot, 0.0f, 1.0f, 0.0f);

    glBindTexture(GL_TEXTURE_2D, texture);
    k3d_animation_player_render(player);

    xrot += xspeed;
    yrot += yspeed;
}

int main(int argc, char **argv) {
    maple_device_t *cont;
    cont_state_t *state;
    uint64_t lastTicks;
    GLboolean xp = GL_FALSE;
    GLboolean yp = GL_FALSE;
    GLboolean lp = GL_FALSE;
    GLboolean rp = GL_FALSE;

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
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_NORMALIZE);
    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    glFogi(GL_FOG_MODE, fogMode[fogType]);
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_DENSITY, 0.35f);
    glHint(GL_FOG_HINT, GL_DONT_CARE);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 5.0f);
    glEnable(GL_FOG);

    texture = glTextureLoadPVR("/rd/glass.pvr", 0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_FILTER, GL_FILTER_BILINEAR);

    player = k3d_animation_player_load("/rd/ball.k3d", "/rd/ball.k3sk",
                                       "/rd/ball_Bounce.k3sa", "/rd/ball_Spike.k3va");
    if(!player) {
        printf("Failed to load animated K3D mesh, exiting\n");
        return 1;
    }

    skeletalAnimation = k3d_animation_player_get_animation(player, 0);
    vertexAnimation = k3d_animation_player_get_animation(player, 1);

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

        if(state->buttons & CONT_START)
            break;

        if(state->buttons & CONT_A) {
            if(z >= -15.0f) z -= 0.02f;
        }

        if(state->buttons & CONT_B) {
            if(z <= 0.0f) z += 0.02f;
        }

        if((state->buttons & CONT_X) && !xp) {
            xp = GL_TRUE;
            fogType = (fogType + 1) % 3;
            glFogi(GL_FOG_MODE, fogMode[fogType]);
            printf("%s\n", cfogMode[fogType]);
        }

        if(!(state->buttons & CONT_X))
            xp = GL_FALSE;

        if((state->buttons & CONT_Y) && !yp) {
            yp = GL_TRUE;
            fog = !fog;
        }

        if(!(state->buttons & CONT_Y))
            yp = GL_FALSE;

        if(state->buttons & CONT_DPAD_UP)
            xspeed -= 0.01f;

        if(state->buttons & CONT_DPAD_DOWN)
            xspeed += 0.01f;

        if(state->buttons & CONT_DPAD_LEFT)
            yspeed -= 0.01f;

        if(state->buttons & CONT_DPAD_RIGHT)
            yspeed += 0.01f;

        if(state->ltrig > 0x7f) {
            if(!lp) {
                lp = GL_TRUE;
                k3d_animation_player_toggle(player, skeletalAnimation);
            }
        }
        else {
            lp = GL_FALSE;
        }

        if(state->rtrig > 0x7f) {
            if(!rp) {
                rp = GL_TRUE;
                k3d_animation_player_toggle(player, vertexAnimation);
            }
        }
        else {
            rp = GL_FALSE;
        }

        nowTicks = timer_ms_gettime64();
        deltaSeconds = (float)(nowTicks - lastTicks) / 1000.0f;
        lastTicks = nowTicks;
        k3d_animation_player_update(player, deltaSeconds);

        if(fog) {
            glEnable(GL_FOG);
        }
        else {
            glDisable(GL_FOG);
        }

        draw_gl();
        glutSwapBuffers();
    }

    k3d_animation_player_free(player);
    return 0;
}


