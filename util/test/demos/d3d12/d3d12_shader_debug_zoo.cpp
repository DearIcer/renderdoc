/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d12_test.h"

RD_TEST(D3D12_Shader_Debug_Zoo, D3D12GraphicsTest)
{
  static constexpr const char *Description = "Tests shader debugging in different edge cases";

  struct ConstsA2V
  {
    Vec3f pos;
    float zero;
    float one;
    float negone;
  };

  std::string pixelBlit = R"EOSHADER(

cbuffer rootconsts : register(b0)
{
  float offset;
}

Texture2D<float4> intex : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target0
{
	return intex.Load(float3(pos.x, pos.y - offset, 0));
}

)EOSHADER";

  std::string common = R"EOSHADER(

struct consts
{
  float3 pos : POSITION;
  float zeroVal : ZERO;
  float oneVal : ONE;
  float negoneVal : NEGONE;
};

struct v2f
{
  float4 pos : SV_POSITION;
  float2 zeroVal : ZERO;
  float tinyVal : TINY;
  float oneVal : ONE;
  float negoneVal : NEGONE;
  uint tri : TRIANGLE;
  uint intval : INTVAL;
};

)EOSHADER";

  std::string vertex = R"EOSHADER(

v2f main(consts IN, uint tri : SV_InstanceID)
{
  v2f OUT = (v2f)0;

  OUT.pos = float4(IN.pos.x + IN.pos.z * float(tri), IN.pos.y, 0.0f, 1);

  OUT.zeroVal = IN.zeroVal.xx;
  OUT.oneVal = IN.oneVal;
  OUT.negoneVal = IN.negoneVal;
  OUT.tri = tri;
  OUT.tinyVal = IN.oneVal * 1.0e-30f;
  OUT.intval = tri + 7;

  return OUT;
}

)EOSHADER";

  std::string pixel = R"EOSHADER(

struct InnerStruct
{
  float a;
  float b[2];
  float c;
};

struct MyStruct
{
  float a;
  float4 b;
  float c;
  InnerStruct d;
  float e;
};

Buffer<float> test : register(t0);
ByteAddressBuffer byterotest : register(t1);
StructuredBuffer<MyStruct> structrotest : register(t2);
Texture2D<float> dimtex : register(t3);
RWByteAddressBuffer byterwtest : register(u1);
RWStructuredBuffer<MyStruct> structrwtest : register(u2);

