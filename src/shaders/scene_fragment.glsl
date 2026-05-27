#version 120
varying vec2 v_texcoord;

// === 背景与几何 ===
uniform sampler2D u_blurredTex;
uniform vec2 u_resolution;
uniform vec2 u_objCenter;     // NDC [-1,1]
uniform vec2 u_objHalfSize;   // NDC half-size
uniform float u_powerFactor;

// === 折射参数 ===
uniform float u_a;
uniform float u_b;
uniform float u_c;
uniform float u_d;
uniform float u_fPower;

// === 材质参数 ===
uniform float u_noise;        // 默认极低（0.01），仅用于消除色带
uniform float u_time;

// === Smooth-union 桥接 ===
uniform int u_numConnections;
uniform vec2 u_connCenterB[8];
uniform vec2 u_connHalfSizeB[8];
uniform float u_connPowerB[8];
uniform float u_connStrength[8];

// === 液态玻璃控件 ===
uniform vec3 u_tintColor;
uniform float u_tintStrength;
uniform float u_opacity;
uniform float u_rippleTime;      // -1 = no ripple
uniform vec2 u_rippleCenter;     // local coords [0,1]
uniform float u_flowSpeed;       // 0 = no flow
uniform float u_deformAmount;    // 0 = no deform

const float M_E = 2.718281828459045;

// 超椭圆 SDF —— 形状基础
float sdSuperellipse(vec2 p, float n, float r) {
    vec2 p_abs = abs(p);
    float numerator = pow(p_abs.x, n) + pow(p_abs.y, n) - pow(r, n);
    float den_x = pow(p_abs.x, 2.0 * n - 2.0);
    float den_y = pow(p_abs.y, 2.0 * n - 2.0);
    float denominator = n * sqrt(den_x + den_y) + 0.00001;
    return numerator / denominator;
}

// 折射强度曲线
float refractionF(float x) {
    return 1.0 - u_b * pow(u_c * M_E, -u_d * x - u_a);
}

// 简单哈希噪声（用于极低强度去色带）
float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// 平滑并集 —— 让相邻形状融合成连续液面
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

void main() {
    vec2 fragNDC = v_texcoord * 2.0 - 1.0;

    // 局部坐标 [-1, +1]
    vec2 localP = (fragNDC - u_objCenter) / u_objHalfSize;

    float d = sdSuperellipse(localP, u_powerFactor, 1.0);

    // === Smooth-union 桥接（多形状融合）===
    float combinedD = d;
    for (int i = 0; i < 8; i++) {
        if (i >= u_numConnections) break;
        vec2 otherLocalP = (fragNDC - u_connCenterB[i]) / u_connHalfSizeB[i];
        float otherD = sdSuperellipse(otherLocalP, u_connPowerB[i], 1.0);
        float k = 0.35 * u_connStrength[i] + 0.001;
        combinedD = smin(combinedD, otherD, k);
    }

    if (combinedD > 0.0)
        discard;

    // Voronoi ownership: only render pixels "owned" by this object
    for (int i = 0; i < 8; i++) {
        if (i >= u_numConnections) break;
        vec2 otherLocalP = (fragNDC - u_connCenterB[i]) / u_connHalfSizeB[i];
        float otherD = sdSuperellipse(otherLocalP, u_connPowerB[i], 1.0);
        if (otherD < d) {
            discard;
        }
    }

    float dist = -combinedD;

    // === 按压涟漪（仅交互时激活，幅度极柔）===
    if (u_rippleTime >= 0.0) {
        float timeSincePress = u_time - u_rippleTime;
        vec2 rippleVec = localP * 0.5 + 0.5 - u_rippleCenter;
        float rippleDist = length(rippleVec);
        float rippleWave = sin(rippleDist * 30.0 - timeSincePress * 8.0)
                         * exp(-timeSincePress * 3.0)
                         * exp(-rippleDist * 4.0) * 0.008;
        dist += rippleWave;
    }

    // === 悬停流动（仅交互时激活，几乎不可见的微扰）===
    if (u_flowSpeed > 0.0) {
        vec2 flowP = localP * 3.0 + u_time * u_flowSpeed * 0.5;
        float flow = sin(flowP.x * 2.7 + flowP.y * 1.3)
                   * sin(flowP.y * 3.1 - flowP.x * 0.7) * 0.001 * u_flowSpeed;
        dist += flow;
    }

    // === 折射 UV 计算 ===
    vec2 sampleP = localP * pow(refractionF(dist), u_fPower);
    vec2 targetNDC = sampleP * u_objHalfSize + u_objCenter;
    vec2 texCoord = clamp(targetNDC * 0.5 + 0.5, 0.0, 1.0);

    // === 苹果液态玻璃材质 ===

    // 1. 干净的单次模糊采样 + 极微噪声（消色带）
    vec4 color = texture2D(u_blurredTex, texCoord);
    vec4 noiseVal = vec4(vec3(rand(gl_FragCoord.xy * 0.001 + u_time * 0.01) - 0.5), 0.0);
    color += noiseVal * u_noise;

    // 2. 凸面玻璃渐变（穹顶光照：上略亮，下略暗）
    float convex = 0.5 + 0.5 * localP.y;
    float domeLight = mix(0.93, 1.07, convex);
    color.rgb *= domeLight;

    // 3. 玻璃色调柔和混合（保留底层亮度结构）
    color.rgb = mix(color.rgb, u_tintColor * (0.5 + 0.5 * color.rgb), u_tintStrength);

    // 4. 极细白色边框线（约 0.5–1 px 边缘反光）
    float fw = clamp(fwidth(combinedD), 0.0003, 0.003);
    float borderLine = smoothstep(fw * 2.5, fw * 0.5, dist);
    color.rgb += vec3(1.0) * borderLine * 0.3;

    // 5. 干净锐利的 alpha 抗锯齿
    float alpha = smoothstep(0.0, fw * 1.2, dist);
    color.a = alpha * u_opacity;

    gl_FragColor = color;
}
