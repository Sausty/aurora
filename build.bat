@echo off

set rootDir=%cd%
if not exist build (
    mkdir build
)

set debug=false

if %debug%==true (
    echo Compiling in debug mode.
    echo.

    set debugFlags = -DAURORA_DEBUG
    set entryPoint = /link /subsystem:CONSOLE
    set link_path=/link /LIBPATH:%rootDir%/bin/dbg    
) else (
    echo Compiling in release mode.
    echo.

    set debugFlags=-Oi -fp:fast
    set entryPoint = /link /subsystem:WINDOWS
    set link_path=/link /LIBPATH:%rootDir%/bin/dbg
)

set output=aurora
set flags=-nologo -FC -Zi -W2 /MP -DVK_NO_PROTOTYPES -DVK_USE_PLATFORM_WIN32_KHR -D_NO_DEBUG_HEAP
set disabledWarnings=-wd4100 -wd4201 -wd4018 -wd4099 -wd4189 -wd4505 -wd4530 -wd4840 -wd4324 -wd4459 -wd4702 -wd4244 -wd4310 -wd4611 -wd4996
set source= %rootDir%/src/*.c %rootDir%/src/resource/*.c %rootDir%/src/gfx/*.c %rootDir%/src/core/*.c %rootDir%/src/client/*.c %rootDir%/src/audio/*.c
set links=user32.lib shlwapi.lib volk.lib vma.lib spirv_reflect.lib stb_image.lib cgltf.lib
set includeDirs= -I%rootDir%/src -I%rootDir%/third_party -I%VULKAN_SDK%/Include

pushd build
if not exist volk.lib (
    cl -I%VULKAN_SDK%/Include %flags% -Fe%output% %rootDir%/third_party/volk.c /incremental /c
    lib %rootDir%/build/volk.obj
)
if not exist vma.lib (
    cl -I%VULKAN_SDK%/Include -nologo -FC -Zi -w /MP -DVK_NO_PROTOTYPES -Fe%output% %rootDir%/third_party/vma.cpp /incremental /c
    lib %rootDir%/build/vma.obj
)
if not exist spirv_reflect.lib (
    cl -I%VULKAN_SDK%/Include -nologo -FC -Zi -w /MP -DVK_NO_PROTOTYPES -Fe%output% %rootDir%/third_party/spirv_reflect.c /incremental /c
    lib %rootDir%/build/spirv_reflect.obj
)
if not exist stb_image.lib (
    cl -nologo -FC -Zi -w /MP -Fe%output% %rootDir%/third_party/stb_image.c /incremental /c
    lib %rootDir%/build/stb_image.obj
)
if not exist cgltf.lib (
    cl -nologo -FC -Zi -w /MP -Fe%output% %rootDir%/third_party/cgltf.c /incremental /c
    lib %rootDir%/build/cgltf.obj
)   
cl %disabledWarnings% %includeDirs% %debugFlags% %flags% -Fe%output% %source% /incremental %links% %entryPoint% %link_path%
popd

echo.
echo Build finished.