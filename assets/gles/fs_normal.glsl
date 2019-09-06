
precision mediump float;

uniform sampler2D uMainTex;
varying vec4 vNormal;
varying vec2 vTexCoord;

void main() {
  gl_FragColor = texture2D(uMainTex, vTexCoord);
}
