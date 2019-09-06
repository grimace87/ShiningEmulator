
precision mediump float;

uniform sampler2D uMainTex;
uniform vec3 uTextColour;
varying vec2 vTexCoord;

void main() {
  vec4 samp = texture2D(uMainTex, vTexCoord);
  gl_FragColor = vec4(uTextColour, samp.a);
}
