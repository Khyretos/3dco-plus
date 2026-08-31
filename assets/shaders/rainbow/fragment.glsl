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
    vec3 pos = normalize(FragPos);
    float t = time * 0.5;

    float wave1 = sin(pos.x * 5.0 + pos.y * 3.0 + t);
    float wave2 = sin(pos.y * 6.0 - pos.z * 4.0 + t * 1.3);
    float wave3 = sin(pos.z * 5.0 + pos.x * 3.0 - t * 0.8);
    float hue = fract((wave1 + wave2 + wave3) * 0.5 + 0.5 + t * 0.2);

    vec3 rainbow = 0.5 + 0.5 * cos(6.28318 * (hue + vec3(0.0, 0.33, 0.67)));

    // Add a glow by boosting saturation and brightness
    rainbow = pow(rainbow, vec3(1.5));
    rainbow *= 1.3;

    // Smooth diffuse shading (not too dark)
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    rainbow *= (0.8 + 0.2 * diff);

    rainbow = mix(rainbow, highlight_color.rgb, highlight_value);

    FragColor = vec4(rainbow, material.alpha);
}