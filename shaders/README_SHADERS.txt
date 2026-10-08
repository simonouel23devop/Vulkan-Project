To build shaders to SPIR-V use glslangValidator (provided by LunarG):

glslangValidator -V shaders/vert.glsl -o shaders/vert.spv
glslangValidator -V shaders/frag.glsl -o shaders/frag.spv

Place the produced .spv files in the shaders/ directory before running the application.
