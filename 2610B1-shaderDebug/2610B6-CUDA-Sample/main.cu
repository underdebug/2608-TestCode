#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA Error: %s\n", cudaGetErrorString(err)); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

// Kernel implemented in kernel.cu
__global__ void vectorAdd(const float* A, const float* B, float* C, int N);

int main()
{
    constexpr int N = 1024;
    constexpr size_t bytes = N * sizeof(float);
    float* A = (float*)malloc(bytes);
    float* B = (float*)malloc(bytes);
    float* C = (float*)malloc(bytes);
    if (!A || !B || !C) {
        fprintf(stderr, "Host allocation failed\n");
        free(A); free(B); free(C);
        return EXIT_FAILURE;
    }
    for (int i = 0; i < N; ++i) {
        A[i] = static_cast<float>(i);
        B[i] = static_cast<float>(i * 2);
    }
    float *d_A = nullptr, *d_B = nullptr, *d_C = nullptr;
    CHECK(cudaMalloc(&d_A, bytes));
    CHECK(cudaMalloc(&d_B, bytes));
    CHECK(cudaMalloc(&d_C, bytes));
    CHECK(cudaMemcpy(d_A, A, bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_B, B, bytes, cudaMemcpyHostToDevice));
    const int threads = 256;
    const int blocks = (N + threads - 1) / threads;
    vectorAdd<<<blocks, threads>>>(d_A, d_B, d_C, N);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());
    CHECK(cudaMemcpy(C, d_C, bytes, cudaMemcpyDeviceToHost));
    for (int i = 0; i < 10; ++i) {
        printf("C[%d] = %.1f\n", i, C[i]);
    }
    CHECK(cudaFree(d_A));
    CHECK(cudaFree(d_B));
    CHECK(cudaFree(d_C));
    free(A); free(B); free(C);
    return 0;
}
