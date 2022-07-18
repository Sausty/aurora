# Aurora: 3D Vulkan sandbox written in C

## This engine stopped development in May 2022.

## Dependencies:
- HandmadeMath
- SPIRV-Reflect
- stb_image
- Vulkan Memory Allocator
- volk
  
## Features:

- Vulkan Backend
- GLTF loading using CGLTF
- Bindless resource model for textures and sampelrs
- PBR shading
- Turing Mesh Shader pipeline
- Frustum Culling
- Fast Approximate Anti-Aliasing (FXAA)

## Screenshots:

![Sponza](.github/sponza.png)

## Running the engine:

```bat
git clone https://github.com/Sausty/aurora
build_shaders.bat
copy_assets.bat
vcvarsall x64
build.bat
run.bat
```
