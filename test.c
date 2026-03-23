#include <kos.h>
#include <stdio.h>
#include <stdlib.h>

#include <KGL/gl.h>
#include <KGL/glu.h>
#include <KGL/glut.h>

#include "k3d.h"

static GLfloat xrot;        /* X Rotation */
static GLfloat yrot;        /* Y Rotation */
static GLfloat xspeed;      /* X Rotation Speed */
static GLfloat yspeed;      /* Y Rotation Speed */
static GLfloat z = -5.0f;   /* Depth Into The Screen */

static GLuint  texture;         /* Storage For Texture */

/* Storage For Three Types Of Fog */
GLuint fogType = 0; /* use GL_EXP initially */
GLuint fogMode[] = { GL_EXP, GL_EXP2, GL_LINEAR };
char cfogMode[3][10] = {"GL_EXP   ", "GL_EXP2  ", "GL_LINEAR" };
GLfloat fogColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f }; /* Fog Color */
int fog = GL_TRUE;

/* Load a PVR texture - located in pvr-texture.c */
extern GLuint glTextureLoadPVR(char *fname, unsigned char isMipMapped, unsigned char glMipMap);

/* K3D Mesh pointer */
static K3DMesh *mesh = NULL;

void draw_gl(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, z);

    glRotatef(xrot, 1.0f, 0.0f, 0.0f);
    glRotatef(yrot, 0.0f, 1.0f, 0.0f);

    glBindTexture(GL_TEXTURE_2D, texture);

    /* Render K3D mesh using optimized vertex arrays */
    if(mesh) {
        k3d_render(mesh);
    }

    xrot += xspeed;
    yrot += yspeed;
}

int main(int argc, char **argv) {
    maple_device_t *cont;
    cont_state_t *state;
    GLboolean xp = GL_FALSE;
    GLboolean yp = GL_FALSE;

    /* Get basic stuff initialized */
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
    /* Enable normal normalization to maintain lighting after transformations */
    glEnable(GL_NORMALIZE);
    glColor4f(1.0f, 1.0f, 1.0f, 0.5);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    /* Enable Lighting and GL_LIGHT0 */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    /* Set up the fog */
    glFogi(GL_FOG_MODE, fogMode[fogType]);     /* Fog Mode */
    glFogfv(GL_FOG_COLOR, fogColor);           /* Set Fog Color */
    glFogf(GL_FOG_DENSITY, 0.35f);             /* How Dense The Fog is */
    glHint(GL_FOG_HINT, GL_DONT_CARE);         /* Fog Hint Value */
    glFogf(GL_FOG_START, 0.0f);                /* Fog Start Depth */
    glFogf(GL_FOG_END, 5.0f);                  /* Fog End Depth */
    glEnable(GL_FOG);                          /* Enables GL_FOG */

    /* Set up the textures */
    texture = glTextureLoadPVR("/rd/glass.pvr", 0, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_FILTER, GL_FILTER_BILINEAR);

    /* Load the K3D mesh data */
    mesh = k3d_load("/rd/maid.k3d");
    if(!mesh) {
        printf("Failed to load K3D mesh, exiting\n");
        return 1;
    }

    while(1) {
        cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

        /* Check key status */
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

        /* Switch fog off/on */
        if(fog) {
            glEnable(GL_FOG);
        }
        else {
            glDisable(GL_FOG);
        }

        /* Draw the GL "scene" */
        draw_gl();

        /* Finish the frame */
        glutSwapBuffers();
    }

    /* Cleanup */
    k3d_free(mesh);

    return 0;
}


