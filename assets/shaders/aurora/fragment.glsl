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

void main()
{
    vec3 pos = FragPos;
    float t = time * 0.8;

    // Multiple interfering waves for rich aurora curtains
    float wave1 = sin(pos.x * 3.0 + pos.y * 2.0 + t);
    float wave2 = sin(pos.y * 5.0 - pos.z * 2.0 + t * 1.5);
    float wave3 = sin(pos.z * 4.0 + pos.x * 3.0 - t * 0.9);
    float interference = wave1 * wave2 + wave3 * 0.5;

    // Bright colors: green, blue, purple
    vec3 aurora = vec3(0.0);
    aurora += vec3(0.2, 1.0, 0.6) * max(interference, 0.0) * 1.6;
    aurora += vec3(0.4, 0.6, 1.0) * max(-interference, 0.0) * 1.4;
    aurora += vec3(0.9, 0.4, 1.0) * (0.5 + 0.5 * sin(t * 1.2)) * 0.4;

    // Height fade – but minimum 0.3 so no black
    float height = smoothstep(-1.0, 2.0, pos.y);
    aurora *= (height * 0.8 + 0.3);

    // Glow on the brightest parts
    float glow = pow(max(interference, 0.0), 2.0);
    aurora += vec3(0.6, 1.0, 0.9) * glow * 1.0;

    // Very gentle shading
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    aurora *= (0.7 + 0.3 * diff);

    aurora = mix(aurora, highlight_color.rgb, highlight_value);

    FragColor = vec4(aurora, material.alpha);
}