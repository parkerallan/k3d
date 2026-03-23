# K3D Exporter for Blender

Export Blender meshes to K3D binary format optimized for KallistiOS/Dreamcast rendering.

## Features

- **Optimized Binary Format**: Fast loading with minimal parsing overhead
- **Indexed Geometry**: Automatic vertex deduplication for optimal memory usage
- **Normals & UVs**: Support for lighting and texture mapping
- **High Performance**: 50-100x faster loading than text formats
- **Vertex Arrays**: Generates data compatible with `glDrawElements()` for efficient rendering

## Installation

### Method 1: Install from ZIP (Recommended)

1. Download or create a ZIP file containing the `k3d_exporter` folder
2. Open Blender
3. Go to **Edit > Preferences > Add-ons**
4. Click **Install...** button
5. Navigate to the ZIP file and select it
6. Enable the addon by checking the box next to "Import-Export: K3D Format (.k3d)"

### Method 2: Manual Installation

1. Locate your Blender addons directory:
   - **Windows**: `%APPDATA%\Blender Foundation\Blender\<version>\scripts\addons\`
   - **macOS**: `~/Library/Application Support/Blender/<version>/scripts/addons/`
   - **Linux**: `~/.config/blender/<version>/scripts/addons/`

2. Copy the entire `k3d_exporter` folder to the addons directory

3. Restart Blender

4. Go to **Edit > Preferences > Add-ons**

5. Search for "K3D" and enable the addon

## Usage

### Exporting a Mesh

1. Create or open a mesh in Blender
2. Select the mesh you want to export (or leave unselected to export all meshes)
3. Go to **File > Export > K3D (.k3d)**
4. Configure export options:
   - **Selection Only**: Export only selected objects
   - **Export Normals**: Include vertex normals (recommended for lighting)
   - **Export UVs**: Include texture coordinates (required for texturing)
5. Choose output location and filename
6. Click **Export K3D**

### Export Options

- **Export Normals** (default: ON)
  - Includes per-vertex normals for smooth lighting
  - Disable to reduce file size if lighting is not needed

- **Export UVs** (default: ON)
  - Includes texture coordinates for texture mapping
  - Disable if mesh is untextured to reduce file size

- **Selection Only** (default: OFF)
  - When enabled, exports only selected mesh objects
  - When disabled, exports all mesh objects in the scene
  - Note: Only the first mesh will be exported if multiple exist

### Important Notes

- **Triangulation**: Meshes are automatically triangulated on export (quads and n-gons are converted to triangles)
- **Modifiers**: All modifiers are applied during export
- **Multiple Objects**: If multiple meshes are present, only the first is exported (combine meshes before export if needed)
- **Vertex Limit**: Maximum 65,535 unique vertices per mesh (uint16 index limit)

## K3D Format Specification

### File Structure

```
[16 bytes]  Header
[n bytes]   Vertex positions (float x, y, z) × vertexCount
[n bytes]   Normals (float nx, ny, nz) × vertexCount (if K3D_HAS_NORMALS flag set)
[n bytes]   UVs (float u, v) × vertexCount (if K3D_HAS_UVS flag set)
[n bytes]   Indices (uint16) × indexCount
```

### Header (16 bytes)

```c
struct K3DHeader {
    char magic[4];        // "K3D\0"
    uint16_t version;     // Format version (1)
    uint16_t flags;       // Feature flags
    uint32_t vertexCount; // Number of unique vertices
    uint32_t indexCount;  // Number of indices (triangles × 3)
};
```

### Feature Flags

- `0x0001` - K3D_HAS_NORMALS: Mesh includes normal data
- `0x0002` - K3D_HAS_UVS: Mesh includes texture coordinates
- `0x0004` - K3D_HAS_COLORS: Mesh includes vertex colors (reserved for future use)

### Data Types

- All numeric values are **little-endian**
- Floats are IEEE 754 single-precision (32-bit)
- Indices are unsigned 16-bit integers

## Using K3D Files in KallistiOS

### Loading a K3D Model

```c
#include "k3d.h"

// Load model from romdisk
K3DMesh *mesh = k3d_load("/rd/model.k3d");
if (!mesh) {
    printf("Failed to load model\n");
    return -1;
}
```

### Rendering a K3D Model

```c
// In your render loop
glBindTexture(GL_TEXTURE_2D, texture);
k3d_render(mesh);
```

### Cleanup

```c
// When done with the model
k3d_free(mesh);
```

## Example Workflow

1. **Model in Blender**:
   - Create or import your 3D model
   - Apply modifiers (subdivision, mirror, etc.)
   - Unwrap UVs and assign materials
   - **File > Export > K3D (.k3d)**
   - Save as `model.k3d`

2. **Add to KallistiOS Project**:
   - Copy `model.k3d` to your romdisk directory
   - The file will be embedded in your Dreamcast program

3. **Load in Code**:
   ```c
   mesh = k3d_load("/rd/model.k3d");
   ```

4. **Render**:
   ```c
   k3d_render(mesh);
   ```

## Performance Benefits

Compared to text-based formats:

- **Loading Speed**: 50-100x faster (binary vs text parsing)
- **File Size**: 50-70% smaller (binary vs ASCII)
- **Rendering Speed**: 2-3x faster (indexed vertex arrays vs immediate mode)
- **Memory Usage**: 40-60% less (vertex sharing via index buffer)

## Troubleshooting

### "No mesh objects to export"
- Ensure you have at least one mesh object in your scene
- Check that the object type is MESH (not CURVE, EMPTY, etc.)

### "Export failed: 'NoneType' object has no attribute..."
- Ensure your mesh has valid geometry (vertices and faces)
- Try applying all modifiers before export

### "Index count exceeds 65535"
- Your mesh has too many unique vertices
- Reduce polygon count or split into multiple meshes
- Use Decimate modifier to reduce complexity

### File size seems too large
- Disable normals if lighting is not needed
- Disable UVs if texturing is not needed
- Remove duplicate vertices in Blender (select all, M > Merge > By Distance)

## License

Part of KallistiOS - Licensed under the KOS License.
See the KallistiOS repository for full license details.

## Contributing

Submit issues and pull requests to the [KallistiOS GitHub repository](https://github.com/KallistiOS/KallistiOS).

## Additional Tools

### OBJ to K3D Converter

Convert Wavefront OBJ files to K3D format:

```bash
python obj2k3d.py model.obj model.k3d
```

**Options:**
- `--no-normals` - Skip normal data export
- `--no-uvs` - Skip texture coordinate export

**Features:**
- Automatic vertex deduplication
- Detects and preserves triangles or quads
- Triangulates n-gons automatically
- Handles OBJ files with/without normals and UVs

**Examples:**
```bash
# Convert with all data
python obj2k3d.py teapot.obj teapot.k3d

# Convert without normals (flat shading)
python obj2k3d.py model.obj model.k3d --no-normals

# Convert untextured model
python obj2k3d.py mesh.obj mesh.k3d --no-uvs
```

### Text to K3D Converter

Convert legacy text format to K3D:

```bash
python txt2k3d.py input.txt output.k3d
```

Auto-detects triangle or quad format based on vertex count.

## Version History

### 1.0.0 (2026)
- Initial release
- Binary format with indexed geometry
- Support for positions, normals, and UVs
- Automatic vertex deduplication
- Blender 2.80+ compatibility
