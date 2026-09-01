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

// ---- Noise, for domain-warping the hue so it swirls/marbles instead
// of flowing in clean, predictable bands ----
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
        p *= 2.03;
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec3 pos = normalize(FragPos);
    float t = time * 0.5;

    // ---- Domain-warped, marbled hue field ----
    // The previous version derived hue from three clean sine waves
    // added together, which - however many waves you stack - still
    // reads as one smooth, predictable gradient sweeping across the
    // surface. Warping the sampling coordinate itself with fbm before
    // computing hue breaks that predictability: the color bands bend,
    // pinch, and swirl unevenly instead of flowing in straight
    // diagonal stripes, which is what actually looks "random" rather
    // than just colorful.
    vec2 p2 = pos.xy * 3.0 + pos.z * 1.5;
    vec2 warp = vec2(fbm(p2 + t * 0.3), fbm(p2 * 1.3 - t * 0.25)) * 1.8;

    float wave1 = sin((p2.x + warp.x) * 5.0 + t);
    float wave2 = sin((p2.y + warp.y) * 6.0 - t * 1.3);
    float wave3 = sin(dot(pos, vec3(1.0)) * 4.0 + warp.x * 3.0 - t * 0.8);
    float hue = fract((wave1 + wave2 + wave3) * 0.5 + 0.5 + t * 0.15 +
                      fbm(p2 * 2.0 - t * 0.2) * 0.4);

    vec3 rainbow = 0.5 + 0.5 * cos(6.28318 * (hue + vec3(0.0, 0.33, 0.67)));

    // Extra saturation/contrast boost, and a touch of local variation
    // so neighboring color bands don't all sit at the exact same
    // brightness.
    rainbow = pow(rainbow, vec3(1.5));
    rainbow *= 1.2 + 0.2 * fbm(p2 * 4.0 + t * 0.5);

    // ---- Sparkle ----
    // Small, sharp bright flecks scattered across the surface (hashed
    // per-cell, thresholded hard rather than smoothed) - the kind of
    // random glints that make something feel alive/shiny instead of
    // just being a colored gradient.
    vec2 sparkleCell = floor(p2 * 10.0 + warp * 2.0);
    float sparkleChance = hash(sparkleCell + floor(t * 3.0) * 0.001);
    float sparkle = step(0.97, sparkleChance);
    rainbow += vec3(1.0) * sparkle * 0.5;

    // Smooth diffuse shading (not too dark).
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    rainbow *= (0.8 + 0.2 * diff);

    rainbow = mix(rainbow, highlight_color.rgb, highlight_value);

    FragColor = vec4(rainbow, material.alpha);
}
