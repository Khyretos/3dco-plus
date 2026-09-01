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
    float facing = dot(viewDir, norm); // ~0 at silhouette, ~1 facing camera

    // ---- Cel shading ----
    // Quantized into 4 hard bands rather than a smooth gradient - this
    // is the core of the "flat anime coloring" look, each band reads as
    // a distinct flat color rather than a gradient.
    float diff = max(dot(norm, -lightDir), 0.0);
    float level = floor(diff * 4.0) / 4.0;
    level = max(level, 0.5); // keep shadows readable rather than black

    // ---- Form/curvature shading ----
    // The banding above only reacts to ONE directional light, so a
    // rounded part (a thumbstick's dome, a concave button) viewed
    // from an angle where that light barely varies across it - e.g.
    // looking straight down at a stick lit mostly from above - can
    // end up in a single flat band with no visible shape at all. This
    // adds a second, always-on shading term based purely on view
    // angle (bright dead-on, darker toward the silhouette) - the toon
    // equivalent of ambient occlusion/a studio fill light - so
    // concave/convex shape reads correctly from *any* viewing angle,
    // not just ones where the key light happens to help.
    float form = clamp(facing, 0.0, 1.0);
    float formShade = mix(0.72, 1.08, form);

    // Saturate the base color for a vibrant anime look, and use a
    // strong ambient + diffuse term for bright, flat coloring.
    vec3 baseColor = material.color.rgb * 1.4;
    vec3 finalColor =
        baseColor * (material.ambient * 1.2 + material.diffuse * level) *
        formShade;

    // ---- Hard-edged specular "pop" ----
    // Real cel-shaded/anime rendering almost never has a smooth
    // specular gradient - it's a small, sharp, near-white highlight
    // with a hard edge. A smoothstep with a narrow transition band
    // gives that pop without a fully aliased single-pixel edge.
    vec3 halfDir = normalize(-lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 48.0);
    float specPop = smoothstep(0.35, 0.5, spec);
    finalColor += vec3(1.0) * specPop * 0.55;

    // ---- Rim light ----
    // Fresnel-style rim glow along the silhouette, tinted by the light
    // color, layered under the outline pass below.
    float rim = 1.0 - clamp(facing, 0.0, 1.0);
    rim = pow(rim, 3.0);
    finalColor += lightColor * rim * 0.6;

    // ---- Outline / edge detection ----
    // The previous version only used fwidth(Normal) to catch crease
    // edges (where the normal itself changes sharply between adjacent
    // fragments), which barely ever fires except on very hard angles -
    // most controller meshes are made of smoothly curved or flat-ish
    // parts, so that outline rarely showed up at all. This combines
    // three independent edge signals so the outline actually appears
    // reliably around the silhouette as well as at internal creases:
    //   1. Crease edges - large per-fragment change in the normal.
    //   2. Silhouette edges - grazing view angle, where the surface is
    //      turning away from the camera (the classic single-pass toon
    //      outline trick; catches the mesh's own silhouette without
    //      needing a depth buffer or a second inflated-hull pass).
    //   3. Depth-gradient edges - large per-fragment change in view-space
    //      depth, which catches edges where geometry folds sharply
    //      toward/away from the camera even when the normal itself
    //      doesn't change much (e.g. a thin bumper meeting a flat face).
    vec3 normalDelta = fwidth(norm);
    float creaseEdge = length(normalDelta);
    float creaseOutline = smoothstep(0.35, 0.85, creaseEdge);

    float silhouetteOutline = smoothstep(0.72, 0.92, rim);

    float viewDepth = length(viewPos - FragPos);
    float depthEdge = fwidth(viewDepth);
    float depthOutline = smoothstep(0.01, 0.05, depthEdge);

    float outline = max(creaseOutline, max(silhouetteOutline, depthOutline));

    // Outline color is a very dark tint of the base color rather than
    // pure black - reads as "ink line" instead of a harsh cutout, while
    // still being dark enough to read clearly as an outline.
    vec3 outlineColor = baseColor * 0.08;
    finalColor = mix(finalColor, outlineColor, outline);

    // Apply highlight (button presses, etc.) on top of everything else.
    finalColor = mix(finalColor, highlight_color.rgb, highlight_value);

    FragColor = vec4(finalColor, material.alpha);
}
