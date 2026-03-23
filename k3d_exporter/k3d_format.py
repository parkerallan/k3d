"""
K3D Binary Format Writer
(c)2026 KallistiOS

Utilities for writing K3D binary 3D model files.
"""

import struct

# K3D Format Constants
K3D_MAGIC = b'K3D\x00'
K3D_VERSION = 1

# Feature Flags
K3D_HAS_NORMALS = 0x0001
K3D_HAS_UVS = 0x0002
K3D_HAS_COLORS = 0x0004


def write_k3d_header(file, vertex_count, index_count, flags, primitive_type=0):
    """
    Write K3D file header (16 bytes).
    
    Args:
        file: Binary file object opened for writing
        vertex_count: Number of unique vertices (uint32)
        index_count: Number of indices (uint32)
        flags: Feature flags bitfield (uint16)
        primitive_type: Primitive type (0=triangles, 1=quads) (uint8)
    """
    # Magic: 4 bytes "K3D\0"
    file.write(K3D_MAGIC)
    
    # Version: uint16 (little-endian)
    file.write(struct.pack('<H', K3D_VERSION))
    
    # Flags: uint16 (little-endian)
    file.write(struct.pack('<H', flags))
    
    # Primitive type: uint8
    file.write(struct.pack('<B', primitive_type))
    
    # Reserved: uint8 (padding)
    file.write(struct.pack('<B', 0))
    
    # Vertex count: uint32 (little-endian)
    file.write(struct.pack('<I', vertex_count))
    
    # Index count: uint32 (little-endian)
    file.write(struct.pack('<I', index_count))


def write_vertex_data(file, vertices):
    """
    Write vertex position data (x, y, z) as floats.
    
    Args:
        file: Binary file object
        vertices: List of (x, y, z) tuples
    """
    for v in vertices:
        file.write(struct.pack('<fff', v[0], v[1], v[2]))


def write_normal_data(file, normals):
    """
    Write vertex normal data (nx, ny, nz) as floats.
    
    Args:
        file: Binary file object
        normals: List of (nx, ny, nz) tuples
    """
    for n in normals:
        file.write(struct.pack('<fff', n[0], n[1], n[2]))


def write_uv_data(file, uvs):
    """
    Write texture coordinate data (u, v) as floats.
    
    Args:
        file: Binary file object
        uvs: List of (u, v) tuples
    """
    for uv in uvs:
        file.write(struct.pack('<ff', uv[0], uv[1]))


def write_indices(file, indices):
    """
    Write triangle indices as uint16 values.
    
    Args:
        file: Binary file object
        indices: List of vertex indices (uint16)
    """
    for idx in indices:
        file.write(struct.pack('<H', idx))


def calculate_flags(has_normals, has_uvs, has_colors=False):
    """
    Calculate feature flags bitfield.
    
    Args:
        has_normals: Boolean, mesh has normal data
        has_uvs: Boolean, mesh has UV data
        has_colors: Boolean, mesh has vertex colors (reserved)
    
    Returns:
        uint16 flags value
    """
    flags = 0
    if has_normals:
        flags |= K3D_HAS_NORMALS
    if has_uvs:
        flags |= K3D_HAS_UVS
    if has_colors:
        flags |= K3D_HAS_COLORS
    return flags
