# K3D for KallistiOS KGL

K3D is a simple 3D asset library for KallistiOS projects using KGL. In this repo it shows up in three parts:

- A binary model and animation format
- A C runtime library for loading and playing K3D assets in KallistiOS KGL applications
- A Blender exporter for writing mesh, skeleton, and animation files

Also includes an example with assets so you can see the whole thing working together  
<img src="https://github.com/user-attachments/assets/619ab46e-366e-4b4a-a054-523bf8107ccc" >

## Runtime Library

The runtime code lives under [example](example). It handles loading, animation playback, and rendering.

Key files:

- [example/k3d.h](example/k3d.h): core format types, constants, and runtime declarations
- [example/k3d_loader.c](example/k3d_loader.c): binary loaders for meshes, skeletons, skeletal clips, and vertex clips
- [example/k3d_animation.h](example/k3d_animation.h): animation player API
- [example/k3d_animation.c](example/k3d_animation.c): skeletal blending, vertex morph application, and playback state management
- [example/test.c](example/test.c): sample KGL application using the runtime

At a high level, the runtime flow looks like this:

1. Load a mesh from a `.k3d` file.
2. Load a matching skeleton from a `.k3sk` file if the model is skinned.
3. Load skeletal clips from `.k3sa` files and morph clips from `.k3va` files.
4. Register those clips with the animation player.
5. Update the animation player each frame and render the animated mesh.

## Blender Exporter

The Blender exporter lives in [k3d_exporter](k3d_exporter) and writes files that match the runtime loader.

- [k3d_exporter/__init__.py](k3d_exporter/__init__.py): Blender addon registration and metadata
- [k3d_exporter/export_k3d.py](k3d_exporter/export_k3d.py): export operator and mesh/action/shapekey export workflow
- [k3d_exporter/k3d_format.py](k3d_exporter/k3d_format.py): binary format writing helpers

The exporter targets Blender 2.80 or newer and shows up in Blender's export menu as `K3D (.k3d)`.

K3D uses a few related file types exported from Blender:

- `.k3d`: mesh data
- `.k3sk`: skeleton data
- `.k3sa`: skeletal animation clips from your Blender actions
- `.k3va`: vertex animation clips from your Blender shapekeys

This keeps the base mesh separate from animation data, but you can blend clips or play them at the same time as needed.

## Example Asset Pipeline

The sample assets in [romdisk](romdisk) show the intended layout:

- [romdisk/char-model.k3d](romdisk/char-model.k3d): base character mesh
- [romdisk/char-model.k3sk](romdisk/char-model.k3sk): character skeleton
- [romdisk/char-model_Idle.k3sa](romdisk/char-model_Idle.k3sa): idle skeletal clip
- [romdisk/char-model_Talking.k3sa](romdisk/char-model_Talking.k3sa): talking skeletal clip
- [romdisk/char-model_Walk.k3sa](romdisk/char-model_Walk.k3sa): walk skeletal clip
- [romdisk/char-model_Blink.k3va](romdisk/char-model_Blink.k3va): blink morph clip
- [romdisk/char-model_Talk.k3va](romdisk/char-model_Talk.k3va): mouth morph clip

This sample uses:

- Skeletal clips which drive large body motion like the movement when standing idle or talking
- Vertex clips which handle localized shape changes with facial expressions such as blinking or mouth movement for talking
