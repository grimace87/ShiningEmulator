#version 440
precision mediump float;

layout (binding = 1) uniform sampler2D uMainTex;
uniform vec3 uTextColour;
in vec2 vTexCoord;
out vec4 FragColor;

void main() {
  vec4 samp = texture2D(uMainTex, vTexCoord);
  FragColor = vec4(uTextColour, samp.a);
}
