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
    // Screen-space block size (increase for chunkier pixels).
    float pixelSize = 8.0; // try 6, 10, or 12

    // ---- Real screen-space pixelation ----
    // The previous version of this shader floored gl_FragCoord into
    // blocks but never actually used the result for anything, so
    // shading still varied smoothly across every fragment and nothing
    // ever looked blocky. Without a separate low-resolution render
    // target (a bigger architectural change), the way to get a
    // genuinely chunky look from a single fragment shader pass is to
    // snap the *inputs* to the lighting calculation - world position
    // and normal - to the value they'd have at the center of each
    // on-screen block, using dFdx/dFdy to extrapolate there. Every
    // fragment inside the same block then computes the exact same
    // lighting, which is what actually produces flat, hard-edged
    // "pixels" instead of smooth shading with a grid line drawn over it.
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 blockCenter = (floor(fragCoord / pixelSize) + 0.5) * pixelSize;
    vec2 delta = blockCenter - fragCoord;

    vec3 posDx = dFdx(FragPos);
    vec3 posDy = dFdy(FragPos);
    vec3 blockPos = FragPos + posDx * delta.x + posDy * delta.y;

    vec3 normDx = dFdx(Normal);
    vec3 normDy = dFdy(Normal);
    vec3 norm = normalize(Normal + normDx * delta.x + normDy * delta.y);

    // Basic diffuse lighting, sampled at the block-snapped normal.
    float diff = max(dot(norm, -lightDir), 0.0);

    // Quantize into a handful of distinct light bands (shadow, mid, lit)
    // for the classic posterized pixel-art shading look.
    const float bands = 3.0;
    float lightLevel = floor(diff * bands) / bands;
    float brightness = 0.55 + lightLevel * 0.85;

    // Vibrant base color * brightness, posterized to a limited palette.
    vec3 baseColor = material.color.rgb;
    vec3 finalColor = baseColor * brightness;
    finalColor = floor(finalColor * 5.0 + 0.5) / 5.0;

    finalColor = clamp(finalColor, 0.0, 1.0);
    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    // Thin dark grid line between blocks, on top of the now-genuinely-
    // blocky shading, to emphasize the pixel grid the way pixel-art
    // upscalers often do.
    vec2 coord = fract(fragCoord / pixelSize);
    float edge = step(0.92, max(coord.x, coord.y));
    finalColor *= mix(1.0, 0.82, edge);

    FragColor = vec4(finalColor, material.alpha);
}
