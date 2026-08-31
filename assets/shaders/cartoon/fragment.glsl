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
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);

    // Cel shading: quantize diffuse into 4 bands for more depth
    float diff = max(dot(norm, -lightDir), 0.0);
    float level = floor(diff * 4.0) / 4.0;
    // Slightly darker shadows for contrast, but still not too dark
    level = max(level, 0.5);

    // Saturate the base color a bit for a vibrant anime look
    vec3 baseColor = material.color.rgb * 1.4;
    // Use a strong ambient + diffuse for bright, flat anime color
    vec3 finalColor = baseColor * (material.ambient * 1.2 + material.diffuse * level);

    // Rim light (fresnel) – gives that anime edge glow
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = pow(rim, 3.0);
    finalColor += lightColor * rim * 0.6;

    // ---- Edge detection using fwidth ----
    vec3 normalDelta = fwidth(Normal);
    float edge = length(normalDelta);
    // Smooth outline – higher lower bound reduces jaggies
    float outline = smoothstep(0.1, 1.0, edge);

    // Apply black outline (mix to black)
    finalColor = mix(finalColor, vec3(0.0), outline);

    // Apply highlight (button presses, etc.)
    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    FragColor = vec4(finalColor, material.alpha);
}