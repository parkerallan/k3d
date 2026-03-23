"""
K3D Exporter for Blender
(c)2026 KallistiOS

Exports Blender meshes to K3D binary format optimized for Dreamcast/KGL.
"""

import bpy
import bmesh
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty
from . import k3d_format


class ExportK3D(bpy.types.Operator, ExportHelper):
    """Export mesh to K3D binary format"""
    bl_idname = "export_mesh.k3d"
    bl_label = "Export K3D"
    bl_options = {'PRESET'}
    
    filename_ext = ".k3d"
    
    filter_glob: StringProperty(
        default="*.k3d",
        options={'HIDDEN'},
    )
    
    use_selection: BoolProperty(
        name="Selection Only",
        description="Export selected objects only",
        default=False,
    )
    
    export_normals: BoolProperty(
        name="Export Normals",
        description="Include vertex normals in export",
        default=True,
    )
    
    export_uvs: BoolProperty(
        name="Export UVs",
        description="Include texture coordinates in export",
        default=True,
    )
    
    def execute(self, context):
        """Main export function"""
        return self.export_k3d(context, self.filepath)
    
    def export_k3d(self, context, filepath):
        """
        Export selected/all meshes to K3D format.
        
        Args:
            context: Blender context
            filepath: Output file path
        
        Returns:
            {'FINISHED'} on success, {'CANCELLED'} on failure
        """
        # Get objects to export
        if self.use_selection:
            objects = [obj for obj in context.selected_objects if obj.type == 'MESH']
        else:
            objects = [obj for obj in context.scene.objects if obj.type == 'MESH']
        
        if not objects:
            self.report({'ERROR'}, "No mesh objects to export")
            return {'CANCELLED'}
        
        if len(objects) > 1:
            self.report({'WARNING'}, 
                       f"Multiple meshes found. Only exporting first mesh: {objects[0].name}")
        
        # Export the first mesh
        obj = objects[0]
        
        try:
            # Create a temporary mesh with modifiers applied
            depsgraph = context.evaluated_depsgraph_get()
            obj_eval = obj.evaluated_get(depsgraph)
            mesh = obj_eval.to_mesh()
            
            # Calculate split normals for proper shading
            mesh.calc_normals_split()
            
            # Extract mesh data
            vertices, normals, uvs, indices, primitive_type = self.extract_mesh_data(mesh)
            
            # Write K3D file
            self.write_k3d_file(filepath, vertices, normals, uvs, indices, primitive_type)
            
            # Cleanup
            obj_eval.to_mesh_clear()
            
            prim_name = "quads" if primitive_type == 1 else "triangles"
            prim_count = len(indices) // 4 if primitive_type == 1 else len(indices) // 3
            self.report({'INFO'}, 
                       f"Exported {len(vertices)} vertices, {prim_count} {prim_name} to {filepath}")
            return {'FINISHED'}
            
        except Exception as e:
            self.report({'ERROR'}, f"Export failed: {str(e)}")
            import traceback
            traceback.print_exc()
            return {'CANCELLED'}
    
    def extract_mesh_data(self, mesh):
        """
        Extract vertex, normal, UV, and index data from Blender mesh.
        Respects Blender's Shade Flat/Smooth shading:
          - Shade Smooth: Uses vertex normals (averaged from connected faces)
          - Shade Flat: Uses polygon face normals
        NO vertex deduplication to avoid shared-vertex lighting artifacts.
        
        Args:
            mesh: Blender mesh object
        
        Returns:
            Tuple of (vertices, normals, uvs, indices, primitive_type)
        """
        import math
        
        vertices = []
        normals = []
        uvs = []
        indices = []
        
        next_index = 0
        
        def normalize(v):
            length = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
            if length > 0.0001:
                return (v[0]/length, v[1]/length, v[2]/length)
            return (0.0, 0.0, 1.0)
        
        # Get UV layer
        uv_layer = None
        if self.export_uvs and mesh.uv_layers.active:
            uv_layer = mesh.uv_layers.active.data
        
        # Auto-detect primitive type and shading mode from geometry
        all_tris = all(len(poly.vertices) == 3 for poly in mesh.polygons)
        all_quads = all(len(poly.vertices) == 4 for poly in mesh.polygons)
        
        # Detect if mesh uses smooth or flat shading from Blender
        has_smooth = any(poly.use_smooth for poly in mesh.polygons)
        use_smooth_shading = has_smooth  # Use smooth if ANY face is smooth
        
        # FORCE triangulation to avoid non-planar quad issues
        primitive_type = 0  # Always triangles
        
        # For quads: calculate polygon normal ONCE, use for BOTH triangles
        # This ensures consistent lighting even on non-planar quads
        for poly in mesh.polygons:
            poly_vert_count = len(poly.vertices)
            
            # Calculate face normal for entire polygon
            poly_normal = normalize(tuple(poly.normal)) if self.export_normals else (0.0, 0.0, 0.0)
            
            if poly_vert_count == 3:
                # Triangle - add directly
                for loop_idx in poly.loop_indices:
                    loop = mesh.loops[loop_idx]
                    vertex = mesh.vertices[loop.vertex_index]
                    
                    pos_orig = tuple(vertex.co)
                    
                    # BRANCHING: Smooth vs Flat shading
                    if self.export_normals:
                        if use_smooth_shading:
                            normal_orig = normalize(tuple(vertex.normal))  # SMOOTH: vertex normal
                        else:
                            normal_orig = poly_normal  # FLAT: polygon normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_idx].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    indices.append(next_index)
                    next_index += 1
            
            elif poly_vert_count == 4:
                # Quad - split into two triangles using SAME polygon normal
                loop_list = list(poly.loop_indices)
                
                # Triangle 1: loops 0, 1, 2
                for i in [0, 1, 2]:
                    loop = mesh.loops[loop_list[i]]
                    vertex = mesh.vertices[loop.vertex_index]
                    
                    pos_orig = tuple(vertex.co)
                    
                    # BRANCHING: Smooth vs Flat shading
                    if self.export_normals:
                        if use_smooth_shading:
                            normal_orig = normalize(tuple(vertex.normal))
                        else:
                            normal_orig = poly_normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_list[i]].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    indices.append(next_index)
                    next_index += 1
                
                # Triangle 2: loops 0, 2, 3
                for i in [0, 2, 3]:
                    loop = mesh.loops[loop_list[i]]
                    vertex = mesh.vertices[loop.vertex_index]
                    
                    pos_orig = tuple(vertex.co)
                    
                    # BRANCHING: Smooth vs Flat shading
                    if self.export_normals:
                        if use_smooth_shading:
                            normal_orig = normalize(tuple(vertex.normal))
                        else:
                            normal_orig = poly_normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_list[i]].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    indices.append(next_index)
                    next_index += 1
            
            else:
                # N-gon: triangulate from first vertex, all triangles share polygon normal
                loop_list = list(poly.loop_indices)
                for i in range(1, poly_vert_count - 1):
                    # Triangle: loops 0, i, i+1
                    for loop_idx in [loop_list[0], loop_list[i], loop_list[i + 1]]:
                        loop = mesh.loops[loop_idx]
                        vertex = mesh.vertices[loop.vertex_index]
                        
                        pos_orig = tuple(vertex.co)
                        
                        # BRANCHING: Smooth vs Flat shading
                        if self.export_normals:
                            if use_smooth_shading:
                                normal_orig = normalize(tuple(vertex.normal))
                            else:
                                normal_orig = poly_normal
                        else:
                            normal_orig = (0.0, 0.0, 0.0)
                        
                        uv_orig = tuple(uv_layer[loop_idx].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                        
                        vertices.append(pos_orig)
                        normals.append(normal_orig)
                        uvs.append(uv_orig)
                        indices.append(next_index)
                        next_index += 1
        
        return vertices, normals, uvs, indices, primitive_type
    
    def write_k3d_file(self, filepath, vertices, normals, uvs, indices, primitive_type):
        """
        Write K3D binary file.
        
        Args:
            filepath: Output file path
            vertices: List of (x, y, z) vertex positions
            normals: List of (nx, ny, nz) normals
            uvs: List of (u, v) texture coordinates
            indices: List of indices
            primitive_type: 0 for triangles, 1 for quads
        """
        # Calculate flags
        flags = k3d_format.calculate_flags(
            has_normals=self.export_normals and len(normals) > 0,
            has_uvs=self.export_uvs and len(uvs) > 0
        )
        
        # Open file for binary writing
        with open(filepath, 'wb') as f:
            # Write header with primitive type
            k3d_format.write_k3d_header(f, len(vertices), len(indices), flags, primitive_type=primitive_type)
            
            # Write vertex positions (always present)
            k3d_format.write_vertex_data(f, vertices)
            
            # Write normals if present
            if flags & k3d_format.K3D_HAS_NORMALS:
                k3d_format.write_normal_data(f, normals)
            
            # Write UVs if present
            if flags & k3d_format.K3D_HAS_UVS:
                k3d_format.write_uv_data(f, uvs)
            
            # Write indices
            k3d_format.write_indices(f, indices)


def menu_func_export(self, context):
    """Add K3D export to File > Export menu"""
    self.layout.operator(ExportK3D.bl_idname, text="K3D (.k3d)")


def register():
    """Register Blender operator"""
    bpy.utils.register_class(ExportK3D)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    """Unregister Blender operator"""
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportK3D)


if __name__ == "__main__":
    register()
