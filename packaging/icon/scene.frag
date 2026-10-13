// Cutthulhu application icon: Cthulhu, shears raised, wading through spilled
// 35mm film in front of NTSC colour bars (SMPTE EG 1 pattern), lit from
// behind by the bars. Original artwork for this project, GPL-3.0-or-later.
//
// A ray-marched scene (WebGL 1 fragment shader). scripts/render-icon.js
// renders it to packaging/icon/icon-1024.png; scripts/make-icons.py makes the
// platform icons from that image.
//
// Note: avoid `continue` and early `return` inside loops and the functions
// they call; Chromium's software WebGL (SwiftShader) miscompiles them.
precision highp float;
uniform vec2 res;

// ================================================================== noise
float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) {
        s += a * noise(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s;
}

// ================================================================== SDF helpers
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
float smax(float a, float b, float k) { return -smin(-a, -b, k); }
float sdEllipsoid(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}
// capsule with radius varying linearly from ra (at a) to rb (at b)
float sdLimb(vec3 p, vec3 a, vec3 b, float ra, float rb) {
    vec3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - mix(ra, rb, h);
}
float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xy) - t.x, p.z);
    return length(q) - t.y;
}
// 2D triangle distance (iq)
float sdTri(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

// ================================================================== the creature
// y up, ground at y = -2.1, creature faces +z (toward the camera)
const vec3 EYE_L = vec3(-0.21, 0.79, 1.27);
const vec3 EYE_R = vec3(0.21, 0.79, 1.27);

// jointed arms: shoulder, elbow, wrist
const vec3 R_SH = vec3(0.86, 0.92, 0.10);
const vec3 R_EL = vec3(1.55, 0.05, 0.55);
const vec3 R_WR = vec3(1.30, 1.00, 1.05);
const vec3 L_SH = vec3(-0.86, 0.92, 0.10);
const vec3 L_EL = vec3(-1.40, -0.15, 0.45);
const vec3 L_WR = vec3(-1.15, -1.05, 0.95);

vec3 tentaclePoint(int i, float t) {
    float fi = float(i);
    float x0 = (fi - 3.5) * 0.095;
    float ph = fi * 1.9;
    vec3 p;
    p.x = x0 * (1.0 + 0.8 * t) + 0.07 * sin(t * 5.0 + ph) * t;
    p.y = 0.48 - 1.40 * t;
    // they hang forward over the chest, then fall against it
    p.z = 1.20 + 0.10 * sin(t * 3.0 + ph) * t - 0.30 * t * t + 0.18 * t * (1.0 - t);
    float c = smoothstep(0.6, 1.0, t);
    p.x += c * 0.09 * sin(ph + t * 10.0);
    p.y += c * 0.10 * (1.0 - cos(t * 10.0 + ph)) * 0.5;
    p.z += c * 0.07 * cos(t * 10.0 + ph);
    return p;
}

float tentacles(vec3 p) {
    float d = 1e9;
    for (int i = 0; i < 8; i++) {
        float r0 = 0.082 - abs(float(i) - 3.5) * 0.004;
        vec3 a = tentaclePoint(i, 0.0);
        for (int s = 1; s <= 7; s++) {
            float t1 = float(s) / 7.0;
            vec3 b = tentaclePoint(i, t1);
            float ra = r0 * pow(1.0 - (t1 - 1.0 / 7.0), 0.9) + 0.005;
            float rb = r0 * pow(1.0 - t1, 0.9) + 0.005;
            d = min(d, sdLimb(p, a, b, ra, rb));
            a = b;
        }
    }
    return d;
}

// a taloned hand: palm at `wrist`, fingers toward `dir`, curl toward `up`
float hand(vec3 p, vec3 wrist, vec3 dir, vec3 up, float curl, out float claw) {
    vec3 side = normalize(cross(dir, up));
    vec3 palm = wrist + dir * 0.14;
    float d = sdEllipsoid(p - palm, vec3(0.16, 0.16, 0.16));
    claw = 1e9;
    for (int f = 0; f < 4; f++) {
        float ff = float(f) - 1.5;
        vec3 k0 = palm + dir * 0.10 + side * ff * 0.085;
        vec3 k1 = k0 + dir * 0.17 + side * ff * 0.03 + up * curl * 0.05;
        vec3 k2 = k1 + dir * (0.13 - curl * 0.06) + up * curl * 0.12;
        d = smin(d, sdLimb(p, k0, k1, 0.055, 0.045), 0.04);
        d = smin(d, sdLimb(p, k1, k2, 0.045, 0.035), 0.03);
        vec3 tip = k2 + dir * (0.12 - curl * 0.08) + up * curl * 0.10 - up * 0.04;
        claw = min(claw, sdLimb(p, k2, tip, 0.032, 0.003));
    }
    // thumb
    vec3 t0 = palm - side * 0.12 + up * 0.04;
    vec3 t1 = t0 + dir * 0.10 - side * 0.06 + up * 0.08;
    d = smin(d, sdLimb(p, t0, t1, 0.05, 0.04), 0.03);
    claw = min(claw, sdLimb(p, t1, t1 + dir * 0.09 + up * 0.06, 0.03, 0.003));
    return d;
}

float arms(vec3 p, out float claw) {
    // upper arms with a bulging biceps, thick forearms
    float d = sdLimb(p, R_SH, R_EL, 0.30, 0.20);
    d = smin(d, sdEllipsoid(p - mix(R_SH, R_EL, 0.45) - vec3(-0.05, 0.0, 0.10), vec3(0.24, 0.30, 0.24)), 0.08);
    d = smin(d, sdLimb(p, R_EL, R_WR, 0.21, 0.13), 0.10);
    d = smin(d, sdEllipsoid(p - mix(R_EL, R_WR, 0.3), vec3(0.22, 0.24, 0.20)), 0.08);
    d = smin(d, sdLimb(p, L_SH, L_EL, 0.30, 0.20), 0.05);
    d = smin(d, sdEllipsoid(p - mix(L_SH, L_EL, 0.45) - vec3(0.05, 0.0, 0.10), vec3(0.24, 0.30, 0.24)), 0.08);
    d = smin(d, sdLimb(p, L_EL, L_WR, 0.21, 0.13), 0.10);
    d = smin(d, sdEllipsoid(p - mix(L_EL, L_WR, 0.3), vec3(0.22, 0.24, 0.20)), 0.08);
    float c1, c2;
    // raised hand closes on the shears; the low hand hangs open
    float h1 = hand(p, R_WR, normalize(vec3(-0.25, 0.85, 0.30)), normalize(vec3(-0.9, -0.1, 0.3)), 1.0, c1);
    float h2 = hand(p, L_WR, normalize(vec3(0.15, -0.9, 0.35)), normalize(vec3(0.3, 0.2, 0.9)), 0.35, c2);
    d = smin(d, h1, 0.08);
    d = smin(d, h2, 0.08);
    claw = min(c1, c2);
    return d;
}

float body(vec3 p) {
    // hunched torso: chest, belly, back hump, pectorals
    float d = sdEllipsoid(p - vec3(0.0, 0.40, 0.10), vec3(0.98, 0.80, 0.62));
    d = smin(d, sdEllipsoid(p - vec3(0.0, -0.55, 0.12), vec3(0.72, 0.75, 0.52)), 0.25);
    d = smin(d, sdEllipsoid(p - vec3(0.0, 0.85, -0.25), vec3(0.80, 0.45, 0.50)), 0.25);
    vec3 q = vec3(abs(p.x), p.y, p.z);
    d = smin(d, sdEllipsoid(q - vec3(0.38, 0.45, 0.55), vec3(0.36, 0.28, 0.20)), 0.15);
    d = smin(d, sdEllipsoid(q - vec3(0.78, 0.92, 0.10), vec3(0.34, 0.30, 0.32)), 0.15);   // shoulders
    // legs, one stepping forward
    d = smin(d, sdLimb(p, vec3(-0.42, -1.10, 0.05), vec3(-0.55, -2.20, 0.75), 0.36, 0.26), 0.20);
    d = smin(d, sdLimb(p, vec3(0.42, -1.10, 0.05), vec3(0.60, -2.30, -0.30), 0.36, 0.26), 0.20);
    // head: mantle swept back, face low and forward
    vec3 m = p - vec3(0.0, 1.28, 0.55);
    m.yz = mat2(0.87, -0.5, 0.5, 0.87) * m.yz;
    float head = sdEllipsoid(m, vec3(0.58, 0.76, 0.61));
    head = smin(head, sdEllipsoid(p - vec3(0.0, 0.74, 0.98), vec3(0.46, 0.38, 0.38)), 0.16);
    // brow
    vec3 b = q - vec3(0.21, 0.88, 1.20);
    head = smin(head, sdEllipsoid(b, vec3(0.24, 0.075, 0.12)), 0.07);
    head = smax(head, -(length(q - vec3(0.21, 0.79, 1.36)) - 0.095), 0.03);
    d = smin(d, head, 0.12);
    return d;
}

// ================================================================== wings
// a curved bone: quadratic bezier a -> c -> b, tapering from ra to rb
float bone(vec3 p, vec3 a, vec3 c, vec3 b, float ra, float rb) {
    float d = 1e9;
    vec3 prev = a;
    for (int i = 1; i <= 8; i++) {
        float t = float(i) / 8.0;
        vec3 q = mix(mix(a, c, t), mix(c, b, t), t);
        d = min(d, sdLimb(p, prev, q, mix(ra, rb, t - 0.125), mix(ra, rb, t)));
        prev = q;
    }
    return d;
}

// membrane between the arm bone and finger bones, in the wing's own plane
float wing(vec3 p, float sx, out float membrane) {
    vec3 q = vec3(p.x * sx, p.y, p.z);
    vec3 root = vec3(0.45, 1.10, -0.30);
    vec3 wrist = vec3(2.05, 3.35, 0.10);
    vec3 hook = vec3(1.55, 3.85, 0.45);
    vec3 f1 = vec3(3.55, 2.55, -0.25);
    vec3 f2 = vec3(3.60, 0.70, -0.45);
    vec3 f3 = vec3(2.85, -0.90, -0.50);
    vec3 base = vec3(0.80, -0.55, -0.40);
    // bones
    // the wing arm arches up from the shoulder, then out to the wrist
    float d = bone(q, root, vec3(0.70, 2.95, -0.15), wrist, 0.24, 0.13);
    // a big hooked claw on top of the wing, curling outward and down
    d = min(d, bone(q, wrist, wrist + vec3(-0.05, 1.00, 0.12), wrist + vec3(0.85, 0.80, 0.15), 0.17, 0.008));
    // long fingers that bow downward, each ending in a small hook
    vec3 c1 = mix(wrist, f1, 0.5) + vec3(0.10, 0.35, 0.0);
    vec3 c2 = mix(wrist, f2, 0.5) + vec3(0.30, 0.15, 0.0);
    vec3 c3 = mix(wrist, f3, 0.5) + vec3(0.35, -0.05, 0.0);
    d = min(d, bone(q, wrist, c1, f1, 0.095, 0.03));
    d = min(d, bone(q, wrist, c2, f2, 0.09, 0.03));
    d = min(d, bone(q, wrist, c3, f3, 0.085, 0.028));
    d = min(d, bone(q, f1, f1 + vec3(0.12, -0.10, 0.0), f1 + vec3(0.06, -0.32, 0.02), 0.03, 0.004));
    d = min(d, bone(q, f2, f2 + vec3(0.10, -0.12, 0.0), f2 + vec3(0.0, -0.32, 0.02), 0.03, 0.004));
    d = min(d, bone(q, f3, f3 + vec3(0.06, -0.14, 0.0), f3 + vec3(-0.08, -0.30, 0.02), 0.028, 0.004));
    // membrane plane through root, wrist and f2
    vec3 o = wrist;
    vec3 ex = normalize(f2 - wrist);
    vec3 nz = normalize(cross(ex, root - wrist));
    vec3 ey = cross(nz, ex);
    vec3 r = q - o;
    vec2 uv = vec2(dot(r, ex), dot(r, ey));
    float w = dot(r, nz);
    // billow: the membrane sags between the bones
    w += 0.10 * sin(uv.x * 2.2) * sin(uv.y * 2.4);
    vec2 P1 = vec2(dot(f1 - o, ex), dot(f1 - o, ey));
    vec2 P2 = vec2(dot(f2 - o, ex), dot(f2 - o, ey));
    vec2 P3 = vec2(dot(f3 - o, ex), dot(f3 - o, ey));
    vec2 PB = vec2(dot(base - o, ex), dot(base - o, ey));
    vec2 PR = vec2(dot(root - o, ex), dot(root - o, ey));
    float area = min(min(sdTri(uv, vec2(0.0), P1, P2), sdTri(uv, vec2(0.0), P2, P3)),
                     min(sdTri(uv, vec2(0.0), P3, PB), sdTri(uv, vec2(0.0), PB, PR)));
    // scalloped trailing edge
    area = max(area, -(length(uv - (P1 + P2) * 0.5 - normalize((P1 + P2) * 0.5) * 0.60) - 0.80));
    area = max(area, -(length(uv - (P2 + P3) * 0.5 - normalize((P2 + P3) * 0.5) * 0.55) - 0.70));
    area = max(area, -(length(uv - (P3 + PB) * 0.5 - normalize((P3 + PB) * 0.5) * 0.45) - 0.60));
    float mem = max(area, abs(w) - 0.012);
    membrane = mem;
    return min(d, mem);
}

// ================================================================== shears
float shears(vec3 p) {
    vec3 A = normalize(vec3(-0.15, 1.0, 0.30));   // toward the tips
    vec3 N = normalize(vec3(0.25, 0.0, 1.0));
    vec3 B = normalize(cross(N, A));
    vec3 pivot = R_WR + normalize(vec3(-0.25, 0.85, 0.30)) * 0.42 + N * 0.12;
    vec3 q = p - pivot;
    float d = 1e9;
    for (int b = 0; b < 2; b++) {
        float sgn = b == 0 ? 1.0 : -1.0;
        float ang = sgn * 0.30;
        vec3 dir = A * cos(ang) + B * sin(ang);
        vec3 side = B * cos(ang) - A * sin(ang);
        float x = dot(q, dir), y = dot(q, side), z = dot(q, N) - sgn * 0.014;
        float len = 1.05;
        float along = clamp(x, 0.0, len);
        float w = 0.10 * pow(1.0 - along / len, 0.7) + 0.004;
        float blade = max(abs(y - sgn * w * 0.35) - w, abs(z) - 0.010);
        blade = max(blade, max(-x, x - len));
        d = min(d, blade);
        vec3 hc = -dir * 0.32 - side * sgn * 0.11;
        vec3 hq = q - hc;
        vec3 hl = vec3(dot(hq, dir), dot(hq, side), dot(hq, N));
        d = min(d, sdTorus(hl, vec2(0.12, 0.032)));
        d = min(d, sdLimb(q, vec3(0.0), hc + dir * 0.11, 0.035, 0.03));
    }
    d = min(d, length(q) - 0.05);
    return d;
}

// ================================================================== film, tape and a reel
// a ribbon swept along a curve: segments of thin oriented boxes
vec3 filmCurve(int k, float t) {
    float fk = float(k);
    float a0 = fk * 1.9;
    float R = 0.85 + 0.28 * fk;
    float ang = a0 + t * (2.6 + 0.3 * fk);
    vec3 c = vec3(0.0, -2.1, 1.0);
    return c + vec3(R * cos(ang), 0.05 + (0.62 + 0.40 * mod(fk, 2.0)) * pow(sin(t * 3.14159), 1.1), 0.55 * R * sin(ang));
}
vec3 tapeCurve(int k, float t) {
    float fk = float(k);
    float ang = fk * 2.3 + t * 7.0;
    float R = 0.9 + 0.5 * sin(t * 4.0 + fk) + 0.25 * fk;
    vec3 c = vec3(0.2 * fk - 0.3, -2.1, 0.8);
    return c + vec3(R * cos(ang), 0.03 + 0.45 * pow(sin(t * 3.14159), 2.0) * (0.6 + 0.4 * sin(fk + t * 9.0)), 0.6 * R * sin(ang));
}

float ribbon(vec3 p, vec3 a, vec3 b, float halfWidth, float twist, out vec2 st) {
    vec3 tg = b - a;
    float len = length(tg);
    tg /= len;
    vec3 side = normalize(cross(tg, vec3(0.0, 1.0, 0.0)) + vec3(0.0, 0.0001, 0.0));
    vec3 up = cross(side, tg);
    float c = cos(twist), s = sin(twist);
    vec3 bw = side * c + up * s;
    vec3 nn = cross(tg, bw);
    vec3 q = p - (a + b) * 0.5;
    float x = dot(q, tg), y = dot(q, bw), z = dot(q, nn);
    st = vec2(x, y);
    return max(max(abs(x) - len * 0.5 - 0.01, abs(y) - halfWidth), abs(z) - 0.008);
}

float film(vec3 p, out vec2 filmST, out float tapeD) {
    float d = 1e9;
    filmST = vec2(0.0);
    for (int k = 0; k < 4; k++) {
        vec3 a = filmCurve(k, 0.0);
        for (int s = 1; s <= 18; s++) {
            float t = float(s) / 18.0;
            vec3 b = filmCurve(k, t);
            vec2 st;
            float r = ribbon(p, a, b, 0.09, sin(t * 6.0 + float(k)) * 1.2, st);
            if (r < d) { d = r; filmST = st + vec2(t * 7.0 + float(k) * 3.0, 0.0); }
            a = b;
        }
    }
    tapeD = 1e9;
    for (int k = 0; k < 3; k++) {
        vec3 a = tapeCurve(k, 0.0);
        for (int s = 1; s <= 14; s++) {
            float t = float(s) / 14.0;
            vec3 b = tapeCurve(k, t);
            vec2 st;
            tapeD = min(tapeD, ribbon(p, a, b, 0.035, sin(t * 11.0 + float(k)) * 1.3, st));
            a = b;
        }
    }
    return d;
}

float reel(vec3 p) {
    // a film reel lying tilted in the debris, front left
    vec3 q = p - vec3(-1.25, -1.95, 1.75);
    q.yz = mat2(0.45, -0.89, 0.89, 0.45) * q.yz;
    q.xy = mat2(0.95, -0.31, 0.31, 0.95) * q.xy;
    float disc = max(length(q.xy) - 0.55, abs(q.z) - 0.02);
    float rim = sdTorus(q, vec2(0.55, 0.03));
    // three cut-outs
    float holes = 1e9;
    for (int i = 0; i < 3; i++) {
        float a = float(i) * 2.094 + 0.4;
        holes = min(holes, length(q.xy - 0.30 * vec2(cos(a), sin(a))) - 0.14);
    }
    disc = max(disc, -holes);
    disc = max(disc, -(length(q.xy) - 0.05));
    return min(min(disc, rim), max(length(q.xy) - 0.12, abs(q.z) - 0.05) + 0.0);
}

// ================================================================== scene
// materials: 1 skin, 2 eye, 3 steel, 4 claw, 5 membrane, 6 film, 7 tape, 8 reel, 9 ground
float map(vec3 p, out float mat, out vec2 st) {
    st = vec2(0.0);
    float claw;
    float d = body(p);
    d = smin(d, arms(p, claw), 0.10);
    d = smin(d, tentacles(p), 0.08);
    // skin relief
    float n = fbm(p * 3.0);
    d += (n - 0.5) * 0.05 + (noise(p * 14.0 + n * 3.0) - 0.5) * 0.012;
    mat = 1.0;
    float e = min(length(p - EYE_L), length(p - EYE_R)) - 0.058;
    if (e < d) { d = e; mat = 2.0; }
    if (claw < d) { d = claw; mat = 4.0; }
    float memL, memR;
    float wl = wing(p, -1.0, memL);
    float wr = wing(p, 1.0, memR);
    float wd = min(wl, wr);
    if (wd < d) { d = wd; mat = min(memL, memR) <= wd + 0.0001 ? 5.0 : 1.0; }
    float s = shears(p);
    if (s < d) { d = s; mat = 3.0; }
    vec2 fst;
    float tape;
    float f = film(p, fst, tape);
    if (f < d) { d = f; mat = 6.0; st = fst; }
    if (tape < d) { d = tape; mat = 7.0; }
    float r = reel(p);
    if (r < d) { d = r; mat = 8.0; }
    float g = p.y + 2.12 + 0.04 * fbm(p * 1.5);
    if (g < d) { d = g; mat = 9.0; }
    return d;
}
float mapD(vec3 p) { float m; vec2 s; return map(p, m, s); }

vec3 calcNormal(vec3 p) {
    const vec2 k = vec2(1.0, -1.0);
    const float h = 0.002;
    return normalize(k.xyy * mapD(p + k.xyy * h) + k.yyx * mapD(p + k.yyx * h) +
                     k.yxy * mapD(p + k.yxy * h) + k.xxx * mapD(p + k.xxx * h));
}
float ambientOcclusion(vec3 p, vec3 n) {
    float occ = 0.0, sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.02 + 0.10 * float(i);
        occ += (h - mapD(p + h * n)) * sca;
        sca *= 0.7;
    }
    return clamp(1.0 - 1.6 * occ, 0.0, 1.0);
}

