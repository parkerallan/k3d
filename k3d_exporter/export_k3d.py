"""
K3D Exporter for Blender

Exports Blender meshes to K3D binary format optimized for KallistiOS KGL.
"""

import os
import re

import bpy
import bmesh
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, FloatProperty
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

    normal_face_bias: FloatProperty(
        name="Normal Face Bias",
        description="Blend smooth normals toward the polygon normal. Higher values make faces fall into shadow sooner",
        default=0.0,
        min=0.0,
        max=1.0,
        subtype='FACTOR',
    )

    export_skeleton: BoolProperty(
        name="Export Skeleton Sidecar",
        description="Write a .k3sk file with deform bones and weights",
        default=True,
    )

    export_actions: BoolProperty(
        name="Export Action Clips",
        description="Write one .k3sa file per Blender action that affects the armature",
        default=True,
    )

    export_shapekeys: BoolProperty(
        name="Export Shapekey Clips",
        description="Write one .k3va file per animated shapekey",
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
            vertices, normals, uvs, indices, primitive_type, source_vertex_indices = \
                self.extract_mesh_data(mesh)
            
            # Write K3D file
            self.write_k3d_file(filepath, vertices, normals, uvs, indices, primitive_type)

            armature_obj = self.find_armature_object(obj)
            if self.export_skeleton and armature_obj:
                skeleton_data = self.extract_skeleton_data(obj, armature_obj, source_vertex_indices)
                if skeleton_data:
                    self.write_skeleton_file(filepath, skeleton_data)

                if self.export_actions and skeleton_data:
                    self.write_action_files(context, filepath, obj, armature_obj, skeleton_data['bones'])

            if self.export_shapekeys:
                self.write_shapekey_files(context, filepath, obj, source_vertex_indices)
            
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
                    - Shade Smooth: Uses split loop normals from Blender
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
        source_vertex_indices = []
        
        next_index = 0
        face_bias = self.normal_face_bias
        
        def normalize(v):
            length = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
            if length > 0.0001:
                return (v[0]/length, v[1]/length, v[2]/length)
            return (0.0, 0.0, 1.0)

        def apply_face_bias(loop_normal, face_normal):
            if face_bias <= 0.0:
                return loop_normal

            keep = 1.0 - face_bias
            return normalize((
                loop_normal[0] * keep + face_normal[0] * face_bias,
                loop_normal[1] * keep + face_normal[1] * face_bias,
                loop_normal[2] * keep + face_normal[2] * face_bias,
            ))
        
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
                            normal_orig = apply_face_bias(
                                normalize(tuple(loop.normal)),
                                poly_normal,
                            )
                        else:
                            normal_orig = poly_normal  # FLAT: polygon normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_idx].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    source_vertex_indices.append(loop.vertex_index)
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
                            normal_orig = apply_face_bias(
                                normalize(tuple(loop.normal)),
                                poly_normal,
                            )
                        else:
                            normal_orig = poly_normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_list[i]].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    source_vertex_indices.append(loop.vertex_index)
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
                            normal_orig = apply_face_bias(
                                normalize(tuple(loop.normal)),
                                poly_normal,
                            )
                        else:
                            normal_orig = poly_normal
                    else:
                        normal_orig = (0.0, 0.0, 0.0)
                    
                    uv_orig = tuple(uv_layer[loop_list[i]].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                    
                    vertices.append(pos_orig)
                    normals.append(normal_orig)
                    uvs.append(uv_orig)
                    source_vertex_indices.append(loop.vertex_index)
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
                                normal_orig = apply_face_bias(
                                    normalize(tuple(loop.normal)),
                                    poly_normal,
                                )
                            else:
                                normal_orig = poly_normal
                        else:
                            normal_orig = (0.0, 0.0, 0.0)
                        
                        uv_orig = tuple(uv_layer[loop_idx].uv) if (uv_layer and self.export_uvs) else (0.0, 0.0)
                        
                        vertices.append(pos_orig)
                        normals.append(normal_orig)
                        uvs.append(uv_orig)
                        source_vertex_indices.append(loop.vertex_index)
                        indices.append(next_index)
                        next_index += 1
        
        return vertices, normals, uvs, indices, primitive_type, source_vertex_indices

    def find_armature_object(self, obj):
        """Find the armature driving the exported mesh, if any."""
        if obj.parent and obj.parent.type == 'ARMATURE':
            return obj.parent

        for modifier in obj.modifiers:
            if modifier.type == 'ARMATURE' and modifier.object:
                return modifier.object

        return None

    def sanitize_name(self, name):
        """Convert a Blender name into a filename-safe suffix."""
        cleaned = re.sub(r'[^A-Za-z0-9_-]+', '_', name.strip())
        return cleaned or "clip"

    def flatten_matrix(self, matrix):
        """Flatten a Blender matrix in column-major order for the runtime."""
        return [matrix[row][col] for col in range(4) for row in range(4)]

    def build_output_path(self, filepath, suffix, extension):
        """Create a sidecar filename next to the mesh file."""
        base, _ = os.path.splitext(filepath)
        return f"{base}{suffix}{extension}"

    def pack_weights(self, weighted_groups):
        """Pack up to four weights into byte-sized indices and weights."""
        selected = weighted_groups[:4]
        total = sum(weight for _, weight in selected)
        indices = [255, 255, 255, 255]
        weights = [0, 0, 0, 0]

        if total <= 0.0:
            return indices, weights

        normalized = []
        for bone_index, weight in selected:
            normalized.append((bone_index, weight / total))

        running_total = 0
        for slot, (bone_index, weight) in enumerate(normalized):
            indices[slot] = bone_index
            if slot == len(normalized) - 1:
                packed = max(0, 255 - running_total)
            else:
                packed = int(round(weight * 255.0))
                packed = max(0, min(255, packed))
                running_total += packed
            weights[slot] = packed

        return indices, weights

    def extract_skeleton_data(self, obj, armature_obj, source_vertex_indices):
        """Extract deform bones, inverse bind matrices, and per-exported-vertex weights."""
        mesh_local_from_armature = obj.matrix_world.inverted() @ armature_obj.matrix_world
        deform_bones = [bone for bone in armature_obj.data.bones if bone.use_deform]
        bone_index_map = {bone.name: index for index, bone in enumerate(deform_bones)}
        influences = []
        bones = []

        if not deform_bones:
            return None

        for bone in deform_bones:
            parent_index = -1
            if bone.parent and bone.parent.name in bone_index_map:
                parent_index = bone_index_map[bone.parent.name]

            bind_matrix = mesh_local_from_armature @ bone.matrix_local
            bones.append({
                'parent_index': parent_index,
                'inverse_bind': self.flatten_matrix(bind_matrix.inverted())
            })

        original_mesh = obj.data
        vertex_groups = {group.index: group.name for group in obj.vertex_groups}
        for source_index in source_vertex_indices:
            weighted_groups = []

            if source_index < len(original_mesh.vertices):
                for group in original_mesh.vertices[source_index].groups:
                    group_name = vertex_groups.get(group.group)
                    if group_name in bone_index_map and group.weight > 0.0:
                        weighted_groups.append((bone_index_map[group_name], group.weight))

            weighted_groups.sort(key=lambda item: item[1], reverse=True)
            bone_indices, bone_weights = self.pack_weights(weighted_groups)
            influences.append({
                'bone_indices': bone_indices,
                'bone_weights': bone_weights,
            })

        return {
            'bones': bones,
            'influences': influences,
        }

    def find_action_fps(self, context):
        """Return the effective scene frame rate as an integer."""
        scene = context.scene
        if scene.render.fps_base:
            return max(1, int(round(scene.render.fps / scene.render.fps_base)))
        return max(1, int(scene.render.fps))

    def iter_armature_actions(self, armature_obj):
        """Yield actions that animate pose bones on the target armature."""
        for action in bpy.data.actions:
            if any(curve.data_path.startswith('pose.bones[') for curve in action.fcurves):
                yield action

    def write_skeleton_file(self, filepath, skeleton_data):
        """Write the shared skeleton sidecar."""
        skeleton_path = self.build_output_path(filepath, '', '.k3sk')
        with open(skeleton_path, 'wb') as handle:
            k3d_format.write_k3sk_file(handle, skeleton_data['bones'], skeleton_data['influences'])

    def write_action_files(self, context, filepath, obj, armature_obj, bones):
        """Write one skeletal action clip sidecar per Blender action."""
        fps = self.find_action_fps(context)
        original_action = armature_obj.animation_data.action if armature_obj.animation_data else None
        original_frame = context.scene.frame_current
        pose_bones = armature_obj.pose.bones
        bone_names = [bone.name for bone in armature_obj.data.bones if bone.use_deform]
        mesh_local_from_armature = obj.matrix_world.inverted() @ armature_obj.matrix_world

        if not armature_obj.animation_data:
            armature_obj.animation_data_create()

        try:
            for action in self.iter_armature_actions(armature_obj):
                start = int(action.frame_range[0])
                end = int(action.frame_range[1])
                frames = []
                armature_obj.animation_data.action = action

                for frame in range(start, end + 1):
                    context.scene.frame_set(frame)
                    frame_transforms = []

                    for bone_name in bone_names:
                        pose_bone = pose_bones.get(bone_name)
                        if pose_bone is None:
                            frame_transforms.append({
                                'translation': (0.0, 0.0, 0.0),
                                'rotation': (0.0, 0.0, 0.0, 1.0),
                                'scale': (1.0, 1.0, 1.0),
                            })
                            continue

                        if pose_bone.parent and pose_bone.parent.name in bone_names:
                            local_matrix = pose_bone.parent.matrix.inverted() @ pose_bone.matrix
                        else:
                            local_matrix = mesh_local_from_armature @ pose_bone.matrix

                        location, rotation, scale = local_matrix.decompose()
                        frame_transforms.append({
                            'translation': (location.x, location.y, location.z),
                            'rotation': (rotation.x, rotation.y, rotation.z, rotation.w),
                            'scale': (scale.x, scale.y, scale.z),
                        })

                    frames.append(frame_transforms)

                action_path = self.build_output_path(
                    filepath,
                    f"_{self.sanitize_name(action.name)}",
                    '.k3sa'
                )
                with open(action_path, 'wb') as handle:
                    k3d_format.write_k3sa_file(handle, len(bones), fps, frames)
        finally:
            armature_obj.animation_data.action = original_action
            context.scene.frame_set(original_frame)

    def iter_shapekey_actions(self, shape_keys, key_name):
        """Yield actions that animate the given shapekey."""
        needle = f'key_blocks["{key_name}"].value'
        for action in bpy.data.actions:
            if any(curve.data_path == needle for curve in action.fcurves):
                yield action

    def write_shapekey_files(self, context, filepath, obj, source_vertex_indices):
        """Write one vertex animation sidecar per animated non-basis shapekey."""
        shape_keys = obj.data.shape_keys
        original_frame = context.scene.frame_current
        fps = self.find_action_fps(context)

        if not shape_keys or not shape_keys.key_blocks:
            return

        basis_key = shape_keys.reference_key
        if basis_key is None:
            return

        if not shape_keys.animation_data:
            shape_keys.animation_data_create()

        original_action = shape_keys.animation_data.action

        try:
            for key_block in shape_keys.key_blocks:
                if key_block == basis_key:
                    continue

                actions = list(self.iter_shapekey_actions(shape_keys, key_block.name))
                deltas = []
                for source_index in source_vertex_indices:
                    if source_index >= len(basis_key.data) or source_index >= len(key_block.data):
                        deltas.append((0.0, 0.0, 0.0))
                        continue

                    basis = basis_key.data[source_index].co
                    target = key_block.data[source_index].co
                    deltas.append((target.x - basis.x, target.y - basis.y, target.z - basis.z))

                if actions:
                    action = actions[0]
                    start = int(action.frame_range[0])
                    end = int(action.frame_range[1])
                    weights = []
                    shape_keys.animation_data.action = action

                    for frame in range(start, end + 1):
                        context.scene.frame_set(frame)
                        weights.append(key_block.value)
                else:
                    weights = [1.0]

                vertex_path = self.build_output_path(
                    filepath,
                    f"_{self.sanitize_name(key_block.name)}",
                    '.k3va'
                )
                with open(vertex_path, 'wb') as handle:
                    k3d_format.write_k3va_file(handle, deltas, weights, fps)
        finally:
            shape_keys.animation_data.action = original_action
            context.scene.frame_set(original_frame)
    
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
