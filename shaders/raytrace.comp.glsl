#version 460

// Simple compute path tracer with lambert/metal/dielectric materials, depth-of-field camera, multi-frame accumulation.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

struct Sphere
{
    vec4 centerRadius; // xyz center, w radius
    vec4 albedo; // xyz albedo
    vec4 misc; // x material (0 lambert, 1 metal, 2 dielectric), y fuzz, z refIdx
};

layout(std430, binding = 2) buffer Spheres
{
    Sphere spheres[];
};

layout(std140, binding = 3) uniform Params
{
    vec4 originLens; // xyz origin, w lens radius
    vec4 lowerLeft; // xyz lower-left
    vec4 horizontal; // xyz horizontal
    vec4 vertical; // xyz vertical
    vec4 u; // camera basis
    vec4 v;
    vec4 w;
    uvec4 frameSampleDepthCount; // x = frame, y = samples per frame, z = max depth, w = sphere count
    vec4 resolution; // x = width, y = height
    vec4 invResolution; // x = 1 / width, y = 1 / height
} params;

layout(binding = 0, rgba32f) uniform image2D accumImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

// ----------RNG----------
uint hash(uvec3 value)
{
    value = (value ^ (value >> 17u)) * 0xED5AD4BBu;
    value = (value ^ (value >> 11u)) * 0xAC4C1B51u;
    value = (value ^ (value >> 15u)) * 0x31848BABu;

    return value.x ^ value.y ^ value.z;
}

uint lcg(inout uint state)
{
    state = 1664525u * state + 1013904223u;
    return state;
}

float rand(inout uint state)
{
    return float(lcg(state) & 0x00FFFFFFu) / float(0x01000000u);
}

vec3 randomInUnitSphere(inout uint state)
{
    vec3 point;

    do
    {
        point = vec3(rand(state), rand(state), rand(state)) * 2.0 - 1.0;
    }
    while (dot(point, point) >= 1.0);

    return point;
}

vec3 randomUnitVector(inout uint state)
{
    return normalize(randomInUnitSphere(state));
}

vec3 randomInUnitDisk(inout uint state)
{
    vec3 point;

    do
    {
        point = vec3(rand(state), rand(state), 0.0) * 2.0 - vec3(1.0, 1.0, 0.0);
    }
    while (dot(point.xy, point.xy) >= 1.0);

    return point;
}

// ----------Math Helpers----------
struct Ray
{
    vec3 o;
    vec3 d;
};

struct Hit
{
    bool hit;
    float t;
    vec3 p;
    vec3 n;
    bool frontFace;
    uint mat;
    float fuzz;
    float refIdx;
    vec3 albedo;
    float flags;
};

bool hitSphere(const Sphere sphere, const Ray ray, float tMin, float tMax, out Hit hitRecord)
{
    vec3 oc = ray.o - sphere.centerRadius.xyz;
    float a = dot(ray.d, ray.d);
    float halfB = dot(oc, ray.d);
    float c = dot(oc, oc) - sphere.centerRadius.w * sphere.centerRadius.w;
    float discriminant = halfB * halfB - a * c;

    if (discriminant < 0.0)
    {
        return false;
    }

    float sqrtDisc = sqrt(discriminant);
    float root = (-halfB - sqrtDisc) / a;

    if (root < tMin || root > tMax)
    {
        root = (-halfB + sqrtDisc) / a;

        if (root < tMin || root > tMax)
        {
            return false;
        }
    }

    hitRecord.t = root;
    hitRecord.p = ray.o + root * ray.d;
    hitRecord.n = (hitRecord.p - sphere.centerRadius.xyz) / sphere.centerRadius.w;
    hitRecord.frontFace = dot(ray.d, hitRecord.n) < 0.0;

    if (!hitRecord.frontFace)
    {
        hitRecord.n = -hitRecord.n;
    }

    hitRecord.mat = uint(sphere.misc.x + 0.5);
    hitRecord.fuzz = sphere.misc.y;
    hitRecord.refIdx = sphere.misc.z;
    hitRecord.albedo = sphere.albedo.xyz;
    hitRecord.flags = sphere.misc.w;
    hitRecord.hit = true;

    return true;
}

bool scatterLambert(const Hit hitData, inout vec3 attenuation, inout Ray nextRay, inout uint rng)
{
    vec3 target = hitData.p + hitData.n + randomUnitVector(rng);
    nextRay.o = hitData.p + hitData.n * 0.001;
    nextRay.d = normalize(target - hitData.p);

    vec3 albedo = hitData.albedo;

    if (hitData.flags > 0.5)
    {
        float checkerIndex = floor(hitData.p.x) + floor(hitData.p.z);
        float checker = mod(checkerIndex, 2.0);
        float tone = (checker < 1.0) ? 0.05 : 0.95;
        albedo *= tone;
    }

    attenuation *= albedo;

    return true;
}

