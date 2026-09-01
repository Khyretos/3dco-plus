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

// Noise
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p); vec2 f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y);
}
float fbm(vec2 p) {
    float s=0.0; float a=0.5;
    for(int i=0;i<5;i++){ s+=a*noise(p); p*=2.0; a*=0.5; }
    return s;
}

void main()
{
    // ---- Dark, rough rock ----
    // Previously a mid-brown rock color with a bright, evenly-lit toon
    // floor (level clamped to at least 0.5) - together those kept the
    // whole surface fairly bright and warm, which read as "cartoon
    // dirt" rather than "dark hellish rock". This drops the rock to a
    // near-black charred color and lets real shadow through (the toon
    // floor below is now much lower), so only the glowing cracks and
    // whatever's actually lit stand out - the "cartoon hell" look
    // (Hercules/Moana-style lava caves) is built on that darkness
    // contrast, not on the rock itself being bright.
    vec3 rockColor = vec3(0.05, 0.035, 0.03);
    vec3 rockColorLit = vec3(0.16, 0.11, 0.09); // slightly lit facets, still dark
    vec3 lavaColor = vec3(1.0, 0.35, 0.05);
    vec3 lavaCore = vec3(1.0, 0.9, 0.3); // hottest vein centers

    vec2 uv = FragPos.xy * 3.0 + vec2(time * 0.15, 0.0);

    // ---- Glowing cracks instead of blotches ----
    // Sampling fbm directly (the previous approach) gives smooth
    // rounded lava "blobs". Real lava-rock cracks are thin veins that
    // snake through the surface - ridged noise (folding fbm around
    // its midpoint) produces exactly that: thin bright ridges with
    // dark rock in between, which is what actually reads as "cracked
    // open rock glowing from within" rather than "brown and orange
    // marble".
    float base = fbm(uv + fbm(uv * 0.5 - time * 0.08));
    float ridged = 1.0 - abs(base * 2.0 - 1.0);
    ridged = pow(ridged, 4.0); // sharpen into thin veins

    float veinCore = pow(ridged, 6.0); // even thinner hot core within each vein

    // Slow pulse so the glow breathes rather than sitting static.
    float pulse = 0.85 + 0.15 * sin(time * 1.3 + base * 6.2831);
    ridged *= pulse;

    vec3 finalColor = mix(rockColor, lavaColor, ridged);
    finalColor = mix(finalColor, lavaCore, veinCore * pulse);
    finalColor += lavaColor * ridged * 0.6; // emissive bloom on the veins

    // ---- Dim, moody toon lighting ----
    // The floor here is deliberately low (0.12, not the previous 0.5)
    // so unlit rock actually reads as dark instead of always being
    // pulled back up to a mid-gray "safe" brightness - that floor was
    // the main reason this never looked like a dark ambience before.
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0);
    float level = floor(diff * 3.0) / 3.0;
    level = max(level, 0.12);
    vec3 rockLit = mix(rockColor, rockColorLit, level);
    finalColor = mix(rockLit, finalColor, max(ridged, 0.15));

    // Warm rim glow suggesting heat radiating off the surface, kept
    // subtle so it doesn't wash out the darkness.
    vec3 viewDir = normalize(viewPos - FragPos);
    float rim = 1.0 - abs(dot(viewDir, norm));
    finalColor += vec3(0.8, 0.25, 0.05) * rim * rim * 0.25;

    // A little ambient smoke/haze darkening in the deepest crevices
    // (low light, low glow) to push the "dark ambience" further.
    float haze = 1.0 - (level * 0.5 + ridged * 0.5);
    finalColor *= mix(1.0, 0.7, haze * 0.4);

    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    FragColor = vec4(finalColor, material.alpha);
}
