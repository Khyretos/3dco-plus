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
    // Two depth cues on top of the base color: per-band HUE grading
    // (shadows toward cool blue-violet, highlights toward warm cream -
    // a classic pixel-art/limited-palette trick that reads as "3D"
    // even on a flat gray/black base color) and a view-angle "form"
    // term (darker toward the silhouette) so curved parts still show
    // shape even where the diffuse light alone is nearly flat.
    //
    // These two cues are combined with max(), not multiplied. An
    // earlier version multiplied light-band brightness and form-shade
    // together as two separate darkening factors - which meant any
    // pixel that was even moderately in shadow AND near a silhouette
    // edge (extremely common across a rounded controller shell) had
    // both factors compounding on top of each other, crushing the
    // whole surface toward black well before quantization ran at all.
    // With max(), a pixel only goes dark when BOTH cues genuinely say
    // "shadow" - lit-but-grazing or unlit-but-facing-camera pixels
    // both stay reasonably bright, which is what actually happens on
    // a real curved, lit surface.
    const float bands = 4.0;
    float lightLevel = floor(diff * bands) / (bands - 1.0);
    lightLevel = clamp(lightLevel, 0.0, 1.0);

    float form = clamp(dot(norm, viewDir), 0.0, 1.0);
    float shade = max(lightLevel, form * 0.55);

    vec3 shadowTint = vec3(0.75, 0.75, 0.95);  // cool blue-violet shadow
    vec3 midTint = vec3(1.0, 1.0, 1.0);        // neutral midtone
    vec3 highTint = vec3(1.15, 1.08, 0.85);    // warm highlight
    vec3 grade = shade < 0.5
                    ? mix(shadowTint, midTint, shade * 2.0)
                    : mix(midTint, highTint, (shade - 0.5) * 2.0);

    // Floor of 0.6 (not 0.0) - even the darkest "both cues say shadow"
    // pixel stays visibly above black rather than crushing to it.
    float brightness = 0.6 + shade * 0.65;

    vec3 baseColor = material.color.rgb;
    vec3 finalColor = baseColor * grade * brightness;

    // ---- Local-shape outline ----
    // Neither depth cue above depends on local geometric detail - both
    // lightLevel and form are direction-based, so they barely change
    // across a head-on/top view of a mostly-convex front face (that's
    // exactly why a flat gray/dark-gray controller went nearly flat
    // from that angle even after the brightness fix above: rotating
    // helped a little by changing the light-vs-surface angle, but a
    // button or d-pad only slightly raised/recessed from the
    // surrounding shell often doesn't tilt its normal far enough to
    // cross into a different coarse light/form band at all). This
    // adds a real edge-detection pass instead - the same fwidth(Normal)
    // technique used for the toon shader's outlines - which reacts to
    // actual local curvature discontinuities (a button's edge, a
    // stick's base) regardless of viewing or lighting angle, so those
    // shapes stay visible even when the coarse banding above reads
    // completely flat across them.
    float creaseMag = length(normDx) + length(normDy);
    float outline = smoothstep(0.12, 0.45, creaseMag);
    finalColor *= mix(1.0, 0.4, outline);

    // Posterize to a limited palette per channel - the actual "pixel
    // art" color quantization.
    finalColor = floor(finalColor * 7.0 + 0.5) / 7.0;
    finalColor = clamp(finalColor, 0.0, 1.0);

    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    // Thin dark grid line between blocks, emphasizing the pixel grid
    // the way pixel-art upscalers often do.
    vec2 coord = fract(fragCoord / pixelSize);
    float edge = step(0.92, max(coord.x, coord.y));
    finalColor *= mix(1.0, 0.85, edge);

    FragColor = vec4(finalColor, material.alpha);
}
