// CPU test driver matching main.cu; no CUDA SDK required.
// The extension supplies host types, launch indices, and texture sampling.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "kernel.h"
#include "FeldKamp_modern.cuh"

constexpr int N=128, C=192, ROWS=16, V=180, NZ=3;
constexpr double PI=3.1415926535897932384626433832795;
constexpr float R=10000.f; // large radius makes the old geometry near parallel beam
constexpr float MID_C = C*0.5f; // CUDA unnormalized texture texel centers c+0.5
constexpr float MID_ROW = ROWS*0.5f;

// Synthetic thorax in voxel units, not patient CT data. Signed ellipsoids
// describe tissue contrasts; their sum is a nonnegative attenuation phantom.
struct Ellipsoid {
    double x, y, z, rx, ry, rz, angle, attenuation;
};
static const std::vector<Ellipsoid> chest = [] {
    std::vector<Ellipsoid> parts = {
        {  0,  0,  0, 53, 39, 75,  0,  1.00}, // torso / chest wall
        {  0,  0,  0, 49, 35, 70,  0, -0.10}, // inner soft tissue
        {-23,  3,  2, 18, 27, 55, -8, -0.72}, // right lung
        { 23,  4,  4, 17, 26, 53, 10, -0.72}, // left lung
        { -7,-10, -5, 14, 17, 28,-18,  0.18}, // heart / mediastinum
        {  0, 27,  0,  7,  7, 60,  0,  0.95}, // vertebral body
        {  0, 27,  0,  4,  4, 60,  0, -0.65}, // cancellous bone
        {  0, 35,  0,  3,  4, 58,  0,  0.70}, // spinous process
        {  0,-35,  0,  3,  2, 55,  0,  0.85}, // sternum
        {  5, 17,  0,  3,  3, 60,  0,  0.15}, // descending aorta
        {  0, 10,  6,  3,  4, 38,  0, -0.65}, // trachea
        {-24,  4,  0,  2,  3, 40,-15,  0.25}, // pulmonary vessels
        { 24,  5,  0,  2,  3, 40, 15,  0.25},
    };
    // Rib cross-sections around the thoracic wall, with cortical shells.
    for (int side : {-1, 1}) for (int rib = 0; rib < 8; ++rib) {
        double angle = (-65.0 + rib * 19.0) * PI / 180.0;
        double x = side * 48.0 * std::cos(angle), y = 34.0 * std::sin(angle);
        parts.push_back({x, y, 0, 2.5, 1.8, 36, side * angle * 180 / PI, 0.85});
        parts.push_back({x, y, 0, 1.4, 0.9, 36, side * angle * 180 / PI,-0.50});
    }
    return parts;
}();

// Exact parallel-ray integral through the rotated 3D ellipsoids. This matches
// the existing large-radius, near-parallel reconstruction geometry.
static double projection(double s, double theta, double z) {
    const double nx = std::cos(theta), ny = -std::sin(theta);
    const double dx = std::sin(theta), dy = std::cos(theta);
    double sum = 0;
    for (const auto& e : chest) {
        double axial = (z - e.z) / e.rz;
        if (std::abs(axial) >= 1) continue;
        double angle = e.angle * PI / 180.0;
        double ca = std::cos(angle), sa = std::sin(angle);
        double px = s * nx - e.x, py = s * ny - e.y;
        double ux = (ca * px + sa * py) / e.rx;
        double uy = (-sa * px + ca * py) / e.ry;
        double vx = (ca * dx + sa * dy) / e.rx;
        double vy = (-sa * dx + ca * dy) / e.ry;
        double a = vx * vx + vy * vy;
        double b = ux * vx + uy * vy;
        double c = ux * ux + uy * uy + axial * axial - 1;
        double discriminant = b * b - a * c;
        if (discriminant > 0)
            sum += e.attenuation * 2 * std::sqrt(discriminant) / a;
    }
    return sum;
}

// O(N^2) DFT ramp filter, padded to suppress circular wrap-around.
// Same CPU preprocessing as main.cu; backprojection runs on the host here.
static std::vector<float> rampFilter(const std::vector<double>& input) {
    const int L=512;
    std::vector<double> real(L,0), imag(L,0), output(L,0);
    const int offset=(L-C)/2;
    for(int k=0;k<L;++k) {
        for(int j=0;j<C;++j) {
            double phi=-2.0*PI*k*(j+offset)/L;
            real[k]+=input[j]*std::cos(phi);
            imag[k]+=input[j]*std::sin(phi);
        }
        double omega=2*PI*std::min(k,L-k)/L;
        real[k]*=omega;
        imag[k]*=omega;
    }
    for(int j=0;j<C;++j) {
        double val=0;
        for(int k=0;k<L;++k) {
            double phi=2.0*PI*k*(j+offset)/L;
            val+=real[k]*std::cos(phi)-imag[k]*std::sin(phi);
        }
        output[j+offset]=val/L;
    }
    std::vector<float> result(C);
    for(int j=0;j<C;++j) result[j]=float(output[j+offset]);
    return result;
}

