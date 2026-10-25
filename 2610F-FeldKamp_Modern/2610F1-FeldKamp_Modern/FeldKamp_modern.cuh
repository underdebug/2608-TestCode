const unsigned int MAX_IMAGE_SIZE = 1024;
const unsigned int MAX_ZPOS_SIZE = 256;
const unsigned int SIN_COS_TBL_LEN = 2320;

__constant__ float2 g_fFeldKampSinTbl_d[SIN_COS_TBL_LEN];
__constant__ float g_fFeldKampPixlX_d[MAX_IMAGE_SIZE];
__constant__ float g_fFeldKampPixlY_d[MAX_IMAGE_SIZE];
__constant__ float g_fFeldKampZPos_d[MAX_ZPOS_SIZE];
__constant__ int g_nFeldKampStatPixl_d[MAX_ZPOS_SIZE];
__constant__ int g_nFeldKampEndPixl_d[MAX_ZPOS_SIZE];

extern "C" __global__ void FeldKampBackProject_kernel(
        float *pfImage,const int nFeldKampProjectNum,
        const int nViewBackPro,const int nImgRowSize,
        const float fRadius,const float fSliceThickness,
        const float fMidChannelPar,const float fDetParT,
        const float fRadiusPower,const float fMidSlice,
        int nCouchMoveDirection, cudaTextureObject_t texFeldKampBckProDataIn)
{
	const  int ix = blockDim.x * blockIdx.x + threadIdx.x;
	const  int iy = blockDim.y * blockIdx.y + threadIdx.y;

    if (ix >= nImgRowSize || iy >= nImgRowSize) return;

	float fX = g_fFeldKampPixlX_d[ix];
	float fY = g_fFeldKampPixlY_d[iy];
	
	for(int n = 0;n < nFeldKampProjectNum; n++)
	{
		int nBase;
		if (1 == nCouchMoveDirection)
		{
			nBase = n;
		}
		else
		{
			nBase = nFeldKampProjectNum - 1 - n;
		}

		float fZ = g_fFeldKampZPos_d[n];
		int nStartPixel = g_nFeldKampStatPixl_d[n];
		int nEndPixel   = g_nFeldKampEndPixl_d[n];

        // -exec source /mem/print_g_nFeldKampStatPixl_d.py

		float fTemp = fRadius * fZ / fSliceThickness;
		if(ix <= nEndPixel && ix >= nStartPixel)
		{
			if(iy <= nEndPixel && iy >= nStartPixel)
			{			
				float fData = 0;
				for(int k = 0; k < nViewBackPro; k++)
				{
                    // -exec cuda kernel block thread
                    // kernel 0, block (0,0,0), thread (0,2,0)

                    // -exec cuda device sm warp lane block thread
                    // -exec cuda kernel 0 block (0,0,0) thread (0,2,0)

                    // -exec info cuda kernels
                    // -exec info cuda threads
                    // -exec info cuda blocks
                    // -exec info cuda warps
                    // -exec info cuda lanes

					float fSinTheta = g_fFeldKampSinTbl_d[k].x;
					float fCosTheta = g_fFeldKampSinTbl_d[k].y;

					float fChannelPos = fX * fCosTheta - fY * fSinTheta;
					float fSqrtRT = sqrtf(fRadiusPower - fChannelPos * fChannelPos);
					fChannelPos = fChannelPos * fDetParT + fMidChannelPar;
					float fV = -fY * fCosTheta -  fX * fSinTheta;
					float fDisTemp = fSqrtRT + fV;
					float fSlicePos = fTemp / fDisTemp + fMidSlice;

					float fView = k + 0.5f;
					fData += tex3D<float>(texFeldKampBckProDataIn,fChannelPos, fSlicePos,fView);
							
				}
				int nImgSizePower = nImgRowSize * nImgRowSize;
				pfImage[nBase * nImgSizePower + iy * nImgRowSize + ix] = fData;
			}
		}    
	}
}

// 命令（在前面加 -exec）
//
// 命令                功能
// ------------------  ----------------------------------------------
// info cuda threads   查看 GPU 线程及当前 PC、源代码行
// info cuda blocks    查看正在运行的 CUDA Blocks
// info cuda warps     查看 GPU Warps、SM 和运行位置
// info cuda lanes     查看当前 Warp 的 Lane
// info cuda kernels   查看 Kernel、启动参数和 Grid/Block 尺寸
// info cuda devices   查看 GPU 设备信息
// info cuda sms       查看 SM 的运行状态
// info cuda contexts  查看 CUDA Context
// info locals         查看当前线程的局部变量
// info args           查看当前函数的参数
// bt                  查看当前线程的调用堆栈
// info registers      查看当前线程的寄存器
// info line           查看当前源代码行对应的地址
// x/10i $pc           查看从当前 PC 开始的 10 条 GPU 指令
