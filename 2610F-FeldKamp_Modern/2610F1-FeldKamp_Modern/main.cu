// CUDA 12+ demo: original FeldKamp geometry/loop structure, modern texture object API.
#include <cuda_runtime.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>
#include <numeric>

#define CHECK(x) do { cudaError_t e=(x); if(e!=cudaSuccess) { \
 std::fprintf(stderr,"CUDA %s:%d: %s\n",__FILE__,__LINE__,cudaGetErrorString(e)); std::exit(1); } } while(0)

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
// Educational CPU preprocessing; the actual backprojection is the old GPU kernel.
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
    // Modern kernel includes explicit launch bounds check.
    int deviceCount=0;
    CHECK(cudaGetDeviceCount(&deviceCount));
    if(!deviceCount){std::fprintf(stderr,"NVIDIA CUDA GPU required\n");return 1;}

    std::vector<float2> angles(V);
    for(int k=0;k<V;++k){float a=float(2*PI*k/V);angles[k]=make_float2(std::sin(a),std::cos(a));}
    CHECK(cudaMemcpyToSymbol(g_fFeldKampSinTbl_d,angles.data(),sizeof(float2)*V));
    std::vector<float> coordinates(N);
    for(int i=0;i<N;++i) coordinates[i]=i-(N-1)*0.5f;
    CHECK(cudaMemcpyToSymbol(g_fFeldKampPixlX_d,coordinates.data(),sizeof(float)*N));
    CHECK(cudaMemcpyToSymbol(g_fFeldKampPixlY_d,coordinates.data(),sizeof(float)*N));
    float zpos[NZ]={-3.f,0.f,3.f};
    int starts[NZ]={0,0,0}, ends[NZ]={N-1,N-1,N-1};
    CHECK(cudaMemcpyToSymbol(g_fFeldKampZPos_d,zpos,sizeof(zpos)));
    CHECK(cudaMemcpyToSymbol(g_nFeldKampStatPixl_d,starts,sizeof(starts)));
    CHECK(cudaMemcpyToSymbol(g_nFeldKampEndPixl_d,ends,sizeof(ends)));

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
    cudaChannelFormatDesc desc=cudaCreateChannelDesc<float>();
    cudaArray_t texArray=nullptr;
    cudaExtent extent=make_cudaExtent(C,ROWS,V);
    CHECK(cudaMalloc3DArray(&texArray,&desc,extent));
    cudaMemcpy3DParms copy={};
    copy.srcPtr=make_cudaPitchedPtr(filtered.data(),C*sizeof(float),C,ROWS);
    copy.dstArray=texArray;
    copy.extent=extent;
    copy.kind=cudaMemcpyHostToDevice;
    CHECK(cudaMemcpy3D(&copy));

    // Texture coordinate centers: (channel+0.5, row+0.5, view+0.5).
    cudaResourceDesc resource={};
    resource.resType=cudaResourceTypeArray;
    resource.res.array.array=texArray;
    cudaTextureDesc textureDesc={};
    textureDesc.normalizedCoords=0;
    textureDesc.filterMode=cudaFilterModeLinear;
    textureDesc.readMode=cudaReadModeElementType;
    for(int i=0;i<3;++i) textureDesc.addressMode[i]=cudaAddressModeBorder;
    cudaTextureObject_t projectionTexture=0;
    CHECK(cudaCreateTextureObject(&projectionTexture,&resource,&textureDesc,nullptr));

    const size_t count=size_t(N)*N*NZ;
    float* deviceImage=nullptr;
    CHECK(cudaMalloc((void**)&deviceImage,count*sizeof(float)));
    CHECK(cudaMemset(deviceImage,0,count*sizeof(float)));
    dim3 block(16,16), grid(N/16,N/16);
    // Calls the modernized copy of YOUR function, preserving its math and nested loops.
    FeldKampBackProject_kernel<<<grid,block>>>(deviceImage,NZ,V,N,
        R,1.f,MID_C,1.f,R*R,MID_ROW,1,projectionTexture);
    CHECK(cudaGetLastError()); CHECK(cudaDeviceSynchronize());
    std::vector<float> image(count);
    CHECK(cudaMemcpy(image.data(),deviceImage,count*sizeof(float),cudaMemcpyDeviceToHost));
    CHECK(cudaFree(deviceImage));
    CHECK(cudaDestroyTextureObject(projectionTexture));
    CHECK(cudaFreeArray(texArray));

    // -exec source /mem/plot_input_data.py
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
