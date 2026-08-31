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
    // Screen‑space block size (increase for chunkier pixels)
    float pixelSize = 8.0; // try 6, 10, or 12
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 block = floor(fragCoord / pixelSize);

    // Basic diffuse lighting, no abs()
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);

    // Quantize into 3 distinct light bands (shadow, mid, lit)
    float lightLevel = floor(diff * 1.0) / 1.0;

    // Map to brightness: shadow 0.65, mid 0.95, lit 1.25
    // Adjust these to make it brighter/darker
    float brightness = 5 * lightLevel;

    // Vibrant base color * brightness
    vec3 baseColor = material.color.rgb;
    vec3 finalColor = baseColor * brightness;

    // Posterize to a limited palette (5 levels per channel)
    finalColor = floor(finalColor * 5.0) / 5.0;

    // Clamp and apply highlight
    finalColor = clamp(finalColor, 0.0, 1.0);
    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    // Darken block edges slightly to emphasize the pixel grid
    vec2 coord = fract(fragCoord / pixelSize);
    float edge = step(0.95, max(coord.x, coord.y));
    finalColor *= mix(1.0, 0.9, edge);

    FragColor = vec4(finalColor, material.alpha);
}