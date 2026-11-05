struct Slate_TyreStepPush_std140_0
{
    @align(16) ParticleCount_0 : u32,
    @align(4) Rings_0 : u32,
    @align(8) Segments_0 : u32,
    @align(4) TyreCount_0 : u32,
    @align(16) SubstepSeconds_0 : f32,
    @align(4) SubstepIndex_0 : u32,
    @align(8) SubstepCount_0 : u32,
    @align(4) IterationIndex_0 : u32,
    @align(16) PassIndex_0 : u32,
    @align(4) CordOffset_0 : u32,
    @align(8) CordCount_0 : u32,
};

@binding(14) @group(0) var<uniform> Step_0 : Slate_TyreStepPush_std140_0;
@binding(11) @group(0) var<storage, read_write> ShapeMultipliers_0 : array<f32>;

struct Slate_TyreShapeRecord_std430_0
{
    @align(4) RestValue_0 : f32,
    @align(4) Compliance_0 : f32,
    @align(4) Reserved0_0 : f32,
    @align(4) Reserved1_0 : f32,
};

@binding(9) @group(0) var<storage, read> ShapeRows_0 : array<Slate_TyreShapeRecord_std430_0>;

@binding(10) @group(0) var<storage, read_write> ShapeSums_0 : array<atomic<i32>>;

@binding(13) @group(0) var<storage, read_write> ShapeMatrix_0 : array<f32>;

@binding(12) @group(0) var<storage, read_write> ShapeAdvances_0 : array<f32>;

fn Slate_ShapeOrder_0() -> u32
{
    return u32(1) + Step_0.Rings_0 + Step_0.Segments_0;
}

fn Slate_ShapeEntryScale_0( Row_0 : u32,  Column_0 : u32) -> f32
{
    var _S1 : bool = Row_0 == u32(0);
    var _S2 : bool;
    if(_S1)
    {
        _S2 = Column_0 == u32(0);
    }
    else
    {
        _S2 = false;
    }
    if(_S2)
    {
        return 4.0e+09f;
    }
    if(_S1)
    {
        _S2 = true;
    }
    else
    {
        _S2 = Column_0 == u32(0);
    }
    if(_S2)
    {
        return 2.68435456e+08f;
    }
    return 1.048576e+06f;
}

fn Slate_DecodeFixed_0( Encoded_0 : i32,  Scale_0 : f32) -> f32
{
    return f32(Encoded_0) / Scale_0;
}

fn Slate_ShapeValueScale_0( Row_1 : u32) -> f32
{
    var _S3 : f32;
    if(Row_1 == u32(0))
    {
        _S3 = 5.0e+08f;
    }
    else
    {
        _S3 = 1.0e+06f;
    }
    return _S3;
}

fn SquareRoot_0( Operand_0 : f32) -> f32
{
    return sqrt(Operand_0);
}

