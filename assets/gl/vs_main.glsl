#version 440

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoord;
uniform mat4 uMatMVP;
out vec2 vTexCoord;

void main() {
  gl_Position = uMatMVP * vec4(aPosition, 1.0);
  vTexCoord = aTexCoord;
}