bool refractVec(vec3 incident, vec3 normal, float eta, out vec3 refracted)
{
    float cosTheta = min(dot(-incident, normal), 1.0);
    vec3 refractPerp = eta * (incident + cosTheta * normal);
    float perpDot = dot(refractPerp, refractPerp);

    if (perpDot > 1.0)
    {
        return false;
    }

    vec3 refractParallel = -sqrt(1.0 - perpDot) * normal;
    refracted = refractPerp + refractParallel;
    return true;
}

float schlick(float cosine, float refIdx)
{
    float r0 = (1.0 - refIdx) / (1.0 + refIdx);
    r0 *= r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

bool scatterMetal(const Hit hitData, inout vec3 attenuation, inout Ray nextRay, inout uint rng)
{
    vec3 reflected = reflect(normalize(nextRay.d), hitData.n);

    nextRay.o = hitData.p + hitData.n * 0.001;
    nextRay.d = reflected + hitData.fuzz * randomInUnitSphere(rng);
    attenuation *= hitData.albedo;

    return dot(nextRay.d, hitData.n) > 0.0;
}

bool scatterDielectric(const Hit hitData, inout vec3 attenuation, inout Ray nextRay, inout uint rng)
{
    attenuation *= vec3(1.0);

    float etaRatio = hitData.frontFace ? (1.0 / hitData.refIdx) : hitData.refIdx;
    vec3 unitDir = normalize(nextRay.d);
    float cosTheta = min(dot(-unitDir, hitData.n), 1.0);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 direction;
    bool cannotRefract = etaRatio * sinTheta > 1.0;

    if (cannotRefract || schlick(cosTheta, etaRatio) > rand(rng))
    {
        direction = reflect(unitDir, hitData.n);
    }
    else if (!refractVec(unitDir, hitData.n, etaRatio, direction))
    {
        direction = reflect(unitDir, hitData.n);
    }

    nextRay.o = hitData.p + direction * 0.001;
    nextRay.d = direction;

    return true;
}

vec3 traceRay(Ray ray, inout uint rng)
{
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);
    uint maxDepth = params.frameSampleDepthCount.z;

    for (uint depth = 0u; depth < maxDepth; ++depth)
    {
        float closest = 1e20;
        Hit best;
        best.hit = false;

        uint sphereCount = params.frameSampleDepthCount.w;

        for (uint i = 0u; i < sphereCount; ++i)
        {
            Hit hitCandidate;

            if (hitSphere(spheres[i], ray, 0.001, closest, hitCandidate))
            {
                closest = hitCandidate.t;
                best = hitCandidate;
            }
        }

        if (!best.hit)
        {
            vec3 unitDir = normalize(ray.d);
            float t = 0.5 * (unitDir.y + 1.0);
            vec3 sky = mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
            radiance += throughput * sky;

            break;
        }

        Ray nextRay = ray;
        bool scattered = false;

        if (best.mat == 0u)
        {
            scattered = scatterLambert(best, throughput, nextRay, rng);
        }
        else if (best.mat == 1u)
        {
            scattered = scatterMetal(best, throughput, nextRay, rng);
        }
        else
        {
            scattered = scatterDielectric(best, throughput, nextRay, rng);
        }

        if (!scattered)
        {
            break;
        }

        ray = nextRay;
    }

    return radiance;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);

    if (pixel.x >= int(params.resolution.x) || pixel.y >= int(params.resolution.y))
    {
        return;
    }

    uint frame = params.frameSampleDepthCount.x;
    uint spp = max(params.frameSampleDepthCount.y, 1u);

    uvec3 seedBase = uvec3(pixel, frame);
    uint rng = hash(seedBase);

    vec3 accum = vec3(0.0);

    for (uint sampleIndex = 0u; sampleIndex < spp; ++sampleIndex)
    {
        float uCoord = (float(pixel.x) + rand(rng)) * params.invResolution.x;

        // Flip Y so image is upright (swapchain origin top-left, camera expects bottom-left).
        float vCoord = (float(params.resolution.y - 1u - pixel.y) + rand(rng)) * params.invResolution.y;

        vec3 randomDisk = params.originLens.w * randomInUnitDisk(rng);
        vec3 offset = params.u.xyz * randomDisk.x + params.v.xyz * randomDisk.y;

        vec3 dir = params.lowerLeft.xyz + uCoord * params.horizontal.xyz + vCoord * params.vertical.xyz - params.originLens.xyz - offset;
        Ray ray;
        ray.o = params.originLens.xyz + offset;
        ray.d = normalize(dir);

        accum += traceRay(ray, rng);
    }

    vec3 prev = imageLoad(accumImage, pixel).rgb;
    vec3 total = prev + accum;
    imageStore(accumImage, pixel, vec4(total, 1.0));

    float sampleCount = float((frame + 1u) * params.frameSampleDepthCount.y);
    vec3 color = total / sampleCount;
    color = color / (color + vec3(1.0)); // simple tonemap
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    imageStore(outputImage, pixel, vec4(color, 1.0));
}