cbuffer _UniformBuffer : register(b0, space0)
{
    float4 _10_colorGreen : packoffset(c0);
    float4 _10_colorRed : packoffset(c1);
};


static float4 sk_FragColor;

struct SPIRV_Cross_Output
{
    float4 sk_FragColor : SV_Target0;
};

float4 main(float2 _24)
{
    bool ok = true;
    int i = 5;
    int _34 = 5 + 1;
    i = _34;
    bool _41 = false;
    if (true)
    {
        i = _34 + 1;
        _41 = _34 == 6;
    }
    else
    {
        _41 = false;
    }
    ok = _41;
    bool _47 = false;
    if (_41)
    {
        _47 = i == 7;
    }
    else
    {
        _47 = false;
    }
    ok = _47;
    bool _53 = false;
    if (_47)
    {
        int _50 = i;
        i = _50 - 1;
        _53 = _50 == 7;
    }
    else
    {
        _53 = false;
    }
    ok = _53;
    bool _58 = false;
    if (_53)
    {
        _58 = i == 6;
    }
    else
    {
        _58 = false;
    }
    ok = _58;
    int _59 = i;
    int _60 = _59 - 1;
    i = _60;
    bool _64 = false;
    if (_58)
    {
        _64 = _60 == 5;
    }
    else
    {
        _64 = false;
    }
    ok = _64;
    float f = 0.5f;
    float _69 = 0.5f + 1.0f;
    f = _69;
    bool _75 = false;
    if (_64)
    {
        f = _69 + 1.0f;
        _75 = _69 == 1.5f;
    }
    else
    {
        _75 = false;
    }
    ok = _75;
    bool _81 = false;
    if (_75)
    {
        _81 = f == 2.5f;
    }
    else
    {
        _81 = false;
    }
    ok = _81;
    bool _87 = false;
    if (_81)
    {
        float _84 = f;
        f = _84 - 1.0f;
        _87 = _84 == 2.5f;
    }
    else
    {
        _87 = false;
    }
    ok = _87;
    bool _92 = false;
    if (_87)
    {
        _92 = f == 1.5f;
    }
    else
    {
        _92 = false;
    }
    ok = _92;
    float _93 = f;
    float _94 = _93 - 1.0f;
    f = _94;
    bool _98 = false;
    if (_92)
    {
        _98 = _94 == 0.5f;
    }
    else
    {
        _98 = false;
    }
    ok = _98;
    float4 _99 = 0.0f.xxxx;
    if (_98)
    {
        _99 = _10_colorGreen;
    }
    else
    {
        _99 = _10_colorRed;
    }
    return _99;
}

void frag_main()
{
    float2 _20 = 0.0f.xx;
    sk_FragColor = main(_20);
}

SPIRV_Cross_Output main()
{
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.sk_FragColor = sk_FragColor;
    return stage_output;
}