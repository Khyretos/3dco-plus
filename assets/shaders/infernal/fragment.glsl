#version 330 core

in vec2 TexCoord;
in vec3 Normal;
in vec3 FragPos;

out vec4 FragColor;

uniform float time;
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec4 highlight_color;
uniform float highlight_value;

uniform struct Material {
    vec4 color;
    float ambient;
    float diffuse;
    float specular;
    float shininess;
    float alpha;
} material;

// Noise
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p); vec2 f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y);
}
float fbm(vec2 p) {
    float s=0.0; float a=0.5;
    for(int i=0;i<5;i++){ s+=a*noise(p); p*=2.0; a*=0.5; }
    return s;
}

void main()
{
    vec3 rockColor = vec3(0.2, 0.1, 0.05);
    vec3 lavaColor = vec3(1.0, 0.4, 0.0);

    vec2 uv = FragPos.xy * 3.0 + vec2(time * 0.2, 0.0);
    float lava = fbm(uv + fbm(uv * 0.5 - time * 0.1));
    lava = smoothstep(0.35, 0.75, lava);

    vec3 finalColor = mix(rockColor, lavaColor, lava);
    finalColor += lavaColor * lava * 0.5; // emissive glow

    // Toon lighting (bright, no black)
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    float level = floor(diff * 3.0) / 3.0;
    level = max(level, 0.5);
    finalColor *= (material.ambient + material.diffuse * level);

    // Rim glow for heat
    vec3 viewDir = normalize(viewPos - FragPos);
    float rim = 1.0 - abs(dot(viewDir, norm));
    finalColor += vec3(1.0, 0.3, 0.0) * rim * 0.3;

    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    FragColor = vec4(finalColor, material.alpha);
}