int main() {
    static_assert(N <= MAX_IMAGE_SIZE && NZ <= MAX_ZPOS_SIZE
                  && V <= SIN_COS_TBL_LEN, "Kernel table capacity exceeded");

    std::vector<float2> angles(V);
    for (int k = 0; k < V; ++k) {
        float a = float(2 * PI * k / V);
        angles[k] = make_float2(std::sin(a), std::cos(a));
    }
    std::copy(angles.begin(), angles.end(), g_fFeldKampSinTbl_d);
    std::vector<float> coordinates(N);
    for (int i = 0; i < N; ++i) coordinates[i] = i - (N - 1) * 0.5f;
    std::copy(coordinates.begin(), coordinates.end(), g_fFeldKampPixlX_d);
    std::copy(coordinates.begin(), coordinates.end(), g_fFeldKampPixlY_d);
    float zpos[NZ] = {-3.f, 0.f, 3.f};
    int starts[NZ] = {0, 0, 0}, ends[NZ] = {N - 1, N - 1, N - 1};
    std::copy(zpos, zpos + NZ, g_fFeldKampZPos_d);
    std::copy(starts, starts + NZ, g_nFeldKampStatPixl_d);
    std::copy(ends, ends + NZ, g_nFeldKampEndPixl_d);

    // Texture layout [view][detector row][channel]. CPU data index likewise.
    std::vector<float> raw(size_t(V)*ROWS*C), filtered(raw.size());
    std::vector<double> line(C);
    for(int k=0;k<V;++k){
        double theta=2*PI*k/V;
        for(int r=0;r<ROWS;++r){
            double z=r-(ROWS-1)*0.5;
            for(int c=0;c<C;++c){
                double s=c-(C-1)*0.5;
                line[c]=projection(s,theta,z);
                raw[(size_t(k)*ROWS+r)*C+c]=float(line[c]);
            }
            auto result=rampFilter(line);
            for(int c=0;c<C;++c) filtered[(size_t(k)*ROWS+r)*C+c]=result[c];
        }
    }
    // Same [view][row][channel] layout and texel centers as main.cu.
    // Full precision CPU interpolation can differ slightly from CUDA hardware.
    const CpuTexture3D texArray{filtered.data(), C, ROWS, V};
    const cudaTextureObject_t projectionTexture = &texArray;
    const size_t count = size_t(N) * N * NZ;
    std::vector<float> image(count, 0.f);
    float* deviceImage = image.data(); // Host buffer, named like main.cu for debugging.
    dim3 block(16, 16), grid((N + 15) / 16, (N + 15) / 16);
    blockDim = block;
    // Visit pixels clockwise in square rings, starting at the center.
    const int center = N / 2;
    auto backProjectPixel = [&](int x, int y) {
        if (x < 0 || x >= N || y < 0 || y >= N) return;
        blockIdx.x = x / block.x;
        blockIdx.y = y / block.y;
        threadIdx.x = x % block.x;
        threadIdx.y = y % block.y;
        FeldKampBackProject_kernel(deviceImage, NZ, V, N,
            R, 1.f, MID_C, 1.f, R * R, MID_ROW, 1, projectionTexture);
    };
    backProjectPixel(center, center);
    for (int radius = 1; radius <= center; ++radius) {
        const int low = center - radius, high = center + radius;
        for (int x = low; x < high; ++x) backProjectPixel(x, low);
        for (int y = low; y < high; ++y) backProjectPixel(high, y);
        for (int x = high; x > low; --x) backProjectPixel(x, high);
        for (int y = high; y > low; --y) backProjectPixel(low, y);
    }

    const auto range = std::minmax_element(image.begin(), image.end());
    if (!std::all_of(image.begin(), image.end(),
                     [](float value) { return std::isfinite(value); })
        || *range.first == *range.second) {
        std::fputs("CPU reconstruction failed: nonfinite or constant image\n", stderr);
        return 1;
    }
    std::printf("CPU reconstruction: %d x %d x %d, range [%g, %g]\n",
                N, N, NZ, *range.first, *range.second);
    // Keep raw, filtered, and image in scope for the mem plotting scripts.

    // -exec source /mem/plot_input_data.py
    // -exec source /mem/plot_output_data.py
    // -exec info locals	
    // -exec info args	
    // -exec info registers	
    // -exec bt	
    // -exec bt full	
    // -exec info threads
    // -exec thread 2	
    // -exec p myVar	
    // -exec p/x myVar	
    // -exec x/16xb ptr	
    // -exec disassemble

    std::puts("Finished reconstruction. Use the mem plotting scripts at this breakpoint to save images in mem/figure.");
    return 0;
}
