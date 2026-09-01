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

// ---- Noise ----
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash1(float n) { return fract(sin(n) * 43758.5453); }
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
    for (int i = 0; i < 5; i++) {
        s += a * noise(p);
        p *= 2.05;
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec3 norm = normalize(Normal);
    float t = time * 0.05;

    // ---- Spiral nebula ----
    // Polar coordinates around the mesh's local origin turn straight
    // fbm noise into swirling spiral arms once the angle is warped by
    // 1/radius - the same trick used for the black hole shader's
    // accretion disk, just softer and without the collapsing-inward
    // motion, since a galaxy's arms rotate rigidly rather than being
    // pulled into a point.
    vec2 p = FragPos.xz * 1.3;
    float radius = length(p);
    float angle = atan(p.y, p.x);
    float swirl = angle + 1.4 / (radius + 0.35) - t * 1.5;
    vec2 spiralCoord = vec2(cos(swirl), sin(swirl)) * radius * 2.2 + FragPos.y * 0.6;

    float arms = fbm(spiralCoord * 1.6 + t * 0.4);
    float wisps = fbm(spiralCoord * 3.5 - t * 0.6);
    float nebula = arms * 0.7 + wisps * 0.3;

    // ---- Palette ----
    // Deep-space navy base, blooming into magenta/violet nebula clouds
    // with cyan fringing - the Fortnite-galaxy-skin look is really
    // just "dark base + saturated purple/pink/blue clouds + tiny
    // bright stars", so that's exactly what's layered here.
    vec3 spaceBase = vec3(0.02, 0.02, 0.06);
    vec3 violet = vec3(0.45, 0.15, 0.65);
    vec3 magenta = vec3(0.85, 0.15, 0.55);
    vec3 cyanEdge = vec3(0.25, 0.65, 0.95);

    float cloud = smoothstep(0.35, 0.9, nebula);
    vec3 nebulaColor = mix(violet, magenta, smoothstep(0.4, 0.85, wisps));
    nebulaColor = mix(nebulaColor, cyanEdge, smoothstep(0.75, 1.0, arms) * 0.5);

    vec3 galaxyColor = mix(spaceBase, nebulaColor, cloud);

    // Bright core glow near the spiral's center, like a galactic core.
    float core = 1.0 - smoothstep(0.0, 1.1, radius);
    galaxyColor += vec3(1.0, 0.85, 0.7) * pow(core, 3.0) * 0.9;

    // ---- Stars ----
    // Sparse, hard-edged bright points via a threshold on per-cell
    // hash rather than noise - noise gives smooth blobs, a real
    // starfield needs isolated pinpricks. Twinkle by modulating each
    // star's own brightness with its own phase (hashed per cell) so
    // they don't all pulse in lockstep.
    vec2 starCell = floor(spiralCoord * 18.0);
    float starChance = hash(starCell);
    float star = step(0.985, starChance);
    float twinkle = 0.6 + 0.4 * sin(t * 20.0 + hash1(dot(starCell, vec2(1.0))) * 6.2831);
    galaxyColor += vec3(1.0) * star * twinkle;

    // Gentle real shading so the mesh's actual form still reads
    // (button presses, model silhouette) without flattening the
    // self-illuminated nebula look.
    float diff = max(dot(norm, -lightDir), 0.0);
    galaxyColor *= (0.8 + 0.2 * diff);

    galaxyColor = mix(galaxyColor, highlight_color.rgb, highlight_value);

    FragColor = vec4(galaxyColor, material.alpha);
}
