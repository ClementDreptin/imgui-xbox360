float4x4 matWVP : register(c0);

struct VS_IN
{
    float4 ObjPos : POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

struct VS_OUT
{
    float4 ProjPos : POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

VS_OUT main(VS_IN In)
{
    VS_OUT Out;
    Out.ProjPos = mul(matWVP, In.ObjPos);
    Out.Color = In.Color;
    Out.UV = In.UV;
    return Out;
}