// ================================================================== the colour bars, the light source
vec3 bars(vec2 uv) { // uv 0..1, y down
    vec3 c;
    float x = uv.x * 7.0;
    if (uv.y < 0.67) {
        if (x < 1.0) c = vec3(0.75);
        else if (x < 2.0) c = vec3(0.75, 0.75, 0.0);
        else if (x < 3.0) c = vec3(0.0, 0.75, 0.75);
        else if (x < 4.0) c = vec3(0.0, 0.75, 0.0);
        else if (x < 5.0) c = vec3(0.75, 0.0, 0.75);
        else if (x < 6.0) c = vec3(0.75, 0.0, 0.0);
        else c = vec3(0.0, 0.0, 0.75);
    } else if (uv.y < 0.75) {
        if (x < 1.0) c = vec3(0.0, 0.0, 0.75);
        else if (x < 2.0) c = vec3(0.075);
        else if (x < 3.0) c = vec3(0.75, 0.0, 0.75);
        else if (x < 4.0) c = vec3(0.075);
        else if (x < 5.0) c = vec3(0.0, 0.75, 0.75);
        else if (x < 6.0) c = vec3(0.075);
        else c = vec3(0.75);
    } else {
        float u = uv.x * 7.0;
        if (u < 1.25) c = vec3(0.0, 0.13, 0.30);
        else if (u < 2.5) c = vec3(1.0);
        else if (u < 3.75) c = vec3(0.20, 0.0, 0.42);
        else if (u < 5.0) c = vec3(0.075);
        else if (u < 5.333) c = vec3(0.035);
        else if (u < 5.667) c = vec3(0.075);
        else if (u < 6.0) c = vec3(0.115);
        else c = vec3(0.075);
    }
    return c;
}
// the pattern as a huge screen far behind, seen through fog
vec3 screenAt(vec2 suv) { return bars(clamp(suv, 0.0, 1.0)); }
vec3 FOG = vec3(0.10, 0.20, 0.21);

