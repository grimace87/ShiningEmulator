
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec2 aTexCoord;
uniform mat4 uMatMV;
uniform mat4 uMatMVP;
varying vec4 vNormal;
varying vec2 vTexCoord;

void main() {
  gl_Position = uMatMVP * vec4(aPosition, 1.0);
  vNormal = uMatMV * vec4(aNormal, 1.0);
  vTexCoord = aTexCoord;
}