float4 main(v2f IN) : SV_Target0
{
  float  posinf = IN.oneVal/IN.zeroVal.x;
  float  neginf = IN.negoneVal/IN.zeroVal.x;
  float  nan = IN.zeroVal.x/IN.zeroVal.y;

  float negone = IN.negoneVal;
  float posone = IN.oneVal;
  float zero = IN.zeroVal.x;
  float tiny = IN.tinyVal;

  int intval = IN.intval;

  if(IN.tri == 0)
    return float4(log(negone), log(zero), log(posone), 1.0f);
  if(IN.tri == 1)
    return float4(log(posinf), log(neginf), log(nan), 1.0f);
  if(IN.tri == 2)
    return float4(exp(negone), exp(zero), exp(posone), 1.0f);
  if(IN.tri == 3)
    return float4(exp(posinf), exp(neginf), exp(nan), 1.0f);
  if(IN.tri == 4)
    return float4(sqrt(negone), sqrt(zero), sqrt(posone), 1.0f);
  if(IN.tri == 5)
    return float4(sqrt(posinf), sqrt(neginf), sqrt(nan), 1.0f);
  if(IN.tri == 6)
    return float4(rsqrt(negone), rsqrt(zero), rsqrt(posone), 1.0f);
  if(IN.tri == 7)
    return float4(saturate(posinf), saturate(neginf), saturate(nan), 1.0f);
  if(IN.tri == 8)
    return float4(min(posinf, nan), min(neginf, nan), min(nan, nan), 1.0f);
  if(IN.tri == 9)
    return float4(min(posinf, posinf), min(neginf, posinf), min(nan, posinf), 1.0f);
  if(IN.tri == 10)
    return float4(min(posinf, neginf), min(neginf, neginf), min(nan, neginf), 1.0f);
  if(IN.tri == 11)
    return float4(max(posinf, nan), max(neginf, nan), max(nan, nan), 1.0f);
  if(IN.tri == 12)
    return float4(max(posinf, posinf), max(neginf, posinf), max(nan, posinf), 1.0f);
  if(IN.tri == 13)
    return float4(max(posinf, neginf), max(neginf, neginf), max(nan, neginf), 1.0f);

  // rounding tests
  float round_a = 1.7f*posone;
  float round_b = 2.1f*posone;
  float round_c = 1.5f*posone;
  float round_d = 2.5f*posone;
  float round_e = zero;
  float round_f = -1.7f*posone;
  float round_g = -2.1f*posone;
  float round_h = -1.5f*posone;
  float round_i = -2.5f*posone;

  if(IN.tri == 14)
    return float4(round(round_a), floor(round_a), ceil(round_a), trunc(round_a));
  if(IN.tri == 15)
    return float4(round(round_b), floor(round_b), ceil(round_b), trunc(round_b));
  if(IN.tri == 16)
    return float4(round(round_c), floor(round_c), ceil(round_c), trunc(round_c));
  if(IN.tri == 17)
    return float4(round(round_d), floor(round_d), ceil(round_d), trunc(round_d));
  if(IN.tri == 18)
    return float4(round(round_e), floor(round_e), ceil(round_e), trunc(round_e));
  if(IN.tri == 19)
    return float4(round(round_f), floor(round_f), ceil(round_f), trunc(round_f));
  if(IN.tri == 20)
    return float4(round(round_g), floor(round_g), ceil(round_g), trunc(round_g));
  if(IN.tri == 21)
    return float4(round(round_h), floor(round_h), ceil(round_h), trunc(round_h));
  if(IN.tri == 22)
    return float4(round(round_i), floor(round_i), ceil(round_i), trunc(round_i));

  if(IN.tri == 23)
    return float4(round(neginf), floor(neginf), ceil(neginf), trunc(neginf));
  if(IN.tri == 24)
    return float4(round(posinf), floor(posinf), ceil(posinf), trunc(posinf));
  if(IN.tri == 25)
    return float4(round(nan), floor(nan), ceil(nan), trunc(nan));

  if(IN.tri == 26)
    return test[5].xxxx;

  if(IN.tri == 27)
  {
    uint unsignedVal = uint(344.1f*posone);
    int signedVal = int(344.1f*posone);
    return float4(firstbithigh(unsignedVal), firstbitlow(unsignedVal),
                  firstbithigh(signedVal), firstbitlow(signedVal));
  }

  if(IN.tri == 28)
  {
    int signedVal = int(344.1f*negone);
    return float4(firstbithigh(signedVal), firstbitlow(signedVal), 0.0f, 0.0f);
  }

  // saturate NaN returns 0
  if(IN.tri == 29)
    return float4(0.1f+saturate(nan * 2.0f), 0.1f+saturate(nan * 3.0f), 0.1f+saturate(nan * 4.0f), 1.0f);

  // min() and max() with NaN return the other component if it's non-NaN, or else nan if it is nan
  if(IN.tri == 30)
    return float4(min(nan, 0.3f), max(nan, 0.3f), max(nan, nan), 1.0f);

  // the above applies componentwise
  if(IN.tri == 31)
    return max( float4(0.1f, 0.2f, 0.3f, 0.4f), nan.xxxx );
  if(IN.tri == 32)
    return min( float4(0.1f, 0.2f, 0.3f, 0.4f), nan.xxxx );

  // negating nan and abs(nan) gives nan
  if(IN.tri == 33)
    return float4(-nan, abs(nan), 0.0f, 1.0f);

  // check denorm flushing
  if(IN.tri == 34)
    return float4(tiny * 1.5e-8f, tiny * 1.5e-9f, asfloat(intval) == 0.0f ? 1.0f : 0.0f, 1.0f);

  // test reading/writing byte address data

  // mis-aligned loads
  if(IN.tri == 35)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    return float4(asfloat(byterotest.Load(z+0).x), asfloat(byterotest.Load(z+1).x),
                  asfloat(byterotest.Load(z+3).x), float(byterotest.Load(z+8).x));
  }
  // later loads: valid, out of view bounds but in buffer bounds, out of both bounds
  if(IN.tri == 36)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    return float4(asfloat(byterotest.Load(z+40).x), asfloat(byterotest.Load(z+44).x),
                  asfloat(byterotest.Load(z+48).x), float(byterotest.Load(z+4096).x));
  }
  // 4-uint load
  if(IN.tri == 37)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    // test a 4-uint load
    return asfloat(byterotest.Load4(z+24));
  }
  // 4-uint load crossing view bounds
  if(IN.tri == 38)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    // test a 4-uint load
    return asfloat(byterotest.Load4(z+40));
  }
  // 4-uint load out of view bounds
  if(IN.tri == 39)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    // test a 4-uint load
    return asfloat(byterotest.Load4(z+48));
  }

  // mis-aligned store
  if(IN.tri == 40)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store(z+0, asuint(5.4321f));
    byterwtest.Store(z+1, asuint(9.8765f));

    return asfloat(byterwtest.Load(z2+0).x);
  }
  // mis-aligned loads
  if(IN.tri == 41)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store(z+0, asuint(5.4321f));
    byterwtest.Store(z+4, asuint(9.8765f));
    byterwtest.Store(z+8, 0xbeef);

    return float4(asfloat(byterwtest.Load(z2+0).x), asfloat(byterwtest.Load(z2+1).x),
                  asfloat(byterwtest.Load(z2+3).x), float(byterwtest.Load(z2+8).x));
  }
  // later stores: valid, out of view bounds but in buffer bounds, out of both bounds
  if(IN.tri == 42)
  {
    // use this to ensure the compiler doesn't know we're loading from the same locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store(z+40, asuint(1.2345f));
    byterwtest.Store(z+44, asuint(9.8765f));
    byterwtest.Store(z+48, asuint(1.81818f));
    byterwtest.Store(z+4096, asuint(5.55555f));

    return float4(asfloat(byterwtest.Load(z2+40).x), asfloat(byterwtest.Load(z2+44).x),
                  asfloat(byterwtest.Load(z2+48).x), float(byterwtest.Load(z2+4096).x));
  }
  // 4-uint store
  if(IN.tri == 43)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store4(z+24, uint4(99, 88, 77, 66));

    return asfloat(byterotest.Load4(z2+24));
  }
  // 4-uint store crossing view bounds
  if(IN.tri == 44)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store4(z+40, uint4(99, 88, 77, 66));

    return asfloat(byterotest.Load4(z2+40));
  }
  // 4-uint store out of view bounds
  if(IN.tri == 45)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    byterwtest.Store4(z+48, uint4(99, 88, 77, 66));

    return asfloat(byterotest.Load4(z2+48));
  }

  // test reading/writing structured data

  // reading struct at 0 (need two tests to verify most of the data,
  // we assume the rest is OK because of alignment)
  if(IN.tri == 46)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+0];

    return float4(read.b.xyz, read.c);
  }
  if(IN.tri == 47)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+0];

    return float4(read.a, read.e, read.d.b[z+0], read.d.c);
  }
  // reading later, but in bounds
  if(IN.tri == 48)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+3];

    return float4(read.b.xyz, read.c);
  }
  if(IN.tri == 49)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+3];

    return float4(read.a, read.e, read.d.b[z+0], read.d.c);
  }
  // structured buffers do not allow partially out of bounds behaviour:
  // - buffers must by multiples of structure stride (so buffer partials aren't allowed)
  // - views work in units of structure stride (so view partials aren't allowed)
  // we can only test fully out of bounds of the view, but in bounds of the buffer
  if(IN.tri == 50)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+7];

    return float4(read.b.xyz, read.c);
  }
  if(IN.tri == 51)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;

    MyStruct read = structrotest[z+7];

    return float4(read.a, read.e, read.d.b[z+0], read.d.c);
  }

  // storing in bounds
  if(IN.tri == 52)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    MyStruct write = (MyStruct)0;

    write.a = zero+1.0f;
    write.c = zero+2.0f;
    write.e = zero+3.0f;
    write.b = float4(zero+4.0f, zero+5.0f, zero+6.0f, zero+7.0f);
    write.d.a = zero+8.0f;
    write.d.b[0] = zero+9.0f;
    write.d.b[1] = zero+10.0f;
    write.d.c = zero+11.0f;

    structrwtest[z+2] = write;

    MyStruct read = structrwtest[z2+2];

    return float4(read.b.xyz, read.c);
  }
  if(IN.tri == 53)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    MyStruct write = (MyStruct)0;

    write.a = zero+1.0f;
    write.c = zero+2.0f;
    write.e = zero+3.0f;
    write.b = float4(zero+4.0f, zero+5.0f, zero+6.0f, zero+7.0f);
    write.d.a = zero+8.0f;
    write.d.b[0] = zero+9.0f;
    write.d.b[1] = zero+10.0f;
    write.d.c = zero+11.0f;

    structrwtest[z+2] = write;

    MyStruct read = structrwtest[z2+2];

    return float4(read.a, read.e, read.d.b[z2+0], read.d.c);
  }

  // storing out of bounds
  if(IN.tri == 54)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    MyStruct write = (MyStruct)0;

    write.a = zero+1.0f;
    write.c = zero+2.0f;
    write.e = zero+3.0f;
    write.b = float4(zero+4.0f, zero+5.0f, zero+6.0f, zero+7.0f);
    write.d.a = zero+8.0f;
    write.d.b[0] = zero+9.0f;
    write.d.b[1] = zero+10.0f;
    write.d.c = zero+11.0f;

    structrwtest[z+7] = write;

    MyStruct read = structrwtest[z2+7];

    return float4(read.b.xyz, read.c);
  }
  if(IN.tri == 55)
  {
    // use this to ensure the compiler doesn't know we're using fixed locations
    uint z = intval - IN.tri - 7;
    uint z2 = uint(zero);

    MyStruct write = (MyStruct)0;

    write.a = zero+1.0f;
    write.c = zero+2.0f;
    write.e = zero+3.0f;
    write.b = float4(zero+4.0f, zero+5.0f, zero+6.0f, zero+7.0f);
    write.d.a = zero+8.0f;
    write.d.b[0] = zero+9.0f;
    write.d.b[1] = zero+10.0f;
    write.d.c = zero+11.0f;

    structrwtest[z+7] = write;

    MyStruct read = structrwtest[z2+7];

    return float4(read.a, read.e, read.d.b[z2+0], read.d.c);
  }
  if(IN.tri == 56)
  {
    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(0, width, height, numLevels);
    return float4(width, height, numLevels, 0.0f);
  }
  if(IN.tri == 57)
  {
    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(2, width, height, numLevels);
    return float4(width, height, numLevels, 0.0f);
  }
  if(IN.tri == 58)
  {
    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(10, width, height, numLevels);
    return float4(max(1,width), max(1,height), numLevels, 0.0f);
  }

  if(IN.tri == 59)
  {
    // use this to ensure the compiler doesn't know we're using fixed mips
    uint z = intval - IN.tri - 7;

    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(z, width, height, numLevels);
    return float4(width, height, numLevels, 0.0f);
  }
  if(IN.tri == 60)
  {
    // use this to ensure the compiler doesn't know we're using fixed mips
    uint z = intval - IN.tri - 7;

    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(z+2, width, height, numLevels);
    return float4(width, height, numLevels, 0.0f);
  }
  if(IN.tri == 61)
  {
    // use this to ensure the compiler doesn't know we're using fixed mips
    uint z = intval - IN.tri - 7;

    uint width = 0, height = 0, numLevels = 0;
    dimtex.GetDimensions(z+10, width, height, numLevels);
    return float4(max(1,width), max(1,height), numLevels, 0.0f);
  }

  return float4(0.4f, 0.4f, 0.4f, 0.4f);
}

)EOSHADER";

  int main()
  {
    // initialise, create window, create device, etc
    if(!Init())
      return 3;

    size_t lastTest = pixel.rfind("IN.tri == ");
    lastTest += sizeof("IN.tri == ") - 1;

    const uint32_t numTests = atoi(pixel.c_str() + lastTest) + 1;

    ID3DBlobPtr vsblob = Compile(common + vertex, "main", "vs_5_0");
    ID3DBlobPtr psblob = Compile(common + pixel, "main", "ps_5_0");

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    inputLayout.reserve(4);
    inputLayout.push_back({
        "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0,
    });
    inputLayout.push_back({
        "ZERO", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0,
    });
    inputLayout.push_back({
        "ONE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0,
    });
    inputLayout.push_back({
        "NEGONE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0,
    });

    ID3D12RootSignaturePtr sig = MakeSig({
        tableParam(D3D12_SHADER_VISIBILITY_PIXEL, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 0, 4, 0),
        tableParam(D3D12_SHADER_VISIBILITY_PIXEL, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1, 2, 4),
    });

    ID3D12PipelineStatePtr pso_5_0 = MakePSO()
                                         .RootSig(sig)
                                         .InputLayout(inputLayout)
                                         .VS(vsblob)
                                         .PS(psblob)
                                         .RTVs({DXGI_FORMAT_R32G32B32A32_FLOAT});

    // Recompile the same PS with SM 5.1 to test shader debugging with the different bytecode
    psblob = Compile(common + pixel, "main", "ps_5_1");
    ID3D12PipelineStatePtr pso_5_1 = MakePSO()
                                         .RootSig(sig)
                                         .InputLayout(inputLayout)
                                         .VS(vsblob)
                                         .PS(psblob)
                                         .RTVs({DXGI_FORMAT_R32G32B32A32_FLOAT});

    static const uint32_t texDim = AlignUp(numTests, 64U) * 4;

    ID3D12ResourcePtr fltTex = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, texDim, 4)
                                   .RTV()
                                   .InitialState(D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE fltRTV = MakeRTV(fltTex).CreateCPU(0);
    D3D12_GPU_DESCRIPTOR_HANDLE fltSRV = MakeSRV(fltTex).CreateGPU(6);

    float triWidth = 8.0f / float(texDim);

    ConstsA2V triangle[] = {
        {Vec3f(-1.0f, -1.0f, triWidth), 0.0f, 1.0f, -1.0f},
        {Vec3f(-1.0f, 1.0f, triWidth), 0.0f, 1.0f, -1.0f},
        {Vec3f(-1.0f + triWidth, 1.0f, triWidth), 0.0f, 1.0f, -1.0f},
    };

    ID3D12ResourcePtr vb = MakeBuffer().Data(triangle);
    ResourceBarrier(vb, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    union
    {
      float f;
      uint32_t u;
    } pun;

    pun.u = 0xdead;

    float testdata[] = {
        1.0f,  2.0f,  3.0f,  4.0f,  1.234567f, pun.f, 7.0f,  8.0f,  9.0f,  10.0f,
        11.0f, 12.0f, 13.0f, 14.0f, 15.0f,     16.0f, 17.0f, 18.0f, 19.0f, 20.0f,
    };

    ID3D12ResourcePtr srvBuf = MakeBuffer().Data(testdata);
    MakeSRV(srvBuf).Format(DXGI_FORMAT_R32_FLOAT).CreateGPU(0);

    ID3D12ResourcePtr testTex = MakeTexture(DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 16).Mips(3);
    MakeSRV(testTex).CreateGPU(3);

    ID3D12ResourcePtr rawBuf = MakeBuffer().Data(testdata);
    MakeSRV(rawBuf)
        .Format(DXGI_FORMAT_R32_TYPELESS)
        .ByteAddressed()
        .FirstElement(4)
        .NumElements(12)
        .CreateGPU(1);

    ID3D12ResourcePtr rawBuf2 = MakeBuffer().Size(1024).UAV();
    D3D12ViewCreator uavView1 =
        MakeUAV(rawBuf2).Format(DXGI_FORMAT_R32_TYPELESS).ByteAddressed().FirstElement(4).NumElements(12);
    D3D12_CPU_DESCRIPTOR_HANDLE uav1cpu = uavView1.CreateClearCPU(4);
    D3D12_GPU_DESCRIPTOR_HANDLE uav1gpu = uavView1.CreateGPU(4);

    float structdata[220];
    for(int i = 0; i < 220; i++)
      structdata[i] = float(i);

    ID3D12ResourcePtr structBuf = MakeBuffer().Data(structdata);
    MakeSRV(structBuf)
        .Format(DXGI_FORMAT_UNKNOWN)
        .FirstElement(3)
        .NumElements(5)
        .StructureStride(11 * sizeof(float))
        .CreateGPU(2);

    ID3D12ResourcePtr structBuf2 = MakeBuffer().Size(880).UAV();
    D3D12ViewCreator uavView2 = MakeUAV(structBuf2)
                                    .Format(DXGI_FORMAT_UNKNOWN)
                                    .FirstElement(3)
                                    .NumElements(5)
                                    .StructureStride(11 * sizeof(float));
    D3D12_CPU_DESCRIPTOR_HANDLE uav2cpu = uavView2.CreateClearCPU(5);
    D3D12_GPU_DESCRIPTOR_HANDLE uav2gpu = uavView2.CreateGPU(5);

    vsblob = Compile(D3DFullscreenQuadVertex, "main", "vs_4_0");
    psblob = Compile(pixelBlit, "main", "ps_5_0");
    ID3D12RootSignaturePtr blitSig = MakeSig({
        constParam(D3D12_SHADER_VISIBILITY_PIXEL, 0, 0, 1),
        tableParam(D3D12_SHADER_VISIBILITY_PIXEL, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 0, 1, 6),
    });
    ID3D12PipelineStatePtr blitpso = MakePSO().RootSig(blitSig).VS(vsblob).PS(psblob);

    while(Running())
    {
      ID3D12GraphicsCommandListPtr cmd = GetCommandBuffer();
      Reset(cmd);

      ID3D12ResourcePtr bb = StartUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      D3D12_CPU_DESCRIPTOR_HANDLE rtv =
          MakeRTV(bb).Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).CreateCPU(1);
      ClearRenderTargetView(cmd, rtv, {0.4f, 0.5f, 0.6f, 1.0f});

      ID3D12PipelineStatePtr psos[2] = {pso_5_0, pso_5_1};
      float blitOffsets[2] = {0.0f, 4.0f};
      D3D12_RECT scissors[2] = {{0, 0, (int)texDim, 4}, {0, 4, (int)texDim, 8}};
      const char *markers[2] = {"sm_5_0", "sm_5_1"};

      // Clear, draw, and blit to backbuffer twice - once for SM 5.0 and again for SM 5.1
      for(int i = 0; i < 2; ++i)
      {
        ClearRenderTargetView(cmd, fltRTV, {0.4f, 0.5f, 0.6f, 1.0f});

        IASetVertexBuffer(cmd, vb, sizeof(ConstsA2V), 0);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        OMSetRenderTargets(cmd, {fltRTV}, {});

        cmd->SetGraphicsRootSignature(sig);
        cmd->SetDescriptorHeaps(1, &m_CBVUAVSRV.GetInterfacePtr());
        cmd->SetGraphicsRootDescriptorTable(0, m_CBVUAVSRV->GetGPUDescriptorHandleForHeapStart());
        cmd->SetGraphicsRootDescriptorTable(1, m_CBVUAVSRV->GetGPUDescriptorHandleForHeapStart());

        cmd->SetPipelineState(psos[i]);

        RSSetViewport(cmd, {0.0f, 0.0f, (float)texDim, 4.0f, 0.0f, 1.0f});
        RSSetScissorRect(cmd, {0, 0, (int)texDim, 4});

        UINT zero[4] = {};
        cmd->ClearUnorderedAccessViewUint(uav1gpu, uav1cpu, rawBuf2, zero, 0, NULL);
        cmd->ClearUnorderedAccessViewUint(uav2gpu, uav2cpu, structBuf2, zero, 0, NULL);

        // Add a marker so we can easily locate this draw
        cmd->SetMarker(1, markers[i], (UINT)strlen(markers[i]));
        cmd->DrawInstanced(3, numTests, 0, 0);

        ResourceBarrier(cmd, fltTex, D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        OMSetRenderTargets(cmd, {rtv}, {});
        RSSetViewport(cmd, {0.0f, 0.0f, (float)screenWidth, (float)screenHeight, 0.0f, 1.0f});
        RSSetScissorRect(cmd, scissors[i]);

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmd->SetGraphicsRootSignature(blitSig);
        cmd->SetPipelineState(blitpso);
        cmd->SetGraphicsRoot32BitConstant(0, *(UINT *)&blitOffsets[i], 0);
        cmd->SetGraphicsRootDescriptorTable(1, m_CBVUAVSRV->GetGPUDescriptorHandleForHeapStart());
        cmd->DrawInstanced(4, 1, 0, 0);

        ResourceBarrier(cmd, fltTex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
      }

      FinishUsingBackbuffer(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

      cmd->Close();
      Submit({cmd});
      Present();
    }

    return 0;
  }
};

REGISTER_TEST();