void main() {
    vec2 frag = gl_FragCoord.xy;
    vec2 uv = frag / res;
    vec2 suv = vec2(uv.x, 1.0 - uv.y);
    vec2 p2 = (2.0 * frag - res) / res.y;

    // low camera looking up at the giant
    vec3 ro = vec3(0.0, -1.35, 3.3);
    vec3 ta = vec3(0.0, 0.75, 0.0);
    vec3 ww = normalize(ta - ro);
    vec3 uu = normalize(cross(ww, vec3(0.0, 1.0, 0.0)));
    vec3 vv = cross(uu, ww);
    vec3 rd = normalize(p2.x * uu + p2.y * vv + 0.98 * ww);

    // backdrop: the bars glowing through mist, brightest behind the head
    vec2 halo = suv - vec2(0.5, 0.24);
    float glow = exp(-dot(halo, halo) * 11.0);
    vec3 screen = screenAt(suv);
    float mist = 0.55 + 0.45 * fbm(vec3(suv * vec2(3.0, 5.0), 1.3));
    vec3 col = (FOG * 0.15 + screen * (0.03 + 0.55 * glow * glow)) * mist + vec3(0.55, 0.75, 0.78) * glow * glow * 0.18;

    float t = 0.5, mat = 0.0;
    vec2 st;
    float eyeGlow = 0.0;
    float closest = 1e9;
    bool hit = false;
    for (int i = 0; i < 180; i++) {
        vec3 p = ro + rd * t;
        float d = map(p, mat, st);
        float ed = min(length(p - EYE_L), length(p - EYE_R));
        eyeGlow += exp(-ed * 26.0) * 0.03;
        closest = min(closest, d);
        if (d < 0.0008 * t) { hit = true; break; }
        t += d * 0.85;
        if (t > 16.0) break;
    }

    vec3 backL = normalize(vec3(0.0, 0.35, -1.0));    // from the screen, behind him
    vec3 topL = normalize(vec3(-0.3, 1.0, 0.25));
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        float ao = ambientOcclusion(p, n);
        float fre = pow(clamp(1.0 + dot(n, rd), 0.0, 1.0), 2.5);
        float back = max(dot(n, backL), 0.0);
        vec3 rimCol = screenAt(suv + n.xy * vec2(0.12, -0.12)) * 0.9 + vec3(0.25, 0.4, 0.42);
        float top = max(dot(n, topL), 0.0);
        vec3 h = normalize(topL - rd);
        if (mat < 1.5 || mat > 4.5 && mat < 5.5) {
            float det = fbm(p * 2.5 + vec3(4.0));
            vec3 alb = mix(vec3(0.014, 0.02, 0.016), vec3(0.06, 0.075, 0.058), det);
            col = alb * (vec3(0.5, 0.7, 0.72) * top * 0.6 + FOG * 0.25) * ao;
            col += rimCol * fre * (0.18 + 0.8 * back) * ao;
            col += vec3(0.55, 0.7, 0.68) * pow(max(dot(n, h), 0.0), 40.0) * 0.18 * ao;   // wet sheen
            if (mat > 4.5) {
                // light through the membrane: dark red with veins
                float vein = smoothstep(0.75, 0.95, 1.0 - abs(sin(dot(p, vec3(7.0, 5.0, 3.0)) + fbm(p * 3.0) * 6.0)));
                col *= 0.6;
                col += vec3(0.22, 0.03, 0.01) * pow(glow, 2.0) * (1.0 - 0.8 * vein) * (0.3 + 0.7 * fbm(p * 4.0)) + vec3(0.012, 0.004, 0.002);
            }
            // red glow from below
            col += alb * vec3(1.6, 0.15, 0.08) * max(-n.y, 0.0) * smoothstep(-0.5, -2.1, p.y) * 0.8;
        } else if (mat < 2.5) {
            col = vec3(1.6, 0.18, 0.03);
        } else if (mat < 3.5) {
            vec3 r = reflect(rd, n);
            vec3 env = mix(FOG * 0.5, screenAt(vec2(0.5 + r.x * 0.4, 0.4 - r.y * 0.4)) * 0.6, smoothstep(-0.2, 0.4, r.y));
            env = mix(vec3(dot(env, vec3(0.33))), env, 0.15);
            col = env * 0.9 + vec3(0.8, 0.9, 0.95) * pow(max(dot(n, h), 0.0), 200.0) * 2.5;
            col += rimCol * fre * 0.8;
        } else if (mat < 4.5) {
            col = vec3(0.10, 0.09, 0.07) * (0.2 + top) + rimCol * fre * 0.6 + vec3(0.7) * pow(max(dot(n, h), 0.0), 60.0) * 0.4;
        } else if (mat < 6.5) {
            // 35mm film: frames, sprocket holes, amber where the light comes through
            float u = st.x * 3.2;
            float v = st.y / 0.09;
            float frame = step(0.08, fract(u)) * step(abs(v), 0.68);
            float holes = step(0.82, abs(v)) * step(abs(v), 0.94) * step(0.5, fract(u * 4.0));
            vec3 emul = mix(vec3(0.025, 0.015, 0.006), vec3(0.50, 0.26, 0.06), frame * (0.3 + 0.7 * fbm(vec3(st * 6.0, 2.0))));
            emul *= 1.0 - holes;
            col = emul * (0.15 + 0.6 * glow + back * 1.2) + vec3(0.8, 0.85, 0.8) * pow(max(dot(n, h), 0.0), 300.0) * 0.25;
            col += rimCol * fre * 0.12;
            col *= ao;
        } else if (mat < 7.5) {
            // video tape: thin, glossy, dark brown
            col = vec3(0.03, 0.018, 0.01) + vec3(0.6, 0.5, 0.4) * pow(max(dot(n, h), 0.0), 90.0) * 0.8 + rimCol * fre * 0.45;
            col *= ao;
        } else if (mat < 8.5) {
            vec3 r = reflect(rd, n);
            col = vec3(0.08, 0.09, 0.1) * (0.3 + top) + vec3(0.7) * pow(max(dot(n, h), 0.0), 50.0) * 0.6 + rimCol * fre * 0.5;
            col *= ao;
        } else {
            col = vec3(0.012, 0.016, 0.016) * (0.4 + top) * ao;
            col += vec3(0.25, 0.02, 0.01) * 0.15 * fbm(p * 2.0);
        }
        // fog: thicker with distance and near the ground
        float fogAmt = 1.0 - exp(-0.07 * max(t - 4.5, 0.0));
        fogAmt = max(fogAmt, smoothstep(-1.6, -2.2, p.y) * 0.35 * (0.6 + 0.4 * fbm(p * 1.2)));
        vec3 fogCol = FOG * (0.3 + 1.0 * glow) + screen * 0.05;
        col = mix(col, fogCol, clamp(fogAmt, 0.0, 0.85));
    } else {
        // a dark aura hugging the silhouette
        col *= mix(0.35, 1.0, smoothstep(0.0, 0.35, closest));
    }

    // eyes glow into the mist
    col += vec3(1.0, 0.12, 0.02) * eyeGlow;
    // low drifting mist over everything
    float lowMist = smoothstep(0.55, 1.0, suv.y) * (0.5 + 0.5 * fbm(vec3(suv * vec2(4.0, 9.0), 7.0)));
    col = mix(col, FOG * 0.7, lowMist * 0.35);

    // scanlines, vignette, grain
    col *= 0.9 + 0.1 * step(0.5, fract(frag.y / 4.0));
    float vig = smoothstep(1.3, 0.35, length(uv - vec2(0.5, 0.5)) * 1.4);
    col *= mix(0.15, 1.0, vig);
    col += (hash(vec3(frag, 7.0)) - 0.5) * 0.03;

    col = max(col, 0.0) * 1.15;
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);   // ACES filmic
    col = pow(clamp(col, 0.0, 1.0), vec3(0.4545));
    // gentle teal grade
    col = mix(col, col * vec3(0.92, 1.03, 1.05), 0.6);

    vec2 q = abs(frag - res * 0.5) - (res * 0.5 - vec2(res.x * 0.2));
    float rr = res.x * 0.176;
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rr;
    float alpha = 1.0 - smoothstep(-1.0, 0.5, dist);
    col *= 1.0 - 0.6 * smoothstep(-8.0, -1.0, dist);
    gl_FragColor = vec4(col * alpha, alpha);
}