@compute
@workgroup_size(128, 1, 1)
fn main(@builtin(workgroup_id) Grouped_0 : vec3<u32>, @builtin(local_invocation_id) Local_0 : vec3<u32>)
{
    var Column_1 : u32;
    var Pivot_0 : f32;
    var Earlier_0 : u32;
    var Earlier_1 : u32;
    var Tyre_0 : u32 = Grouped_0.x;
    if(Tyre_0 >= (Step_0.TyreCount_0))
    {
        return;
    }
    var Order_0 : u32 = Slate_ShapeOrder_0();
    var Lane_0 : u32 = Local_0.x;
    var Sums_0 : u32 = Tyre_0 * u32(9312);
    var _S4 : u32 = Sums_0 + u32(9216);
    var _S5 : u32 = Tyre_0 * u32(9216);
    var _S6 : u32 = Tyre_0 * u32(96);
    var _S7 : f32 = Step_0.SubstepSeconds_0;
    var Row_2 : u32 = Lane_0;
    for(;;)
    {
        if(Row_2 < Order_0)
        {
        }
        else
        {
            break;
        }
        if((Step_0.IterationIndex_0) == u32(0))
        {
            ShapeMultipliers_0[_S6 + Row_2] = 0.0f;
        }
        var _S8 : u32 = _S6 + Row_2;
        var Record_0 : Slate_TyreShapeRecord_std430_0 = ShapeRows_0[_S8];
        var ScaledCompliance_0 : f32 = Record_0.Compliance_0 / (_S7 * _S7);
        Column_1 = u32(0);
        for(;;)
        {
            if(Column_1 <= Row_2)
            {
            }
            else
            {
                break;
            }
            var _S9 : i32 = atomicLoad(&(ShapeSums_0[Sums_0 + Column_1 * u32(96) + Row_2]));
            var Entry_0 : f32 = Slate_DecodeFixed_0(_S9, Slate_ShapeEntryScale_0(Column_1, Row_2));
            if(Row_2 == Column_1)
            {
                Pivot_0 = ScaledCompliance_0;
            }
            else
            {
                Pivot_0 = 0.0f;
            }
            ShapeMatrix_0[_S5 + Row_2 * u32(96) + Column_1] = Entry_0 + Pivot_0;
            Column_1 = Column_1 + u32(1);
        }
        var _S10 : i32 = atomicLoad(&(ShapeSums_0[_S4 + Row_2]));
        ShapeAdvances_0[_S8] = - (Slate_DecodeFixed_0(_S10, Slate_ShapeValueScale_0(Row_2)) - Record_0.RestValue_0) - ScaledCompliance_0 * ShapeMultipliers_0[_S8];
        Row_2 = Row_2 + u32(128);
    }
    storageBarrier(); textureBarrier(); workgroupBarrier();;
    Column_1 = u32(0);
    for(;;)
    {
        if(Column_1 < Order_0)
        {
        }
        else
        {
            break;
        }
        var _S11 : u32 = _S5 + Column_1 * u32(96);
        var _S12 : u32 = _S11 + Column_1;
        Earlier_0 = u32(0);
        Pivot_0 = ShapeMatrix_0[_S12];
        for(;;)
        {
            if(Earlier_0 < Column_1)
            {
            }
            else
            {
                break;
            }
            var _S13 : u32 = _S11 + Earlier_0;
            var Pivot_1 : f32 = Pivot_0 - ShapeMatrix_0[_S13] * ShapeMatrix_0[_S13];
            Earlier_0 = Earlier_0 + u32(1);
            Pivot_0 = Pivot_1;
        }
        var _S14 : f32;
        if(Pivot_0 > 1.00000000317107685e-30f)
        {
            _S14 = Pivot_0;
        }
        else
        {
            _S14 = 1.00000000317107685e-30f;
        }
        var Pivot_2 : f32 = SquareRoot_0(_S14);
        storageBarrier(); textureBarrier(); workgroupBarrier();;
        if(Lane_0 == u32(0))
        {
            ShapeMatrix_0[_S12] = Pivot_2;
        }
        var _S15 : u32 = Column_1 + u32(1);
        Row_2 = _S15 + Lane_0;
        for(;;)
        {
            if(Row_2 < Order_0)
            {
            }
            else
            {
                break;
            }
            var _S16 : u32 = _S5 + Row_2 * u32(96);
            var _S17 : u32 = _S16 + Column_1;
            Earlier_1 = u32(0);
            var Sum_0 : f32 = ShapeMatrix_0[_S17];
            for(;;)
            {
                if(Earlier_1 < Column_1)
                {
                }
                else
                {
                    break;
                }
                var Sum_1 : f32 = Sum_0 - ShapeMatrix_0[_S16 + Earlier_1] * ShapeMatrix_0[_S11 + Earlier_1];
                Earlier_1 = Earlier_1 + u32(1);
                Sum_0 = Sum_1;
            }
            ShapeMatrix_0[_S17] = Sum_0 / Pivot_2;
            Row_2 = Row_2 + u32(128);
        }
        storageBarrier(); textureBarrier(); workgroupBarrier();;
        Column_1 = _S15;
    }
    if(Lane_0 == u32(0))
    {
        Row_2 = u32(0);
        for(;;)
        {
            if(Row_2 < Order_0)
            {
            }
            else
            {
                break;
            }
            var _S18 : u32 = _S6 + Row_2;
            Earlier_0 = u32(0);
            Pivot_0 = ShapeAdvances_0[_S18];
            for(;;)
            {
                if(Earlier_0 < Row_2)
                {
                }
                else
                {
                    break;
                }
                var Sum_2 : f32 = Pivot_0 - ShapeMatrix_0[_S5 + Row_2 * u32(96) + Earlier_0] * ShapeAdvances_0[_S6 + Earlier_0];
                Earlier_0 = Earlier_0 + u32(1);
                Pivot_0 = Sum_2;
            }
            ShapeAdvances_0[_S18] = Pivot_0 / ShapeMatrix_0[_S5 + Row_2 * u32(96) + Row_2];
            Row_2 = Row_2 + u32(1);
        }
        Earlier_0 = Order_0;
        for(;;)
        {
            if(Earlier_0 > u32(0))
            {
            }
            else
            {
                break;
            }
            var Row_3 : u32 = Earlier_0 - u32(1);
            var _S19 : u32 = _S6 + Row_3;
            Earlier_1 = Row_3 + u32(1);
            Pivot_0 = ShapeAdvances_0[_S19];
            for(;;)
            {
                if(Earlier_1 < Order_0)
                {
                }
                else
                {
                    break;
                }
                var Sum_3 : f32 = Pivot_0 - ShapeMatrix_0[_S5 + Earlier_1 * u32(96) + Row_3] * ShapeAdvances_0[_S6 + Earlier_1];
                Earlier_1 = Earlier_1 + u32(1);
                Pivot_0 = Sum_3;
            }
            ShapeAdvances_0[_S19] = Pivot_0 / ShapeMatrix_0[_S5 + Row_3 * u32(96) + Row_3];
            Earlier_0 = Row_3;
        }
    }
    storageBarrier(); textureBarrier(); workgroupBarrier();;
    Row_2 = Lane_0;
    for(;;)
    {
        if(Row_2 < Order_0)
        {
        }
        else
        {
            break;
        }
        var _S20 : u32 = _S6 + Row_2;
        ShapeMultipliers_0[_S20] = ShapeMultipliers_0[_S20] + ShapeAdvances_0[_S20];
        Row_2 = Row_2 + u32(128);
    }
    Earlier_0 = Lane_0;
    for(;;)
    {
        if(Earlier_0 < u32(9312))
        {
        }
        else
        {
            break;
        }
        atomicStore(&(ShapeSums_0[Sums_0 + Earlier_0]), i32(0));
        Earlier_0 = Earlier_0 + u32(128);
    }
    return;
}

