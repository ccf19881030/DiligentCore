"#ifndef NON_POWER_OF_TWO\n"
"#define NON_POWER_OF_TWO 0\n"
"#endif\n"
"\n"
"#ifndef CONVERT_TO_SRGB\n"
"#define CONVERT_TO_SRGB 0\n"
"#endif\n"
"\n"
"#ifndef IMG_FORMAT\n"
"#define IMG_FORMAT rgba8\n"
"#endif\n"
"\n"
"layout(IMG_FORMAT) uniform writeonly image2DArray OutMip[4];\n"
"\n"
"uniform sampler2DArray SrcMip;\n"
"\n"
"uniform CB\n"
"{\n"
"    int SrcMipLevel;    // Texture level of source mip\n"
"    int NumMipLevels;   // Number of OutMips to write: [1, 4]\n"
"    int FirstArraySlice;\n"
"    int Dummy;\n"
"    vec2 TexelSize;     // 1.0 / OutMip1.Dimensions\n"
"};\n"
"\n"
"//\n"
"// The reason for separating channels is to reduce bank conflicts in the\n"
"// local data memory controller.  A large stride will cause more threads\n"
"// to collide on the same memory bank.\n"
"shared float gs_R[64];\n"
"shared float gs_G[64];\n"
"shared float gs_B[64];\n"
"shared float gs_A[64];\n"
"\n"
"void StoreColor( uint Index, vec4 Color )\n"
"{\n"
"    gs_R[Index] = Color.r;\n"
"    gs_G[Index] = Color.g;\n"
"    gs_B[Index] = Color.b;\n"
"    gs_A[Index] = Color.a;\n"
"}\n"
"\n"
"vec4 LoadColor( uint Index )\n"
"{\n"
"    return vec4( gs_R[Index], gs_G[Index], gs_B[Index], gs_A[Index]);\n"
"}\n"
"\n"
"float LinearToSRGB(float x)\n"
"{\n"
"    // This is exactly the sRGB curve\n"
"    //return x < 0.0031308 ? 12.92 * x : 1.055 * pow(abs(x), 1.0 / 2.4) - 0.055;\n"
"     \n"
"    // This is cheaper but nearly equivalent\n"
"    return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(abs(x - 0.00228)) - 0.13448 * x + 0.005719;\n"
"}\n"
"\n"
"vec4 PackColor(vec4 Linear)\n"
"{\n"
"#if CONVERT_TO_SRGB\n"
"    return vec4(LinearToSRGB(Linear.r), LinearToSRGB(Linear.g), LinearToSRGB(Linear.b), Linear.a);\n"
"#else\n"
"    return Linear;\n"
"#endif\n"
"}\n"
"\n"
"void GroupMemoryBarrierWithGroupSync()\n"
"{\n"
"    // OpenGL.org: groupMemoryBarrier() waits on the completion of all memory accesses \n"
"    // performed by an invocation of a compute shader relative to the same access performed \n"
"    // by other invocations in the same work group and then returns with no other effect.\n"
"\n"
"    // groupMemoryBarrier() acts like memoryBarrier(), ordering memory writes for all kinds \n"
"    // of variables, but it only orders read/writes for the current work group.\n"
"    groupMemoryBarrier();\n"
"\n"
"    // OpenGL.org: memoryBarrierShared() waits on the completion of \n"
"    // all memory accesses resulting from the use of SHARED variables\n"
"    // and then returns with no other effect. \n"
"    memoryBarrierShared();\n"
"\n"
"    // Thread execution barrier\n"
"    barrier();\n"
"}\n"
"\n"
"layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
"void main()\n"
"{\n"
"    uint LocalInd = gl_LocalInvocationIndex;\n"
"    uvec3 GlobalInd = gl_GlobalInvocationID;\n"
"    \n"
"    ivec3 SrcMipSize = textureSize(SrcMip, 0); // SrcMip is the view of the source mip level\n"
"    bool IsValidThread = GlobalInd.x < uint(SrcMipSize.x) && GlobalInd.y < uint(SrcMipSize.y);\n"
"    int ArraySlice = FirstArraySlice + int(GlobalInd.z);\n"
"\n"
"    vec4 Src1 = vec4(0.0, 0.0, 0.0, 0.0);\n"
"    float fSrcMipLevel = 0.0; // SrcMip is the view of the source mip level\n"
"    if (IsValidThread)\n"
"    {\n"
"        // One bilinear sample is insufficient when scaling down by more than 2x.\n"
"        // You will slightly undersample in the case where the source dimension\n"
"        // is odd.  This is why it\'s a really good idea to only generate mips on\n"
"        // power-of-two sized textures.  Trying to handle the undersampling case\n"
"        // will force this shader to be slower and more complicated as it will\n"
"        // have to take more source texture samples.\n"
"#if NON_POWER_OF_TWO == 0\n"
"        vec2 UV = TexelSize * (vec2(GlobalInd.xy) + vec2(0.5, 0.5));\n"
"        Src1 = textureLod(SrcMip, vec3(UV, ArraySlice), fSrcMipLevel);\n"
"#elif NON_POWER_OF_TWO == 1\n"
"        // > 2:1 in X dimension\n"
"        // Use 2 bilinear samples to guarantee we don\'t undersample when downsizing by more than 2x\n"
"        // horizontally.\n"
"        vec2 UV1 = TexelSize * (vec2(GlobalInd.xy) + vec2(0.25, 0.5));\n"
"        vec2 Off = TexelSize * vec2(0.5, 0.0);\n"
"        Src1 = 0.5 * (textureLod(SrcMip, vec3(UV1,       ArraySlice), fSrcMipLevel) +\n"
"                      textureLod(SrcMip, vec3(UV1 + Off, ArraySlice), fSrcMipLevel));\n"
"#elif NON_POWER_OF_TWO == 2\n"
"        // > 2:1 in Y dimension\n"
"        // Use 2 bilinear samples to guarantee we don\'t undersample when downsizing by more than 2x\n"
"        // vertically.\n"
"        vec2 UV1 = TexelSize * (vec2(GlobalInd.xy) + vec2(0.5, 0.25));\n"
"        vec2 Off = TexelSize * vec2(0.0, 0.5);\n"
"        Src1 = 0.5 * (textureLod(SrcMip, vec3(UV1,       ArraySlice), fSrcMipLevel) +\n"
"                      textureLod(SrcMip, vec3(UV1 + Off, ArraySlice), fSrcMipLevel));\n"
"#elif NON_POWER_OF_TWO == 3\n"
"        // > 2:1 in in both dimensions\n"
"        // Use 4 bilinear samples to guarantee we don\'t undersample when downsizing by more than 2x\n"
"        // in both directions.\n"
"        vec2 UV1 = TexelSize * (vec2(GlobalInd.xy) + vec2(0.25, 0.25));\n"
"        vec2 Off = TexelSize * 0.5;\n"
"        Src1 += textureLod(SrcMip, vec3(UV1,                      ArraySlice), fSrcMipLevel);\n"
"        Src1 += textureLod(SrcMip, vec3(UV1 + vec2(Off.x, 0.0),   ArraySlice), fSrcMipLevel);\n"
"        Src1 += textureLod(SrcMip, vec3(UV1 + vec2(0.0,   Off.y), ArraySlice), fSrcMipLevel);\n"
"        Src1 += textureLod(SrcMip, vec3(UV1 + vec2(Off.x, Off.y), ArraySlice), fSrcMipLevel);\n"
"        Src1 *= 0.25;\n"
"#endif\n"
"\n"
"        imageStore(OutMip[0], ivec3(GlobalInd.xy, ArraySlice), PackColor(Src1));\n"
"    }\n"
"\n"
"    // A scalar (constant) branch can exit all threads coherently.\n"
"    if (NumMipLevels == 1)\n"
"        return;\n"
"\n"
"    if (IsValidThread)\n"
"    {\n"
"        // Without lane swizzle operations, the only way to share data with other\n"
"        // threads is through LDS.\n"
"        StoreColor(LocalInd, Src1);\n"
"    }\n"
"\n"
"    // This guarantees all LDS writes are complete and that all threads have\n"
"    // executed all instructions so far (and therefore have issued their LDS\n"
"    // write instructions.)\n"
"	GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    if (IsValidThread)\n"
"    {\n"
"        // With low three bits for X and high three bits for Y, this bit mask\n"
"        // (binary: 001001) checks that X and Y are even.\n"
"        if ((LocalInd & 0x9u) == 0u)\n"
"        {\n"
"            vec4 Src2 = LoadColor(LocalInd + 0x01u);\n"
"            vec4 Src3 = LoadColor(LocalInd + 0x08u);\n"
"            vec4 Src4 = LoadColor(LocalInd + 0x09u);\n"
"            Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);\n"
"\n"
"            imageStore(OutMip[1], ivec3(GlobalInd.xy / 2u, ArraySlice), PackColor(Src1));\n"
"            StoreColor(LocalInd, Src1);\n"
"        }\n"
"    }\n"
"\n"
"    if (NumMipLevels == 2)\n"
"        return;\n"
"\n"
"	GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    if( IsValidThread )\n"
"    {\n"
"        // This bit mask (binary: 011011) checks that X and Y are multiples of four.\n"
"        if ((LocalInd & 0x1Bu) == 0u)\n"
"        {\n"
"            vec4 Src2 = LoadColor(LocalInd + 0x02u);\n"
"            vec4 Src3 = LoadColor(LocalInd + 0x10u);\n"
"            vec4 Src4 = LoadColor(LocalInd + 0x12u);\n"
"            Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);\n"
"\n"
"            imageStore(OutMip[2], ivec3(GlobalInd.xy / 4u, ArraySlice), PackColor(Src1));\n"
"            StoreColor(LocalInd, Src1);\n"
"        }\n"
"    }\n"
"\n"
"    if (NumMipLevels == 3)\n"
"        return;\n"
"\n"
"	GroupMemoryBarrierWithGroupSync();\n"
"\n"
"    if( IsValidThread )\n"
"    {\n"
"        // This bit mask would be 111111 (X & Y multiples of 8), but only one\n"
"        // thread fits that criteria.\n"
"        if (LocalInd == 0u)\n"
"        {\n"
"            vec4 Src2 = LoadColor(LocalInd + 0x04u);\n"
"            vec4 Src3 = LoadColor(LocalInd + 0x20u);\n"
"            vec4 Src4 = LoadColor(LocalInd + 0x24u);\n"
"            Src1 = 0.25 * (Src1 + Src2 + Src3 + Src4);\n"
"\n"
"            imageStore(OutMip[3], ivec3(GlobalInd.xy / 8u, ArraySlice), PackColor(Src1));\n"
"        }\n"
"    }\n"
"}\n"
