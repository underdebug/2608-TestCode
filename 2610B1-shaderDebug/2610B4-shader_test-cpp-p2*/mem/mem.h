
#include <stdio.h>
#include <string>
#include <vector>
#include <list>
#include <map>

struct Elem
{
    int value = 0;
    int index = 1;
    struct Elem* next = NULL;
};


class Points
{
public:
    Points()
    {
        m_coords.push_back(0.1);
        m_coords.push_back(0.2);
        m_coords.push_back(0.3);
    }

private:
    std::vector<double> m_coords;
};

struct Vec3_t
{
    float x, y, z;

    __host__ __device__
    Vec3_t() : x(0), y(0), z(0) {}

    __host__ __device__
    Vec3_t(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    __host__ __device__
    Vec3_t operator+(const Vec3_t& b) const
    {
        return Vec3_t(x + b.x, y + b.y, z + b.z);
    }

    __host__ __device__
    Vec3_t operator-(const Vec3_t& b) const
    {
        return Vec3_t(x - b.x, y - b.y, z - b.z);
    }

    __host__ __device__
    Vec3_t operator*(float s) const
    {
        return Vec3_t(x * s, y * s, z * s);
    }
};

struct Triangle_t
{
    Vec3_t v0;
    Vec3_t v1;
    Vec3_t v2;
};

__global__ void kernelNearest(float* data1, int size1, float* data2, int size2, int* index2, Triangle_t* triangles)
{
    int i1 = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = gridDim.x * blockDim.x;

    for(; i1 < size1; i1 += stride){
        float best = fabsf(data1[i1] - data2[0]);
        int bestI2 = 0;

        for (int b = 1; b < size2; ++b) {
            float diff = fabsf(data1[i1] - data2[b]);
            if (diff < best) {
                best = diff;
                bestI2 = b;
            }
        }

        index2[i1] = bestI2;
    }
}



__host__ __device__
float dot(const Vec3_t& a, const Vec3_t& b)
{
    return a.x * b.x +
           a.y * b.y +
           a.z * b.z;
}

__host__ __device__
Vec3_t cross(const Vec3_t& a, const Vec3_t& b)
{
    return Vec3_t(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

// ============================================================
// Triangle
// ============================================================
#include <vector>

int mem() 
{
    int i = 1;
    float f = 1.0f;
    double d = 2.0;


    Elem e1;
    e1.value = 1;

    Elem e2;
    e2.value = 2;

    e1.next = &e2;

    Elem* e = &e1;
    while(e){
        printf("e.value = %d, e.next = %p\n", e->value, (void*)e->next);
        e = e->next;
    }

    // std::string
    std::string s1("hello");
    std::string s2("world");
    std::string s3 = s1 + " " + s2;
    printf("s3 = %s (len=%zu)\n", s3.c_str(), s3.size());

    // std::vector<T>
    std::vector<int> v;
    for (int i = 0; i < 5; ++i) {
        v.push_back(i * i);
    }
    printf("vector:");
    for (size_t i = 0; i < v.size(); ++i) {
        printf(" %d", v[i]);
    }
    printf("\n");

    // std::list<T>
    std::list<int> l;
    l.push_back(1);
    l.push_back(2);
    l.push_front(0);
    printf("list:");
    for (std::list<int>::iterator it = l.begin(); it != l.end(); ++it) {
        printf(" %d", *it);
    }
    printf("\n");

    // std::map<Key, Val>
    std::map<std::string, int> ages;
    ages[std::string("alice")] = 4;
    ages[std::string("bob")] = 25;
    printf("alice = %d, bob = %d\n", ages[std::string("alice")], ages[std::string("bob")]);

    std::map<std::string, int>::iterator found = ages.find(std::string("bob"));
    if (found != ages.end()) {
        printf("found bob = %d\n", found->second);
    }
    for (std::map<std::string, int>::iterator it = ages.begin(); it != ages.end(); ++it) {
        printf("  %s -> %d\n", it->first.c_str(), it->second);
    }

    double * a = (double*) calloc(sizeof(double), 5);
    a[0] = 1;
    a[1] = 2;

    double b[3] = {3, 4, 7};

    double b2[2][2] = {{3, 4}, {7, 8}};

    //----------------------------------------------------------------

    Points m_points;

    std::vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);


    std::vector<Triangle_t> triangles =
    {
        {
            Vec3_t(-1.0f, -1.0f, 5.0f),
            Vec3_t( 1.0f, -1.0f, 5.0f),
            Vec3_t( 0.0f,  1.0f, 5.0f)
        },

        {
            Vec3_t(2.0f, -1.0f, 8.0f),
            Vec3_t(4.0f, -1.0f, 8.0f),
            Vec3_t(3.0f,  1.0f, 8.0f)
        }
    };

    std::vector<Vec3_t> points;
    points.push_back(Vec3_t(1.1f, 1.2f, 1.3f));
    points.push_back(Vec3_t(2.1f, 2.2f, 2.3f));
    points.push_back(Vec3_t(3.1f, 3.2f, 3.3f));
    points.push_back(Vec3_t(4.1f, 4.2f, 4.3f));

    Triangle_t* d_triangles;

    cudaMalloc(
        &d_triangles,
        triangles.size() * sizeof(Triangle_t)
    );

    cudaMemcpy(
        d_triangles,
        triangles.data(),
        triangles.size() * sizeof(Triangle_t),
        cudaMemcpyHostToDevice
    );

        //----------------------------------------------------------------
    // kernelNearest test data
    float h_data1[20] = {
        1.0f, 2.5f, 3.3f, 7.0f, 9.9f, 0.2f, 4.4f, 5.5f, 6.6f, 8.8f,
        10.1f, 11.3f, 12.7f, 13.2f, 14.9f, 15.4f, 16.8f, 17.1f, 18.6f, 19.0f
    };
    
    float h_data2[4] = {
        1.1f, 3.0f, 8.0f, 0.5f
    };
    int size1 = 20, size2 = 4;
    int h_index2[20] = {0};

    float *d_data1, *d_data2;
    int *d_index2;
    cudaMalloc(&d_data1, size1 * sizeof(float));
    cudaMalloc(&d_data2, size2 * sizeof(float));
    cudaMalloc(&d_index2, size1 * sizeof(int));

    cudaMemcpy(d_data1, h_data1, size1 * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_data2, h_data2, size2 * sizeof(float), cudaMemcpyHostToDevice);

    int threads = 256;
    int blocks = 10; //(size1 + threads - 1) / threads;
    kernelNearest<<<blocks, threads>>>(d_data1, size1, d_data2, size2, d_index2, d_triangles);
    cudaDeviceSynchronize();

    cudaMemcpy(h_index2, d_index2, size1 * sizeof(int), cudaMemcpyDeviceToHost);

    printf("nearest:");
    for (int i = 0; i < size1; ++i) {
        printf(" data1[%d]=%.1f -> data2[%d]=%.1f", i, h_data1[i], h_index2[i], h_data2[h_index2[i]]);
    }
    printf("\n");

    printf("mem finish");
    cudaFree(d_data1);
    cudaFree(d_data2);
    cudaFree(d_index2);

    return 0;    
};