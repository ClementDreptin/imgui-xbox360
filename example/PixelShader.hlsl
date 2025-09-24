struct PS_IN
{
    float4 ProjPos : POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

sampler TextureSampler : register(s0);
Texture2D Texture : register(t0);

float4 main(PS_IN In) : COLOR
{
    float4 TextureColor = Texture.Sample(TextureSampler, In.UV);
    return In.Color * TextureColor;
}
