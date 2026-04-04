"""
K3D Exporter - Blender Addon

Exports Blender meshes to K3D binary format optimized for Dreamcast/KGL rendering.
The K3D format supports indexed geometry with normals and UV coordinates for
high-performance real-time rendering.
"""

bl_info = {
    "name": "K3D Format (.k3d)",
    "author": "KallistiOS Contributors",
    "version": (1, 0, 0),
    "blender": (2, 80, 0),
    "location": "File > Export > K3D (.k3d)",
    "description": "Export mesh to K3D binary format for KallistiOS/Dreamcast",
    "category": "Import-Export",
    "doc_url": "https://github.com/KallistiOS/KallistiOS",
}

# Import/export addon functionality
if "bpy" in locals():
    import importlib
    if "export_k3d" in locals():
        importlib.reload(export_k3d)
    if "k3d_format" in locals():
        importlib.reload(k3d_format)
else:
    from . import export_k3d
    from . import k3d_format

import bpy


def register():
    """Register addon with Blender"""
    export_k3d.register()


def unregister():
    """Unregister addon from Blender"""
    export_k3d.unregister()


if __name__ == "__main__":
    register()
