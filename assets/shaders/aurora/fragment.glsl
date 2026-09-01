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

// ---- Noise (for soft, drifting curtain shapes rather than clean sine
// waves - real aurora curtains are wispy and irregular, not a
// geometric interference pattern) ----
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * noise(p);
        p *= 2.02;
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec3 norm = normalize(Normal);
    float t = time * 0.35;

    // ---- Curtain shape ----
    // Real aurora reads as vertical ribbons that drift and ripple
    // sideways as they rise, not a static interference pattern. Domain
    // -warp a horizontal coordinate with fbm so the "curtains" bend and
    // wander, then carve them into soft vertical bands with a second
    // fbm layer moving upward over time (the "curtain flow").
    vec2 p = FragPos.xz * 1.6 + FragPos.y * 0.35;
    float warp = fbm(p * 0.8 + t * 0.15) * 1.4;
    vec2 curtainCoord = vec2(p.x + warp, FragPos.y * 2.0 - t * 1.2);

    float bands = fbm(curtainCoord * vec2(1.0, 0.35));
    bands = smoothstep(0.35, 0.85, bands);

    // Secondary finer ripple riding on top of the main curtains, for
    // texture within each band rather than a flat wash of color.
    float ripple = fbm(curtainCoord * vec2(2.5, 0.6) + t * 0.4);
    bands *= 0.6 + 0.4 * ripple;

    // ---- Color gradient ----
    // Classic aurora borealis palette: green base (the most common,
    // oxygen-driven color), shifting to cyan/teal, with rarer
    // magenta-violet fringing at the fringes/high-energy streaks -
    // blended by height and by the curtain's own intensity rather
    // than a fixed per-pixel hue, so it reads as one coherent
    // phenomenon instead of independently colored waves.
    vec3 green = vec3(0.15, 1.0, 0.55);
    vec3 cyan = vec3(0.25, 0.9, 0.85);
    vec3 violet = vec3(0.6, 0.35, 1.0);

    float heightMix = smoothstep(-0.5, 1.5, FragPos.y);
    vec3 auroraColor = mix(green, cyan, heightMix);
    float fringe = smoothstep(0.75, 1.0, ripple) * smoothstep(0.6, 1.0, heightMix);
    auroraColor = mix(auroraColor, violet, fringe * 0.6);

    vec3 aurora = auroraColor * bands * 1.6;

    // Soft glow on the brightest streaks.
    float glow = pow(bands, 2.0);
    aurora += vec3(0.5, 0.9, 0.8) * glow * 0.6;

    // Height fade so it thins out realistically rather than covering
    // the whole mesh uniformly - but never fully black.
    float heightFade = smoothstep(-1.2, 1.8, FragPos.y);
    aurora *= (heightFade * 0.75 + 0.3);

    // Gentle real shading on top, kept subtle so it doesn't flatten
    // the glow back into a lit/unlit surface.
    float diff = max(dot(norm, -lightDir), 0.0);
    aurora *= (0.75 + 0.25 * diff);

    aurora = mix(aurora, highlight_color.rgb, highlight_value);

    FragColor = vec4(aurora, material.alpha);
}
