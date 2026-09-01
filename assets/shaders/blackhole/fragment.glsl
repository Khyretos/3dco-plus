#version 330 core

// ------------------------------------------------------------------
// A "twisting orb absorbing color" black hole - the way it's usually
// drawn in cartoons/anime (a swirling accretion disk being pulled
// into a dark void, with a bright thin ring right at the edge of the
// event horizon) rather than a physically literal one. Previously
// this was a ported ShaderToy voronoi-cell pattern (credited to Inigo
// Quilez), which read more like a lava-lamp/cell texture than
// anything resembling a black hole once mapped onto a controller
// mesh - this is a from-scratch replacement built for this app's own
// uniforms (FragPos/Normal-based, like the rest of the shader set)
// instead of a 2D screen-space ShaderToy port, so it actually wraps
// and reads correctly on curved mesh parts.
// ------------------------------------------------------------------

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
        p *= 2.0;
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec3 norm = normalize(Normal);

    // ---- Accretion disk: streaks spiraling inward ----
    // Angle is warped by 1/radius so lines that would otherwise be
    // straight get wound tighter and tighter approaching the center -
    // the classic "swirling drain" look. Subtracting time from the
    // sampled coordinate (rather than adding to the angle) makes the
    // pattern itself appear to slide inward along the spiral over
    // time, like matter actually falling in, rather than just the
    // whole pattern rotating rigidly in place.
    vec2 p = FragPos.xz * 1.4;
    float radius = length(p) + 0.001;
    float angle = atan(p.y, p.x);

    float swirl = angle + 2.2 / radius;
    vec2 diskCoord = vec2(cos(swirl), sin(swirl)) * radius * 3.0;
    float inflow = radius * 4.0 - time * 1.6; // slides inward over time
    float streaks = fbm(diskCoord + vec2(inflow, inflow * 0.3));
    float fineStreaks = fbm(diskCoord * 2.5 - vec2(inflow * 1.5, 0.0));
    float disk = streaks * 0.65 + fineStreaks * 0.35;

    // ---- Color: hot inner disk cooling outward ----
    // White-hot close to the horizon, through orange/violet, to a
    // cooler blue further out - matches how accretion disks are
    // almost always stylized (brightest and hottest right at the
    // edge of the void).
    vec3 hotCore = vec3(1.0, 0.95, 0.85);
    vec3 midDisk = vec3(0.95, 0.4, 0.15);
    vec3 outerDisk = vec3(0.35, 0.2, 0.75);
    vec3 farDisk = vec3(0.1, 0.15, 0.4);

    float heat = clamp(1.0 - radius * 0.55, 0.0, 1.0);
    vec3 diskColor = mix(farDisk, outerDisk, smoothstep(0.0, 0.6, heat));
    diskColor = mix(diskColor, midDisk, smoothstep(0.4, 0.8, heat));
    diskColor = mix(diskColor, hotCore, smoothstep(0.75, 1.0, heat));

    vec3 color = diskColor * (0.4 + 0.9 * disk);

    // ---- Event horizon ----
    // A genuinely dark void at the center - nothing escapes, so unlike
    // every other shader in this set this one is allowed to actually
    // go to black - with a bright, thin "photon ring" right at its
    // edge (real general-relativistic renders and every stylized
    // depiction alike put the brightest light immediately outside the
    // horizon, from matter about to cross it).
    float horizonRadius = 0.55;
    float horizonEdge = smoothstep(horizonRadius, horizonRadius + 0.03, radius);
    color *= horizonEdge; // fully black inside the horizon

    float ring = 1.0 - smoothstep(0.0, 0.12, abs(radius - horizonRadius - 0.03));
    color += vec3(1.0, 0.95, 0.85) * ring * 1.3;

    // A little real shading so the mesh's own form still reads at a
    // glance (button presses, silhouette) without fighting the
    // self-illuminated look too much.
    float diff = max(dot(norm, -lightDir), 0.0);
    color *= (0.75 + 0.25 * diff);

    color = mix(color, highlight_color.rgb, highlight_value);

    FragColor = vec4(color, material.alpha);
}
