// One triangle covering the target, the vertex half of every pass that
// works on a whole picture (D475): its corner, and the picture's
// coordinates there, rows top first. A module the passes import, not a
// container of its own.

module screen;

public struct Screen
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float2 uv;
}

// What a pass reads of none of its vertex half's outputs has its corner
// alone, as D3D12 links a fragment entry only to what it reads (D418).
public struct Bare
{
    public float4 position : SV_Position;
}

float2 cornerOf(uint vertex)
{
    return float2(float((vertex << 1) & 2), float(vertex & 2));
}

public Screen screenOf(uint vertex)
{
    const float2 at = cornerOf(vertex);
    Screen screen;
    screen.position = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
    screen.uv = at;
    return screen;
}

public Bare bareOf(uint vertex)
{
    const float2 at = cornerOf(vertex);
    Bare bare;
    bare.position = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
    return bare;
}
