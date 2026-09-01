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
    // Snap the lighting inputs - world position and normal - to the
    // value they'd have at the center of each on-screen block (via
    // dFdx/dFdy extrapolation), so every fragment inside the same
    // block computes identical lighting - that's what actually makes
    // this blocky rather than smoothly shaded with a grid drawn over
    // the top.
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 blockCenter = (floor(fragCoord / pixelSize) + 0.5) * pixelSize;
    vec2 delta = blockCenter - fragCoord;

    vec3 posDx = dFdx(FragPos);
    vec3 posDy = dFdy(FragPos);
    vec3 blockPos = FragPos + posDx * delta.x + posDy * delta.y;

    vec3 normDx = dFdx(Normal);
    vec3 normDy = dFdy(Normal);
    vec3 norm = normalize(Normal + normDx * delta.x + normDy * delta.y);
    vec3 viewDir = normalize(viewPos - blockPos);

    float diff = max(dot(norm, -lightDir), 0.0);

    // ---- Depth cues for black/gray controllers ----
    // Quantizing brightness alone (the previous version) does nothing
    // for a mesh whose base color is already dark or neutral gray -
    // multiplying near-black by any brightness band is still
    // near-black, which is exactly the "gray and black mess" problem.
    // Two real depth cues fix this without touching the base color
    // picker at all:
    //   1. Per-band HUE grading (a classic pixel-art/limited-palette
    //      trick): shadows are graded toward cool blue-violet,
    //      highlights toward warm cream, independent of how dark or
    //      neutral the underlying material color is. This alone is
    //      what makes retro pixel-art shading read as "3D" instead of
    //      flat, even on grayscale sprites.
    //   2. A view-angle "form" term (silhouette darkening) that
    //      responds to the surface curving away from the camera -
    //      unlike the light-direction bands, this still shows shape
    //      on a dome/thumbstick even when the diffuse term alone is
    //      nearly flat across it.
    const float bands = 4.0;
    float lightLevel = floor(diff * bands) / (bands - 1.0);
    lightLevel = clamp(lightLevel, 0.0, 1.0);

    vec3 shadowTint = vec3(0.55, 0.55, 0.85);  // cool blue-violet shadow
    vec3 midTint = vec3(1.0, 1.0, 1.0);        // neutral midtone
    vec3 highTint = vec3(1.15, 1.08, 0.85);    // warm highlight
    vec3 grade = lightLevel < 0.5
                    ? mix(shadowTint, midTint, lightLevel * 2.0)
                    : mix(midTint, highTint, (lightLevel - 0.5) * 2.0);

    float brightness = 0.5 + lightLevel * 0.75;

    float form = clamp(dot(norm, viewDir), 0.0, 1.0);
    float formShade = mix(0.6, 1.05, form); // darker toward silhouette

    vec3 baseColor = material.color.rgb;
    vec3 finalColor = baseColor * grade * brightness * formShade;

    // Posterize to a limited palette per channel - the actual "pixel
    // art" color quantization, now with enough headroom (7 levels,
    // up from 5) that dark/gray base colors still get visible steps
    // instead of being crushed to one or two flat blocks.
    finalColor = floor(finalColor * 7.0 + 0.5) / 7.0;
    finalColor = clamp(finalColor, 0.0, 1.0);

    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    // Thin dark grid line between blocks, emphasizing the pixel grid
    // the way pixel-art upscalers often do.
    vec2 coord = fract(fragCoord / pixelSize);
    float edge = step(0.92, max(coord.x, coord.y));
    finalColor *= mix(1.0, 0.82, edge);

    FragColor = vec4(finalColor, material.alpha);
}
