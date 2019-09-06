#version 440
precision mediump float;

layout (binding = 0) uniform sampler2D uMainTex;
in vec4 vNormal;
in vec2 vTexCoord;
out vec4 FragColor;

void main() {
  FragColor = texture2D(uMainTex, vTexCoord);
}
