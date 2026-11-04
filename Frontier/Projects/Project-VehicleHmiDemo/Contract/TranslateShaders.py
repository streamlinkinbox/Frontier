#!/usr/bin/env python3
#============================================================================================================================================
#                                                         TRANSLATESHADERS.PY
#============================================================================================================================================
# 🧩 First-pass WGSL → Slang source translation for the 1:1 VehicleHMI illumination port (structure kept line for line).
#
# 📝 The translation is token based and deliberately conservative: it rewrites syntax (declarations, types, entry points,
#    builtins, pointer forms, matrix products against known matrix operands) and applies the naming-rule rename table, and
#    it reports every construct it could not translate with certainty. The emitted files are then reviewed and corrected
#    by hand; this tool is never re-run over corrected files.
#
#    python3 TranslateShaders.py <WgslFolder> <SlangFolder> File1.wgsl File2.wgsl ...

import json
import pathlib
import re
import sys

#------------------------------------------------------------------------------------------------------------------------
#                                                   NAMING TABLES
#------------------------------------------------------------------------------------------------------------------------

# 📝 Demo file stem → port file stem (SKILL-Naming.md: Record, Frame, Traversal are barred).
FileRenames = {
    "SceneRecords": "SceneLayouts",
    "FrameDescriptor": "RefreshDescriptor",
    "DistanceRecords": "DistanceLayouts",
    "DistanceTraversal": "DistanceMarch",
    "ReservoirRecords": "ReservoirLayouts",
    "RayTraversal": "RayIntersection",
    "ReflectionTraversal": "ReflectionTrace",
    "EffectRecords": "EffectLayouts",
    "CloudRecords": "CloudLayouts",
    "CloudShadowRecords": "CloudShadowLayouts",
}

# 📝 Demo identifier → port identifier. Applied to identifier tokens everywhere (declarations, uses, comments).
IdentifierRenames = {
    "Frame": "Refresh",
    "FrameRecord": "RefreshConstants",
    "FrameIndex": "RefreshIndex",
    "FrameDescriptor": "RefreshDescriptor",
    "ConstructTangentFrame": "ConstructTangentBasis",
    "SceneRecords": "SceneLayouts",
    "DistanceRecords": "DistanceLayouts",
    "ReservoirRecords": "ReservoirLayouts",
    "EffectRecords": "EffectLayouts",
    "CloudRecords": "CloudLayouts",
    "CloudShadowRecords": "CloudShadowLayouts",
    "OccupantRecord": "OccupantProfile",
    "CardRecord": "CardProfile",
    "MaterialRecord": "MaterialProfile",
    "ReservoirRecord": "PackedReservoir",
    "LightRecord": "LuminaireProfile",
    "DistanceRecord": "DistanceConstants",
    "VarianceRecord": "VarianceConstants",
    "TransferRecord": "TransferConstants",
    "VehicleRecord": "VehicleProfile",
    "EffectRecord": "EffectProfile",
    "MoonBodyRecord": "MoonBodyProfile",
    "AtmosphereRecord": "AtmosphereConstants",
    "ClipRecord": "ClipProfile",
    "CascadeRecord": "CascadeProfile",
    "CardWorkRecord": "CardTileJob",
    "BvhNodes": "BvhEntries",
    "BoundingNode": "BoundingEntry",
    "Node": "Entry",
    "Nodes": "Entries",
    "NodeIdx": "EntryIndex",
    "NodeCount": "EntryCount",
    "IntersectNodeBounds": "IntersectEntryBounds",
    "Emission": "Incandescence",
    "EmissionScale": "IncandescenceScale",
    "HitEmission": "HitIncandescence",
    "CardEmission": "CardIncandescence",
    "CosEmit": "CosExitant",
    "Emitters": "Luminaires",
    "Roles": "Participation",
    "OccRoles": "OccParticipation",
    "OccupantRole": "OccupantParticipation",
    "RoleRaster": "ParticipationRaster",
    "RoleDistance": "ParticipationDistance",
    "RoleCards": "ParticipationCards",
    "RoleDynamic": "ParticipationDynamic",
    "RoleFallback": "ParticipationFallback",
    "RoleShredded": "ParticipationShredded",
    "Footprint": "Coverage",
    "PixelFootprint": "PixelCoverage",
    "TraversalHit": "MarchHit",
    "TraversalDepth": "MarchDepth",
    "MaximumTraversalSteps": "MaximumMarchSteps",
    "ResolveTraversalRadiance": "ResolveMarchRadiance",
    "RayTraversal": "RayIntersection",
    "DistanceTraversal": "DistanceMarch",
    "ReflectionTraversal": "ReflectionTrace",
    "FeatureVarianceKernel": "FeatureVarianceFilter",
    "ClusterDraw": "ClusterCommand",
    "ClusterDraws": "ClusterCommands",
    "ClusteredLightBuffer": "ClusteredLightRegister",
}

# 📝 Banned prose words in comments → compliant wording (whole words, case kept for the first letter).
CommentRenames = {
    "frame": "refresh", "frames": "refreshes", "record": "profile", "records": "profiles", "node": "entry",
    "nodes": "entries", "pass": "dispatch", "passes": "dispatches", "kernel": "filter", "kernels": "filters",
    "stage": "step", "stages": "steps", "cache": "store", "cached": "retained", "caching": "retaining",
    "emission": "incandescence", "emissive": "incandescent", "emit": "radiate", "emits": "radiates",
    "emitted": "radiated", "emitting": "radiating", "emitter": "luminaire", "emitters": "luminaires",
    "traversal": "march", "traversals": "marches", "role": "participation", "roles": "participation",
    "data": "values", "buffer": "span", "buffers": "spans", "footprint": "coverage", "shared": "common-domain",
    "graph": "order", "model": "formulation", "models": "formulations", "hierarchy": "nesting",
}

#------------------------------------------------------------------------------------------------------------------------
#                                                   TYPE TABLES
#------------------------------------------------------------------------------------------------------------------------

ScalarTypes = {"f32": "float", "u32": "uint", "i32": "int", "bool": "bool", "f16": "half"}
VectorShort = {}
for Count in (2, 3, 4):
    VectorShort[f"vec{Count}f"] = f"float{Count}"
    VectorShort[f"vec{Count}u"] = f"uint{Count}"
    VectorShort[f"vec{Count}i"] = f"int{Count}"
    VectorShort[f"vec{Count}h"] = f"half{Count}"
MatrixShort = {f"mat{C}x{R}f": f"float{C}x{R}" for C in (2, 3, 4) for R in (2, 3, 4)}
StorageFormats = {"rgba16float": "rgba16f", "rgba32float": "rgba32f", "r32float": "r32f", "rgba8unorm": "rgba8",
                  "r32uint": "r32ui", "rg32float": "rg32f", "r16float": "r16f", "rgba16uint": "rgba16ui",
                  "rgba32uint": "rgba32ui", "rgba8snorm": "rgba8snorm", "rg32uint": "rg32ui"}

BuiltinRenames = {
    "fract": "frac", "inverseSqrt": "rsqrt", "mix": "lerp", "round": "RoundEven", "sign": "FloatSign",
    "countOneBits": "countbits", "firstLeadingBit": "firstbithigh", "firstTrailingBit": "firstbitlow",
    "reverseBits": "reversebits", "dpdx": "ddx", "dpdy": "ddy", "dpdxFine": "ddx_fine", "dpdyFine": "ddy_fine",
    "dpdxCoarse": "ddx_coarse", "dpdyCoarse": "ddy_coarse", "fma": "FusedMultiplyAdd", "faceForward": "faceforward",
    "pack2x16snorm": "PackSnormPair", "unpack2x16snorm": "UnpackSnormPair", "pack2x16float": "PackHalfPair",
    "unpack2x16float": "UnpackHalfPair", "pack4x8unorm": "PackUnormQuad", "unpack4x8unorm": "UnpackUnormQuad",
    "pack4x8snorm": "PackSnormQuad", "unpack4x8snorm": "UnpackSnormQuad", "pack2x16unorm": "PackUnormPair",
    "unpack2x16unorm": "UnpackUnormPair", "quantizeToF16": "QuantizeToHalf", "select": "SelectBranch",
    "workgroupBarrier": "GroupMemoryBarrierWithGroupSync", "storageBarrier": "DeviceMemoryBarrierWithGroupSync",
    "u32": "ToUnsigned", "i32": "ToSigned", "f32": "float", "arrayLength": "ArrayCount",
    "textureDimensions": "TextureExtent", "textureNumLayers": "TextureLayers", "atan2": "atan2",
    "inverse": "inverse", "saturate": "saturate", "trunc": "trunc", "modf": "modf",
}

#------------------------------------------------------------------------------------------------------------------------
#                                                   TOKENIZER
#------------------------------------------------------------------------------------------------------------------------

TokenPattern = re.compile(r"""
 (?P<comment>//[^\n]*)
|(?P<block>/\*.*?\*/)
|(?P<ws>[ \t\r]+)
|(?P<nl>\n)
|(?P<num>0[xX][0-9a-fA-F]+[uif]?|(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fhui]?)
|(?P<id>[^\W\d]\w*)
|(?P<op>->|<<=|>>=|==|!=|<=|>=|&&|\|\||\+\+|--|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|[{}()\[\];,.:<>+\-*/%&|^!~=@?])
|(?P<str>"[^"\n]*")
""", re.X | re.S | re.U)


class Token:
    __slots__ = ("Kind", "Text")

    def __init__(self, Kind, Text):
        self.Kind = Kind
        self.Text = Text

    def __repr__(self):
        return f"{self.Kind}:{self.Text!r}"


def Tokenize(Source):
    Tokens = []
    Position = 0
    while Position < len(Source):
        Match = TokenPattern.match(Source, Position)
        if not Match:
            raise ValueError(f"untokenizable text at {Position}: {Source[Position:Position + 40]!r}")
        Kind = Match.lastgroup
        Tokens.append(Token(Kind, Match.group(Kind)))
        Position = Match.end()
    return Tokens


def Significant(Tokens, Index, Step=1):
    """Index of the next (Step=1) or previous (Step=-1) non-trivia token."""
    Index += Step
    while 0 <= Index < len(Tokens) and Tokens[Index].Kind in ("ws", "nl", "comment", "block"):
        Index += Step
    return Index


def Join(Tokens):
    return "".join(Token.Text for Token in Tokens)


#------------------------------------------------------------------------------------------------------------------------
#                                                   TYPE PARSING
#------------------------------------------------------------------------------------------------------------------------

class TypeInfo:
    def __init__(self, Slang, ArraySuffix="", Runtime=False, Category="value", Element=None, Format=None,
                 Dimension=None, Pointer=False):
        self.Slang = Slang              # element / base Slang type
        self.ArraySuffix = ArraySuffix  # "[N]" for fixed arrays
        self.Runtime = Runtime          # array<T> without a count
        self.Category = Category        # value | texture | storage_texture | sampler | depth_texture
        self.Element = Element
        self.Format = Format
        self.Dimension = Dimension
        self.Pointer = Pointer


def ParseType(Tokens, Index):
    """Parses a WGSL type starting at Tokens[Index]; returns (TypeInfo, IndexAfter)."""
    Index = Index if Tokens[Index].Kind not in ("ws", "nl") else Significant(Tokens, Index)
    Name = Tokens[Index].Text
    After = Index + 1

    def Generic(Start):
        """Returns list of argument token-index ranges for <...> beginning at Tokens[Start] == '<'."""
        Depth = 0
        Arguments = []
        Current = Start + 1
        ArgumentStart = Current
        Cursor = Start
        while True:
            Text = Tokens[Cursor].Text
            if Text == "<":
                Depth += 1
            elif Text == ">":
                Depth -= 1
                if Depth == 0:
                    Arguments.append((ArgumentStart, Cursor))
                    return Arguments, Cursor + 1
            elif Text == ">>":
                Depth -= 2
                if Depth <= 0:
                    raise ValueError("'>>' closing generic — split manually")
            elif Text == "," and Depth == 1:
                Arguments.append((ArgumentStart, Cursor))
                ArgumentStart = Cursor + 1
            Cursor += 1

    def NextIs(Position, Text):
        if Position >= len(Tokens):
            return False, Position
        Position = Position if Tokens[Position].Kind not in ("ws", "nl") else Significant(Tokens, Position)
        return Position < len(Tokens) and Tokens[Position].Text == Text, Position

    if Name in ScalarTypes:
        return TypeInfo(ScalarTypes[Name]), After
    if Name in VectorShort:
        return TypeInfo(VectorShort[Name]), After
    if Name in MatrixShort:
        return TypeInfo(MatrixShort[Name]), After
    Opened, Position = NextIs(After, "<")
    if Name in ("vec2", "vec3", "vec4") and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[0][0])
        return TypeInfo(f"{Element.Slang}{Name[-1]}"), End
    if re.fullmatch(r"mat[234]x[234]", Name) and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[0][0])
        return TypeInfo(f"{Element.Slang}{Name[3]}x{Name[5]}"), End
    if Name == "atomic" and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[0][0])
        return TypeInfo(Element.Slang), End
    if Name == "array" and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[0][0])
        if len(Arguments) == 1:
            return TypeInfo(Element.Slang, Element.ArraySuffix, Runtime=True), End
        Count = Join(Tokens[Arguments[1][0]:Arguments[1][1]]).strip()
        Count = re.sub(r"^(\d+)[ui]$", r"\1", Count)
        return TypeInfo(Element.Slang, f"[{Count}]" + Element.ArraySuffix), End
    if Name == "ptr" and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[1][0])
        Element.Pointer = True
        return Element, End
    if Name.startswith("texture_storage_") and Opened:
        Arguments, End = Generic(Position)
        Format = Join(Tokens[Arguments[0][0]:Arguments[0][1]]).strip()
        Access = Join(Tokens[Arguments[1][0]:Arguments[1][1]]).strip()
        Dimension = Name[len("texture_storage_"):]
        Base = {"2d": "RWTexture2D", "3d": "RWTexture3D", "2d_array": "RWTexture2DArray", "1d": "RWTexture1D"}[Dimension]
        Element = "uint4" if Format.endswith("uint") else ("int4" if Format.endswith("sint") else "float4")
        return TypeInfo(f"{Base}<{Element}>", Category="storage_texture", Format=StorageFormats.get(Format, Format),
                        Dimension=Dimension), End
    if Name.startswith("texture_depth_"):
        Dimension = Name[len("texture_depth_"):]
        Base = {"2d": "Texture2D", "2d_array": "Texture2DArray", "cube": "TextureCube", "cube_array": "TextureCubeArray"}[Dimension]
        return TypeInfo(f"{Base}<float>", Category="depth_texture", Dimension=Dimension), After
    if Name.startswith("texture_") and Opened:
        Arguments, End = Generic(Position)
        Element, _ = ParseType(Tokens, Arguments[0][0])
        Dimension = Name[len("texture_"):]
        Base = {"1d": "Texture1D", "2d": "Texture2D", "3d": "Texture3D", "2d_array": "Texture2DArray",
                "cube": "TextureCube", "cube_array": "TextureCubeArray", "multisampled_2d": "Texture2DMS"}[Dimension]
        return TypeInfo(f"{Base}<{Element.Slang}4>", Category="texture", Dimension=Dimension), End
    if Name == "sampler":
        return TypeInfo("SamplerState", Category="sampler"), After
    if Name == "sampler_comparison":
        return TypeInfo("SamplerComparisonState", Category="sampler"), After
    # 📝 A struct or alias name — renamed through the identifier table.
    return TypeInfo(IdentifierRenames.get(Name, Name)), After


#------------------------------------------------------------------------------------------------------------------------
#                                                   TRANSLATOR
#------------------------------------------------------------------------------------------------------------------------

class Translator:
    def __init__(self, Globals):
        self.Globals = Globals          # cross-file symbol knowledge: resources, matrix names, struct names
        self.Reports = []

    def Report(self, File, Line, Message):
        self.Reports.append(f"{File}:{Line}: {Message}")

    # 📝 Module-scope pass: collects resource declarations and matrix-typed names across every file first, so the
    #    expression pass in any file knows the types of resources and fields declared in its includes.
    def CollectSymbols(self, File, Tokens):
        Index = 0
        while Index < len(Tokens):
            Text = Tokens[Index].Text
            if Text == "var" and Tokens[Index].Kind == "id":
                Next = Significant(Tokens, Index)
                Space = None
                if Tokens[Next].Text == "<":
                    End = Next
                    while Tokens[End].Text != ">":
                        End += 1
                    Space = Join(Tokens[Next + 1:End]).replace(" ", "")
                    Next = Significant(Tokens, End)
                Name = Tokens[Next].Text
                Colon = Significant(Tokens, Next)
                if Tokens[Colon].Text == ":":
                    Type, _ = ParseType(Tokens, Significant(Tokens, Colon))
                    self.Globals["types"].setdefault(Name, (Type.Slang, Type.Category, Type.Dimension, Space,
                                                            Type.ArraySuffix, Type.Runtime))
                    if re.fullmatch(r"float[234]x[234]", Type.Slang):
                        self.Globals["matrices"].add(Name)
            elif Text in ("struct",) and Tokens[Index].Kind == "id":
                Name = Tokens[Significant(Tokens, Index)].Text
                Open = Significant(Tokens, Significant(Tokens, Index))
                Depth = 0
                Cursor = Open
                while True:
                    if Tokens[Cursor].Text == "{":
                        Depth += 1
                    elif Tokens[Cursor].Text == "}":
                        Depth -= 1
                        if Depth == 0:
                            break
                    elif Depth == 1 and Tokens[Cursor].Kind == "id":
                        Colon = Significant(Tokens, Cursor)
                        if Tokens[Colon].Text == ":":
                            Type, _ = ParseType(Tokens, Significant(Tokens, Colon))
                            if re.fullmatch(r"float[234]x[234]", Type.Slang):
                                self.Globals["matrices"].add(Tokens[Cursor].Text)
                    Cursor += 1
                self.Globals["structs"].add(Name)
            elif Text == "fn" and Tokens[Index].Kind == "id":
                Name = Tokens[Significant(Tokens, Index)].Text
                Cursor = Index
                Depth = 0
                while True:
                    if Tokens[Cursor].Text == "(":
                        Depth += 1
                    elif Tokens[Cursor].Text == ")":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Cursor += 1
                Arrow = Significant(Tokens, Cursor)
                if Tokens[Arrow].Text == "->":
                    ReturnStart = Significant(Tokens, Arrow)
                    if Tokens[ReturnStart].Text != "@":
                        Type, _ = ParseType(Tokens, ReturnStart)
                        if re.fullmatch(r"float[234]x[234]", Type.Slang):
                            self.Globals["matrix_functions"].add(Name)
            Index += 1

    def Translate(self, File, Source):
        Tokens = Tokenize(Source)
        Output = []
        Index = 0
        LineNumber = 1
        self.File = File
        self.Locals = {}                # function-local names → Slang type
        self.LocalResources = {}        # function texture parameters → (dimension, category)
        self.PendingAttributes = []

        def Emit(Text):
            Output.append(Text)

        while Index < len(Tokens):
            Token = Tokens[Index]
            if Token.Kind == "nl":
                LineNumber += 1
                Emit("\n")
                Index += 1
                continue
            if Token.Kind in ("ws",):
                Emit(Token.Text)
                Index += 1
                continue
            if Token.Kind in ("comment", "block"):
                Emit(self.TranslateComment(Token.Text))
                LineNumber += Token.Text.count("\n")
                Index += 1
                continue
            # 📝 Module-scope constructs.
            if Token.Text == "@" or (Token.Kind == "id" and Token.Text in ("struct", "fn", "var", "const", "alias", "enable",
                                                                         "override", "diagnostic", "requires")):
                Index, Text = self.TranslateDeclaration(Tokens, Index)
                LineNumber += Text.count("\n")
                Emit(Text)
                continue
            if Token.Text == ";":
                Emit(";")
                Index += 1
                continue
            self.Report(File, LineNumber, f"unexpected module-scope token {Token.Text!r}")
            Emit(Token.Text)
            Index += 1
        return "".join(Output)

    #--------------------------------------------------------------------------------------------------------------------
    #                                               COMMENTS
    #--------------------------------------------------------------------------------------------------------------------

    def TranslateComment(self, Text):
        Include = re.match(r'^//!\s*include\s+"([^"]+)\.wgsl"\s*$', Text)
        if Include:
            Stem = FileRenames.get(Include.group(1), Include.group(1))
            return f'#include "{Stem}.slang"'
        Text = re.sub(r"([A-Za-z][A-Za-z0-9]*)\.WGSL", lambda M: FileRenames.get(self.StemFromBanner(M.group(1)),
                                                                                   self.StemFromBanner(M.group(1))).upper() + ".SLANG", Text)
        Text = re.sub(r"\b([A-Za-z][A-Za-z0-9]*)\.wgsl\b", lambda M: FileRenames.get(M.group(1), M.group(1)) + ".slang", Text)

        def RenameIdentifier(Match):
            Word = Match.group(0)
            if Word in IdentifierRenames:
                return IdentifierRenames[Word]
            Lower = Word.lower()
            if Lower in CommentRenames and (Word == Lower or Word == Lower.capitalize()):
                Replacement = CommentRenames[Lower]
                return Replacement.capitalize() if Word[0].isupper() else Replacement
            return Word

        return re.sub(r"[A-Za-z_][A-Za-z0-9_]*", RenameIdentifier, Text)

    def StemFromBanner(self, Upper):
        for Stem in list(FileRenames.keys()) + list(self.Globals["stems"]):
            if Stem.upper() == Upper:
                return Stem
        return Upper

    #--------------------------------------------------------------------------------------------------------------------
    #                                               DECLARATIONS
    #--------------------------------------------------------------------------------------------------------------------

    def TranslateDeclaration(self, Tokens, Index):
        Attributes = []
        Start = Index
        Prefix = []
        while Tokens[Index].Text == "@":
            NameIndex = Index + 1
            Name = Tokens[NameIndex].Text
            Arguments = None
            After = NameIndex + 1
            if After < len(Tokens) and Tokens[After].Text == "(":
                Depth = 0
                Cursor = After
                while True:
                    if Tokens[Cursor].Text == "(":
                        Depth += 1
                    elif Tokens[Cursor].Text == ")":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Cursor += 1
                Arguments = Join(Tokens[After + 1:Cursor]).strip()
                After = Cursor + 1
            Attributes.append((Name, Arguments))
            # 📝 Keep line breaks between attributes and the declaration.
            Index = After
            while Tokens[Index].Kind in ("ws", "nl", "comment"):
                Prefix.append(Tokens[Index].Text if Tokens[Index].Kind != "comment" else self.TranslateComment(Tokens[Index].Text))
                Index += 1
        Keyword = Tokens[Index].Text
        if Keyword in ("enable", "diagnostic", "requires"):
            End = Index
            while Tokens[End].Text != ";":
                End += 1
            return End + 1, "// (WGSL directive dropped) " + Join(Tokens[Index:End + 1])
        if Keyword == "alias":
            NameIndex = Significant(Tokens, Index)
            Equal = Significant(Tokens, NameIndex)
            Type, After = ParseType(Tokens, Significant(Tokens, Equal))
            Semicolon = Significant(Tokens, After - 1)
            return Semicolon + 1, f"typealias {IdentifierRenames.get(Tokens[NameIndex].Text, Tokens[NameIndex].Text)} = {Type.Slang}{Type.ArraySuffix};"
        if Keyword == "struct":
            return self.TranslateStruct(Tokens, Index, "".join(Prefix))
        if Keyword in ("const", "override"):
            return self.TranslateConstant(Tokens, Index, Attributes, Module=True)
        if Keyword == "var":
            return self.TranslateGlobalVariable(Tokens, Index, Attributes, "".join(Prefix))
        if Keyword == "fn":
            return self.TranslateFunction(Tokens, Index, Attributes, "".join(Prefix))
        raise ValueError(f"{self.File}: unhandled declaration {Keyword}")

    def TranslateStruct(self, Tokens, Index, Prefix):
        NameIndex = Significant(Tokens, Index)
        Name = IdentifierRenames.get(Tokens[NameIndex].Text, Tokens[NameIndex].Text)
        Output = [Prefix, "struct ", Name]
        Cursor = NameIndex + 1
        # 📝 Copy trivia up to the brace.
        while Tokens[Cursor].Text != "{":
            Output.append(Tokens[Cursor].Text if Tokens[Cursor].Kind != "comment" else self.TranslateComment(Tokens[Cursor].Text))
            Cursor += 1
        Output.append("{")
        Cursor += 1
        while True:
            Token = Tokens[Cursor]
            if Token.Text == "}":
                Output.append("};")
                Cursor += 1
                # 📝 WGSL allows an optional ';' after a struct.
                Next = Significant(Tokens, Cursor - 1)
                if Next < len(Tokens) and Tokens[Next].Text == ";":
                    Cursor = Next + 1
                return Cursor, "".join(Output)
            if Token.Kind in ("ws", "nl"):
                Output.append(Token.Text)
                Cursor += 1
                continue
            if Token.Kind in ("comment", "block"):
                Output.append(self.TranslateComment(Token.Text))
                Cursor += 1
                continue
            # 📝 One member: [@attr...] Name : Type [,]
            MemberAttributes = []
            while Tokens[Cursor].Text == "@":
                AttributeName = Tokens[Cursor + 1].Text
                AttributeArguments = None
                After = Cursor + 2
                if Tokens[After].Text == "(":
                    Close = After
                    while Tokens[Close].Text != ")":
                        Close += 1
                    AttributeArguments = Join(Tokens[After + 1:Close]).strip()
                    After = Close + 1
                MemberAttributes.append((AttributeName, AttributeArguments))
                Cursor = Significant(Tokens, After - 1)
            MemberName = Tokens[Cursor].Text
            Colon = Significant(Tokens, Cursor)
            # 📝 Preserve the padding between name and colon as padding after the type, to keep column alignment.
            Padding = Join(Tokens[Cursor + 1:Colon])
            Type, After = ParseType(Tokens, Significant(Tokens, Colon))
            Cursor = After
            Next = Significant(Tokens, Cursor - 1)
            Trailing = ""
            if Tokens[Next].Text == ",":
                Trailing = Join(Tokens[Cursor:Next])
                Cursor = Next + 1
            Semantic = ""
            Qualifier = ""
            for AttributeName, AttributeArguments in MemberAttributes:
                if AttributeName == "builtin":
                    Semantic = {"position": " : SV_Position", "frag_depth": " : SV_Depth"}.get(AttributeArguments, f" : /*{AttributeArguments}*/")
                elif AttributeName == "location":
                    Qualifier += f"[[vk::location({AttributeArguments})]] "
                    Semantic = Semantic or f" : LOCATION{AttributeArguments}"
                elif AttributeName == "interpolate":
                    if "flat" in AttributeArguments:
                        Qualifier += "nointerpolation "
                    elif "linear" in AttributeArguments:
                        Qualifier += "noperspective "
                    if "centroid" in AttributeArguments:
                        Qualifier += "centroid "
                elif AttributeName in ("size", "align"):
                    self.Report(self.File, 0, f"struct {Name}.{MemberName} uses @{AttributeName}({AttributeArguments})")
            Renamed = IdentifierRenames.get(MemberName, MemberName)
            Output.append(f"{Qualifier}{Type.Slang} {Renamed}{Type.ArraySuffix}{Semantic};{Padding}{Trailing}")
            if Type.Runtime:
                self.Report(self.File, 0, f"struct {Name}.{MemberName} is a runtime-sized array")

    def TranslateConstant(self, Tokens, Index, Attributes, Module):
        NameIndex = Significant(Tokens, Index)
        Name = IdentifierRenames.get(Tokens[NameIndex].Text, Tokens[NameIndex].Text)
        Cursor = Significant(Tokens, NameIndex)
        TypeText = None
        if Tokens[Cursor].Text == ":":
            Type, After = ParseType(Tokens, Significant(Tokens, Cursor))
            TypeText = (Type.Slang, Type.ArraySuffix)
            Cursor = Significant(Tokens, After - 1)
        assert Tokens[Cursor].Text == "=", f"{self.File}: const {Name} without initializer"
        End = Cursor
        Depth = 0
        while True:
            Text = Tokens[End].Text
            if Text in "([{":
                Depth += 1
            elif Text in ")]}":
                Depth -= 1
            elif Text == ";" and Depth == 0:
                break
            End += 1
        Expression = self.TranslateExpression(Tokens[Cursor + 1:End])
        if TypeText is None:
            TypeText = (self.InferType(Expression), "")
        Storage = "static const " if Module else "const "
        Initializer = Expression.strip()
        ArrayInitial = re.match(r"^\s*ArrayConstruct<[^>]*>\((.*)\)\s*$", Initializer, re.S)
        if ArrayInitial:
            Initializer = "{" + ArrayInitial.group(1) + "}"
        Gap = Join(Tokens[NameIndex + 1:Significant(Tokens, NameIndex)]) if TypeText else " "
        return End + 1, f"{Storage}{TypeText[0]} {Name}{TypeText[1]} = {Initializer};"

    def InferType(self, Expression):
        Text = Expression.strip()
        if re.fullmatch(r"-?(0[xX][0-9a-fA-F]+|\d+)u", Text):
            return "uint"
        if re.fullmatch(r"-?\d+", Text):
            return "int"
        if re.fullmatch(r"-?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?f?", Text) or re.fullmatch(r"-?\d+\.\d*([eE][+-]?\d+)?", Text):
            return "float"
        Constructor = re.match(r"^(float|uint|int|bool)([234])?(x[234])?\(", Text)
        if Constructor:
            return Constructor.group(0)[:-1]
        self.Report(self.File, 0, f"could not infer const type of {Text!r} — defaulted to float")
        return "float"

    def TranslateGlobalVariable(self, Tokens, Index, Attributes, Prefix):
        Cursor = Significant(Tokens, Index)
        Space = None
        if Tokens[Cursor].Text == "<":
            End = Cursor
            while Tokens[End].Text != ">":
                End += 1
            Space = Join(Tokens[Cursor + 1:End]).replace(" ", "")
            Cursor = Significant(Tokens, End)
        NameIndex = Cursor
        Name = IdentifierRenames.get(Tokens[NameIndex].Text, Tokens[NameIndex].Text)
        Colon = Significant(Tokens, NameIndex)
        Padding = Join(Tokens[NameIndex + 1:Colon]) or " "
        Type, After = ParseType(Tokens, Significant(Tokens, Colon))
        Semicolon = Significant(Tokens, After - 1)
        Initializer = None
        if Tokens[Semicolon].Text == "=":
            End = Semicolon
            while Tokens[End].Text != ";":
                End += 1
            Initializer = self.TranslateExpression(Tokens[Semicolon + 1:End]).strip()
            Semicolon = End
        Group = Binding = None
        for AttributeName, AttributeArguments in Attributes:
            if AttributeName == "group":
                Group = AttributeArguments
            elif AttributeName == "binding":
                Binding = AttributeArguments
        Decoration = f"[[vk::binding({Binding}, {Group})]] " if Binding is not None else ""
        if Space == "workgroup":
            Text = f"groupshared {Type.Slang} {Name}{Type.ArraySuffix};"
        elif Space == "private":
            Text = f"static {Type.Slang} {Name}{Type.ArraySuffix}" + (f" = {Initializer};" if Initializer else ";")
        elif Space == "uniform":
            Text = f"{Decoration}ConstantBuffer<{Type.Slang}>{Padding}{Name};"
        elif Space and Space.startswith("storage"):
            Writable = "read_write" in Space
            Base = "RWStructuredBuffer" if Writable else "StructuredBuffer"
            if Type.Runtime:
                Text = f"{Decoration}{Base}<{Type.Slang}{Type.ArraySuffix}>{Padding}{Name};"
            elif Type.ArraySuffix:
                Inner = Type.ArraySuffix[Type.ArraySuffix.index("]") + 1:]
                Text = f"{Decoration}{Base}<{Type.Slang}{Inner}>{Padding}{Name};   // fixed {Type.ArraySuffix} span"
            else:
                self.Globals["single_storage"].add(Name)
                Text = f"{Decoration}{Base}<{Type.Slang}{Type.ArraySuffix}>{Padding}{Name};   // single-element span: read as {Name}[0]"
        elif Type.Category == "storage_texture":
            Text = f"{Decoration}[[vk::image_format(\"{Type.Format}\")]] {Type.Slang}{Padding}{Name};"
        else:
            Text = f"{Decoration}{Type.Slang}{Padding}{Name};"
        return Semicolon + 1, Prefix + Text

    #--------------------------------------------------------------------------------------------------------------------
    #                                               FUNCTIONS
    #--------------------------------------------------------------------------------------------------------------------

    def TranslateFunction(self, Tokens, Index, Attributes, Prefix):
        NameIndex = Significant(Tokens, Index)
        RawName = Tokens[NameIndex].Text
        Name = IdentifierRenames.get(RawName, RawName)
        Open = Significant(Tokens, NameIndex)
        assert Tokens[Open].Text == "("
        # 📝 Parameters.
        Depth = 0
        Cursor = Open
        while True:
            if Tokens[Cursor].Text == "(":
                Depth += 1
            elif Tokens[Cursor].Text == ")":
                Depth -= 1
                if Depth == 0:
                    break
            Cursor += 1
        Close = Cursor
        self.Locals = {}
        self.LocalResources = {}
        Parameters = self.TranslateParameters(Tokens, Open + 1, Close)
        Cursor = Significant(Tokens, Close)
        ReturnType = "void"
        ReturnSemantic = ""
        if Tokens[Cursor].Text == "->":
            Cursor = Significant(Tokens, Cursor)
            ReturnAttributes = []
            while Tokens[Cursor].Text == "@":
                AttributeName = Tokens[Cursor + 1].Text
                AttributeArguments = None
                After = Cursor + 2
                if Tokens[After].Text == "(":
                    CloseAttribute = After
                    while Tokens[CloseAttribute].Text != ")":
                        CloseAttribute += 1
                    AttributeArguments = Join(Tokens[After + 1:CloseAttribute]).strip()
                    After = CloseAttribute + 1
                ReturnAttributes.append((AttributeName, AttributeArguments))
                Cursor = Significant(Tokens, After - 1)
            Type, After = ParseType(Tokens, Cursor)
            ReturnType = Type.Slang + (Type.ArraySuffix and "")
            for AttributeName, AttributeArguments in ReturnAttributes:
                if AttributeName == "builtin" and AttributeArguments == "position":
                    ReturnSemantic = " : SV_Position"
                elif AttributeName == "location":
                    ReturnSemantic = f" : SV_Target{AttributeArguments}"
            Cursor = Significant(Tokens, After - 1)
        assert Tokens[Cursor].Text == "{", f"{self.File}: function {Name} body"
        BodyEnd = self.MatchBrace(Tokens, Cursor)
        Header = []
        Stage = None
        for AttributeName, AttributeArguments in Attributes:
            if AttributeName in ("compute", "vertex", "fragment"):
                Stage = AttributeName
            elif AttributeName == "workgroup_size":
                Sizes = [Part.strip() for Part in AttributeArguments.split(",")]
                Sizes = [re.sub(r"^(\d+)[ui]$", r"\1", Part) for Part in Sizes]
                while len(Sizes) < 3:
                    Sizes.append("1")
                Header.append(f"[numthreads({', '.join(Sizes)})]")
        if Stage:
            Header.insert(0, f"[shader(\"{ {'fragment': 'fragment', 'vertex': 'vertex', 'compute': 'compute'}[Stage] }\")]")
            self.Globals["entries"].append((self.File, Name, Stage))
        Body = self.TranslateBlock(Tokens, Cursor, BodyEnd)
        Separator = Join(Tokens[Close + 1:Close + 1])
        HeaderText = "\n".join(Header) + ("\n" if Header else "")
        Text = f"{Prefix}{HeaderText}{ReturnType} {Name}({Parameters}){ReturnSemantic}" + self.TriviaBetween(Tokens, Close, Cursor) + Body
        return BodyEnd + 1, Text

    def TriviaBetween(self, Tokens, Close, Brace):
        """Keeps the newline style between ')' (or return type) and '{' (Allman braces stay Allman)."""
        Between = Join(Tokens[Significant(Tokens, Brace, -1) + 1:Brace])
        return Between if Between else " "

    def MatchBrace(self, Tokens, Open):
        Depth = 0
        Cursor = Open
        while True:
            if Tokens[Cursor].Text == "{":
                Depth += 1
            elif Tokens[Cursor].Text == "}":
                Depth -= 1
                if Depth == 0:
                    return Cursor
            Cursor += 1

    def TranslateParameters(self, Tokens, Start, End):
        Parts = []
        Cursor = Start
        Current = []
        Depth = 0
        while Cursor < End:
            Text = Tokens[Cursor].Text
            if Text in "(<":
                Depth += 1
            elif Text in ")>":
                Depth -= 1
            if Text == "," and Depth == 0:
                Parts.append(Current)
                Current = []
            else:
                Current.append(Tokens[Cursor])
            Cursor += 1
        if any(Token.Kind not in ("ws", "nl") for Token in Current):
            Parts.append(Current)
        Translated = []
        for Part in Parts:
            Leading = ""
            Index = 0
            while Index < len(Part) and Part[Index].Kind in ("ws", "nl", "comment"):
                Leading += Part[Index].Text
                Index += 1
            Attributes = []
            while Index < len(Part) and Part[Index].Text == "@":
                AttributeName = Part[Index + 1].Text
                AttributeArguments = None
                After = Index + 2
                if After < len(Part) and Part[After].Text == "(":
                    Close = After
                    while Part[Close].Text != ")":
                        Close += 1
                    AttributeArguments = Join(Part[After + 1:Close]).strip()
                    After = Close + 1
                Attributes.append((AttributeName, AttributeArguments))
                Index = After
                while Index < len(Part) and Part[Index].Kind in ("ws", "nl"):
                    Index += 1
            ParameterName = Part[Index].Text
            Colon = Index + 1
            while Part[Colon].Text != ":":
                Colon += 1
            Type, _ = ParseType(Part, Colon + 1)
            Renamed = IdentifierRenames.get(ParameterName, ParameterName)
            Semantic = ""
            Qualifier = ""
            for AttributeName, AttributeArguments in Attributes:
                if AttributeName == "builtin":
                    Semantic = {
                        "global_invocation_id": " : SV_DispatchThreadID", "local_invocation_id": " : SV_GroupThreadID",
                        "workgroup_id": " : SV_GroupID", "local_invocation_index": " : SV_GroupIndex",
                        "instance_index": " : SV_VulkanInstanceID", "vertex_index": " : SV_VulkanVertexID",
                        "front_facing": " : SV_IsFrontFace", "position": " : SV_Position",
                        "sample_index": " : SV_SampleIndex", "num_workgroups": " : /*num_workgroups*/",
                    }[AttributeArguments]
                elif AttributeName == "location":
                    Qualifier = f"[[vk::location({AttributeArguments})]] "
                    Semantic = f" : LOCATION{AttributeArguments}"
                elif AttributeName == "interpolate" and "flat" in (AttributeArguments or ""):
                    Qualifier += "nointerpolation "
            Prefix = "inout " if Type.Pointer else ""
            self.Locals[Renamed] = Type.Slang
            if Type.Category != "value":
                self.LocalResources[ParameterName] = (Type.Dimension, Type.Category)
            Translated.append(f"{Leading}{Qualifier}{Prefix}{Type.Slang} {Renamed}{Type.ArraySuffix}{Semantic}")
        return ",".join(Translated)

    #--------------------------------------------------------------------------------------------------------------------
    #                                               STATEMENTS
    #--------------------------------------------------------------------------------------------------------------------

    def TranslateBlock(self, Tokens, Open, Close):
        """Translates the tokens from '{' to its matching '}' inclusive, statement by statement."""
        Output = []
        Cursor = Open
        SwitchStack = []
        while Cursor <= Close:
            Token = Tokens[Cursor]
            Text = Token.Text
            if Token.Kind in ("ws", "nl"):
                Output.append(Text)
                Cursor += 1
                continue
            if Token.Kind in ("comment", "block"):
                Output.append(self.TranslateComment(Text))
                Cursor += 1
                continue
            if Text in ("let", "var", "const") and Token.Kind == "id":
                Cursor, Statement = self.TranslateLocal(Tokens, Cursor)
                Output.append(Statement)
                continue
            if Text == "loop" and Token.Kind == "id":
                Output.append("while (true)")
                Cursor += 1
                continue
            if Text == "continuing" and Token.Kind == "id":
                self.Report(self.File, 0, "continuing block needs manual translation")
                Output.append("/*continuing*/")
                Cursor += 1
                continue
            if Text == "switch" and Token.Kind == "id":
                # 📝 Translate the selector expression, then remember the brace so case blocks get a trailing break.
                Open = Significant(Tokens, Cursor)
                Depth = 0
                Scan = Open
                while True:
                    if Tokens[Scan].Text == "(":
                        Depth += 1
                    elif Tokens[Scan].Text == ")":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Scan += 1
                Output.append("switch " + self.TranslateExpression(Tokens[Open:Scan + 1]))
                Brace = Significant(Tokens, Scan)
                Output.append(Join(Tokens[Scan + 1:Brace]))
                SwitchStack.append(self.MatchBrace(Tokens, Brace))
                Output.append("{")
                Cursor = Brace + 1
                continue
            if Text in ("case", "default") and Token.Kind == "id" and SwitchStack:
                # 📝 case A, B: { ... }  →  case A: case B: { ... } break;
                Colon = Cursor
                while Tokens[Colon].Text not in (":", "{"):
                    Colon += 1
                Selectors = Join(Tokens[Cursor:Colon]).strip()
                if Selectors.startswith("case"):
                    Values = [Value.strip() for Value in Selectors[4:].split(",") if Value.strip()]
                    Labels = " ".join("default:" if Value == "default" else f"case {self.TranslateExpression(Tokenize(Value)).strip()}:" for Value in Values)
                else:
                    Labels = "default:"
                Brace = Colon if Tokens[Colon].Text == "{" else Significant(Tokens, Colon)
                BlockEnd = self.MatchBrace(Tokens, Brace)
                Output.append(Labels + Join(Tokens[Colon + 1:Brace]) if Tokens[Colon].Text == ":" else Labels + " ")
                Output.append(self.TranslateBlock(Tokens, Brace, BlockEnd))
                Output.append(" break;")
                Cursor = BlockEnd + 1
                continue
            if Text == "{" and Cursor != Open:
                End = self.MatchBrace(Tokens, Cursor)
                Output.append(self.TranslateBlock(Tokens, Cursor, End))
                Cursor = End + 1
                continue
            if Text in ("{", "}"):
                Output.append(Text)
                Cursor += 1
                continue
            if Text in ("if", "while", "for") and Token.Kind == "id":
                Paren = Significant(Tokens, Cursor)
                if Tokens[Paren].Text != "(":
                    self.Report(self.File, 0, f"{Text} without parentheses")
                Depth = 0
                Scan = Paren
                while True:
                    if Tokens[Scan].Text == "(":
                        Depth += 1
                    elif Tokens[Scan].Text == ")":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Scan += 1
                if Text == "for":
                    Inner = Tokens[Paren + 1:Scan]
                    Pieces = self.SplitTopLevel(Inner, ";")
                    Initial = Pieces[0]
                    InitialText = Join(Initial).strip()
                    if InitialText.startswith(("var", "let")):
                        _, InitialStatement = self.TranslateLocal(Initial + [Token_(";")], 0)
                        InitialStatement = InitialStatement.rstrip(";")
                    else:
                        InitialStatement = self.TranslateExpression(Initial)
                    Rest = [self.TranslateExpression(Piece) for Piece in Pieces[1:]]
                    Output.append("for (" + "; ".join([InitialStatement.strip()] + [Part.strip() for Part in Rest]) + ")")
                else:
                    Output.append(Text + " " + self.TranslateExpression(Tokens[Paren:Scan + 1]))
                Cursor = Scan + 1
                continue
            if Text == "else" and Token.Kind == "id":
                Output.append("else")
                Cursor += 1
                continue
            if Text in ("return", "break", "continue", "discard") and Token.Kind == "id":
                End = Cursor
                Depth = 0
                while True:
                    T = Tokens[End].Text
                    if T in "([{":
                        Depth += 1
                    elif T in ")]}":
                        Depth -= 1
                    elif T == ";" and Depth == 0:
                        break
                    End += 1
                if Text == "return" and Significant(Tokens, Cursor) < End:
                    Output.append("return " + self.TranslateExpression(Tokens[Significant(Tokens, Cursor):End]).strip() + ";")
                else:
                    Output.append(Join(Tokens[Cursor:End + 1]))
                Cursor = End + 1
                continue
            # 📝 Expression statement (assignment, call, increment).
            End = Cursor
            Depth = 0
            while True:
                T = Tokens[End].Text
                if T in "([":
                    Depth += 1
                elif T in ")]":
                    Depth -= 1
                elif T == ";" and Depth == 0:
                    break
                elif T == "}" and Depth == 0:
                    break
                End += 1
            Output.append(self.TranslateStatementExpression(Tokens[Cursor:End]) + (";" if Tokens[End].Text == ";" else ""))
            Cursor = End + 1 if Tokens[End].Text == ";" else End
        return "".join(Output)

    def SplitTopLevel(self, Tokens, Separator):
        Parts = [[]]
        Depth = 0
        for Token in Tokens:
            if Token.Text in "([{":
                Depth += 1
            elif Token.Text in ")]}":
                Depth -= 1
            if Token.Text == Separator and Depth == 0:
                Parts.append([])
            else:
                Parts[-1].append(Token)
        return Parts

    def TranslateStatementExpression(self, Tokens):
        Text = Join(Tokens)
        # 📝 Atomic statements whose result is discarded.
        Atomic = re.match(r"^\s*atomic(Store|Min|Max|Add|Or|And|Xor|Exchange)\s*\(\s*&\s*(.+?)\s*,\s*(.+)\)\s*$", Text, re.S)
        if Atomic:
            Operation, Target, Operand = Atomic.groups()
            Target = self.TranslateExpression(Tokenize(Target)).strip()
            Operand = self.TranslateExpression(Tokenize(Operand)).strip()
            if Operation == "Store":
                return f"InterlockedExchange({Target}, {Operand}, AtomicDiscard)"
            return f"Interlocked{Operation}({Target}, {Operand})"
        # 📝 Discard assignments: _ = expr;
        if re.match(r"^\s*_\s*=", Text):
            return "/*discarded*/ " + self.TranslateExpression(Tokens[[T.Text for T in Tokens].index("=") + 1:])
        # 📝 textureStore(T, C, V) → T[C] = V
        Store = re.match(r"^\s*textureStore\s*\(", Text)
        if Store:
            Inner = Tokens[[T.Text for T in Tokens].index("(") + 1:len(Tokens) - 1 - [T.Text for T in reversed(Tokens)].index(")")]
            Arguments = self.SplitTopLevel(Inner, ",")
            Texture = self.TranslateExpression(Arguments[0]).strip()
            Coordinates = self.TranslateExpression(Arguments[1]).strip()
            if len(Arguments) == 4:
                Layer = self.TranslateExpression(Arguments[2]).strip()
                Coordinates = f"uint3(uint2({Coordinates}), uint({Layer}))"
                Value = self.TranslateExpression(Arguments[3]).strip()
            else:
                Value = self.TranslateExpression(Arguments[2]).strip()
            return f"{Texture}[{Coordinates}] = {Value}"
        return self.TranslateExpression(Tokens)

    def TranslateLocal(self, Tokens, Cursor):
        Keyword = Tokens[Cursor].Text
        NameIndex = Significant(Tokens, Cursor)
        Name = IdentifierRenames.get(Tokens[NameIndex].Text, Tokens[NameIndex].Text)
        Scan = Significant(Tokens, NameIndex)
        Type = None
        if Tokens[Scan].Text == ":":
            Type, After = ParseType(Tokens, Significant(Tokens, Scan))
            Scan = Significant(Tokens, After - 1)
        End = Scan
        Depth = 0
        while True:
            Text = Tokens[End].Text
            if Text in "([{":
                Depth += 1
            elif Text in ")]}":
                Depth -= 1
            elif Text == ";" and Depth == 0:
                break
            End += 1
        Initializer = None
        if Tokens[Scan].Text == "=":
            Initializer = self.TranslateExpression(Tokens[Scan + 1:End]).strip()
        Slang = "let" if Keyword in ("let", "const") else "var"
        if Type is not None:
            self.Locals[Name] = Type.Slang
            if re.fullmatch(r"float[234]x[234]", Type.Slang):
                self.Globals["local_matrices"].add(Name)
            Declared = f"{Slang} {Name} : {Type.Slang}{Type.ArraySuffix}"
            if Initializer is None:
                return End + 1, f"{Declared} = {{}};"
        else:
            Declared = f"{Slang} {Name}"
            if Initializer is not None:
                Inferred = self.InferExpressionType(Initializer)
                if Inferred:
                    self.Locals[Name] = Inferred
                    if re.fullmatch(r"float[234]x[234]", Inferred):
                        self.Globals["local_matrices"].add(Name)
        ArrayInitial = re.match(r"^ArrayConstruct<([^,>]+),\s*([^>]+)>\((.*)\)$", Initializer or "", re.S)
        if ArrayInitial:
            Element, Count, Values = ArrayInitial.groups()
            return End + 1, f"{Slang} {Name} : {Element}[{Count}] = {{{Values}}};"
        return End + 1, f"{Declared} = {Initializer};"

    def InferExpressionType(self, Expression):
        Constructor = re.match(r"^(float[234]x[234])\(", Expression)
        if Constructor:
            return Constructor.group(1)
        Names = set(IdentifierRenames.get(Name, Name) for Name in self.Globals["matrices"]) | set(self.Globals["local_matrices"])
        Field = re.match(r"^[A-Za-z_][\w.\[\]]*?\.?([A-Za-z_]\w*)$", Expression)
        if Field and Field.group(1) in Names:
            return "float4x4"
        Call = re.match(r"^([A-Za-z_]\w*)\(", Expression)
        if Call and Call.group(1) in {IdentifierRenames.get(N, N) for N in self.Globals["matrix_functions"]}:
            return "float3x3"
        return None

    #--------------------------------------------------------------------------------------------------------------------
    #                                               EXPRESSIONS
    #--------------------------------------------------------------------------------------------------------------------

    def TranslateExpression(self, Tokens):
        Tokens = [Token for Token in Tokens]
        Output = []
        Index = 0
        while Index < len(Tokens):
            Token = Tokens[Index]
            Text = Token.Text
            if Token.Kind == "comment" or Token.Kind == "block":
                Output.append(self.TranslateComment(Text))
                Index += 1
                continue
            if Token.Kind == "num":
                Output.append(self.TranslateNumber(Text))
                Index += 1
                continue
            if Token.Kind == "id":
                Next = Significant(Tokens, Index) if Index + 1 < len(Tokens) else len(Tokens)
                NextText = Tokens[Next].Text if Next < len(Tokens) else ""
                # 📝 Generic constructors / casts: vec3<f32>(...), array<T, N>(...), bitcast<T>(...)
                if NextText == "<" and Text in ("vec2", "vec3", "vec4", "array", "bitcast", "mat2x2", "mat3x3", "mat4x4",
                                                 "mat2x3", "mat3x2", "mat2x4", "mat4x2", "mat3x4", "mat4x3"):
                    Depth = 0
                    Scan = Next
                    while True:
                        if Tokens[Scan].Text == "<":
                            Depth += 1
                        elif Tokens[Scan].Text == ">":
                            Depth -= 1
                            if Depth == 0:
                                break
                        Scan += 1
                    Type, _ = ParseType(Tokens, Index)
                    if Text == "bitcast":
                        Target = Type.Slang if False else ParseType(Tokens, Next + 1)[0].Slang
                        Base = re.sub(r"\d$", "", Target)
                        Output.append({"float": "asfloat", "uint": "asuint", "int": "asint"}[Base])
                    elif Text == "array":
                        Element, _ = ParseType(Tokens, Next + 1)
                        Count = Type.ArraySuffix[1:-1] if Type.ArraySuffix else "?"
                        Output.append(f"ArrayConstruct<{Element.Slang}, {Count}>")
                    else:
                        Output.append(Type.Slang)
                    Index = Scan + 1
                    continue
                if Text in VectorShort:
                    Output.append(VectorShort[Text])
                    Index += 1
                    continue
                if Text in MatrixShort:
                    Output.append(MatrixShort[Text])
                    Index += 1
                    continue
                if Text in ScalarTypes and NextText == "(":
                    Output.append(BuiltinRenames.get(Text, ScalarTypes[Text]))
                    Index += 1
                    continue
                if Text in ScalarTypes:
                    Output.append(ScalarTypes[Text])
                    Index += 1
                    continue
                if NextText == "(" and Text.startswith("texture") and Text not in ("textureDimensions", "textureNumLayers"):
                    Index, Translated = self.TranslateTextureCall(Tokens, Index)
                    Output.append(Translated)
                    continue
                if NextText == "(" and Text.startswith("atomic"):
                    Index, Translated = self.TranslateAtomicCall(Tokens, Index)
                    Output.append(Translated)
                    continue
                if NextText == "(" and Text in BuiltinRenames:
                    Output.append(BuiltinRenames[Text])
                    Index += 1
                    continue
                Renamed = IdentifierRenames.get(Text, Text)
                Output.append(Renamed)
                Index += 1
                continue
            if Text == "&":
                # 📝 Address-of: unary when it follows an operator, '(' or ',' (or starts the expression).
                Previous = Significant(Tokens, Index, -1)
                if Previous < 0 or Tokens[Previous].Text in ("(", ",", "=", "return") or Tokens[Previous].Kind == "op" and Tokens[Previous].Text not in (")", "]"):
                    Index += 1
                    continue
            if Text == "*":
                Previous = Significant(Tokens, Index, -1)
                if Previous < 0 or (Tokens[Previous].Kind == "op" and Tokens[Previous].Text not in (")", "]")):
                    # 📝 Dereference.
                    Index += 1
                    continue
            Output.append(Text)
            Index += 1
        Joined = "".join(Output)
        return self.RewriteMatrixProducts(Joined)

    def TranslateNumber(self, Text):
        if re.fullmatch(r"0[xX][0-9a-fA-F]+i", Text):
            return Text[:-1]
        if re.fullmatch(r"\d+i", Text):
            return Text[:-1]
        if re.fullmatch(r"\d+f", Text):
            return Text[:-1] + ".0"
        if re.fullmatch(r".*h", Text) and not Text.startswith("0x"):
            return Text[:-1]
        return Text

    def CallArguments(self, Tokens, Index):
        """Index at a call's name; returns (list of argument token lists, index after ')')."""
        Open = Significant(Tokens, Index)
        Depth = 0
        Scan = Open
        while True:
            if Tokens[Scan].Text in "([":
                Depth += 1
            elif Tokens[Scan].Text in ")]":
                Depth -= 1
                if Depth == 0:
                    break
            Scan += 1
        return self.SplitTopLevel(Tokens[Open + 1:Scan], ","), Scan + 1

    def ResourceDimension(self, Name):
        if Name in self.LocalResources:
            return self.LocalResources[Name]
        Entry = self.Globals["types"].get(Name)
        if not Entry:
            return None, None
        return Entry[2], Entry[1]

    def TranslateTextureCall(self, Tokens, Index):
        Name = Tokens[Index].Text
        Arguments, After = self.CallArguments(Tokens, Index)
        Parts = [self.TranslateExpression(Argument).strip() for Argument in Arguments]
        TextureName = Join(Arguments[0]).strip()
        Dimension, Category = self.ResourceDimension(TextureName)
        Arrayed = Dimension in ("2d_array", "cube_array")
        if Name == "textureSampleLevel":
            if Arrayed:
                return After, f"{Parts[0]}.SampleLevel({Parts[1]}, float3({Parts[2]}, float({Parts[3]})), {Parts[4]})"
            return After, f"{Parts[0]}.SampleLevel({Parts[1]}, {Parts[2]}, {Parts[3]})"
        if Name == "textureSample":
            if Arrayed:
                return After, f"{Parts[0]}.Sample({Parts[1]}, float3({Parts[2]}, float({Parts[3]})))"
            return After, f"{Parts[0]}.Sample({Parts[1]}, {Parts[2]})"
        if Name == "textureSampleCompareLevel":
            if Arrayed:
                return After, f"{Parts[0]}.SampleCmpLevelZero({Parts[1]}, float3({Parts[2]}, float({Parts[3]})), {Parts[4]})"
            return After, f"{Parts[0]}.SampleCmpLevelZero({Parts[1]}, {Parts[2]}, {Parts[3]})"
        if Name == "textureSampleCompare":
            if Arrayed:
                return After, f"{Parts[0]}.SampleCmp({Parts[1]}, float3({Parts[2]}, float({Parts[3]})), {Parts[4]})"
            return After, f"{Parts[0]}.SampleCmp({Parts[1]}, {Parts[2]}, {Parts[3]})"
        if Name == "textureSampleGrad":
            return After, f"{Parts[0]}.SampleGrad({Parts[1]}, {Parts[2]}, {Parts[3]}, {Parts[4]})"
        if Name == "textureLoad":
            if Category == "storage_texture":
                return After, f"{Parts[0]}[{Parts[1]}]"
            # 📝 Dawn's robustness transform clamps textureLoad coordinates, layer and level; LoadTexel does the same.
            if Arrayed:
                return After, f"LoadTexel({Parts[0]}, {Parts[1]}, {Parts[2]}, {Parts[3]})"
            if Dimension is None:
                self.Report(self.File, 0, f"textureLoad on unknown texture {TextureName}")
            return After, f"LoadTexel({Parts[0]}, {Parts[1]}, {Parts[2]})"
        if Name == "textureGather":
            Component = Parts[0]
            self.Report(self.File, 0, "textureGather needs manual review")
            return After, f"{Parts[1]}.GatherRed({Parts[2]}, {Parts[3]}) /*component {Component}*/"
        self.Report(self.File, 0, f"unhandled texture builtin {Name}")
        return After, f"{Name}({', '.join(Parts)})"

    def TranslateAtomicCall(self, Tokens, Index):
        Name = Tokens[Index].Text
        Arguments, After = self.CallArguments(Tokens, Index)
        Parts = [self.TranslateExpression(Argument).strip() for Argument in Arguments]
        if Name == "atomicLoad":
            return After, f"{Parts[0]}"
        self.Report(self.File, 0, f"atomic with result needs manual translation: {Name}({', '.join(Parts)})")
        return After, f"/*{Name}*/AtomicResult({', '.join(Parts)})"

    # 📝 WGSL multiplies matrices with '*'; Slang needs mul(). With row-major storage of the WGSL columns,
    #    WGSL  M * v  ≡  Slang  mul(v, M)   and   WGSL  A * B  ≡  Slang  mul(B, A).
    def RewriteMatrixProducts(self, Text):
        Names = set(IdentifierRenames.get(Name, Name) for Name in self.Globals["matrices"])
        Names |= set(self.Globals["local_matrices"])
        Functions = set(IdentifierRenames.get(Name, Name) for Name in self.Globals["matrix_functions"])
        Changed = True
        Guard = 0
        while Changed and Guard < 64:
            Changed = False
            Guard += 1
            for Match in re.finditer(r"\*", Text):
                Position = Match.start()
                if Text[Position - 1:Position] in ("=", "/") or Text[Position + 1:Position + 2] == "=":
                    continue
                Left = self.PostfixBefore(Text, Position)
                if Left is None:
                    continue
                LeftStart, LeftText = Left
                Root = self.MatrixRoot(LeftText, Names, Functions)
                if not Root:
                    continue
                Right = self.PostfixAfter(Text, Position + 1)
                if Right is None:
                    continue
                RightEnd, RightText = Right
                Text = Text[:LeftStart] + f"mul({RightText.strip()}, {LeftText.strip()})" + Text[RightEnd:]
                Changed = True
                break
        return Text

    def MatrixRoot(self, Expression, Names, Functions):
        Expression = Expression.strip()
        if Expression.startswith("mul(") or Expression.startswith("transpose("):
            return True
        Call = re.match(r"^([A-Za-z_]\w*)\(", Expression)
        if Call and (Call.group(1) in Functions or re.fullmatch(r"float[234]x[234]", Call.group(1))):
            return True
        Tail = re.sub(r"\[[^\[\]]*\]$", "", Expression)
        Tail = re.sub(r"\[[^\[\]]*\]$", "", Tail)
        Last = re.split(r"\.", Tail)[-1]
        return Last in Names

    def PostfixBefore(self, Text, Position):
        """Finds the postfix expression ending right before Text[Position] (skipping spaces)."""
        Cursor = Position - 1
        while Cursor >= 0 and Text[Cursor] == " ":
            Cursor -= 1
        End = Cursor + 1
        if Cursor < 0:
            return None
        while Cursor >= 0:
            Character = Text[Cursor]
            if Character in ")]":
                Depth = 0
                while Cursor >= 0:
                    if Text[Cursor] in ")]":
                        Depth += 1
                    elif Text[Cursor] in "([":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Cursor -= 1
                Cursor -= 1
                continue
            if Character.isalnum() or Character in "_." or ord(Character) > 127:
                Cursor -= 1
                continue
            break
        Start = Cursor + 1
        if Start >= End:
            return None
        return Start, Text[Start:End]

    def PostfixAfter(self, Text, Position):
        Cursor = Position
        while Cursor < len(Text) and Text[Cursor] == " ":
            Cursor += 1
        Start = Cursor
        if Cursor < len(Text) and Text[Cursor] == "-":
            Cursor += 1
        while Cursor < len(Text):
            Character = Text[Cursor]
            if Character in "([":
                Depth = 0
                while Cursor < len(Text):
                    if Text[Cursor] in "([":
                        Depth += 1
                    elif Text[Cursor] in ")]":
                        Depth -= 1
                        if Depth == 0:
                            break
                    Cursor += 1
                Cursor += 1
                continue
            if Character.isalnum() or Character in "_." or ord(Character) > 127:
                Cursor += 1
                continue
            break
        if Cursor == Start:
            return None
        return Cursor, Text[Start:Cursor]


def Token_(Text):
    return Token("op", Text)


#------------------------------------------------------------------------------------------------------------------------
#                                                   MAIN
#------------------------------------------------------------------------------------------------------------------------

def Main():
    SourceFolder = pathlib.Path(sys.argv[1])
    TargetFolder = pathlib.Path(sys.argv[2])
    Files = sys.argv[3:]
    TargetFolder.mkdir(parents=True, exist_ok=True)
    Globals = {"types": {}, "matrices": set(), "local_matrices": set(), "matrix_functions": set(), "structs": set(),
               "single_storage": set(), "entries": [], "stems": [pathlib.Path(File).stem for File in Files]}
    Engine = Translator(Globals)
    Sources = {}
    for File in Files:
        Sources[File] = (SourceFolder / File).read_text(encoding="utf-8")
        Engine.CollectSymbols(File, Tokenize(Sources[File]))
    for File in Files:
        Engine.Locals = {}
        Translated = Engine.Translate(File, Sources[File])
        # 📝 Single-element storage spans are read through element 0.
        for Name in Globals["single_storage"]:
            Translated = re.sub(rf"(?<![\w.]){re.escape(Name)}\.(?=[A-Za-z])", f"{Name}[0].", Translated)
        Stem = FileRenames.get(pathlib.Path(File).stem, pathlib.Path(File).stem)
        HasEntry = any(Entry[0] == File for Entry in Globals["entries"])
        Lines = Translated.split("\n")
        # 📝 '#pragma once' after the banner so a textual include expands once, exactly like the WGSL assembler.
        Insert = 0
        for LineIndex, Line in enumerate(Lines[:8]):
            if Line.startswith("// 🧩"):
                Insert = LineIndex + 1
                break
        Lines.insert(Insert, "\n#pragma once\n#include \"IntrinsicParity.slang\"" if not HasEntry else "\n#pragma once\n#include \"IntrinsicParity.slang\"\n#include \"HostConstants.slang\"")
        (TargetFolder / f"{Stem}.slang").write_text("\n".join(Lines), encoding="utf-8")
    Report = {"entries": Globals["entries"], "reports": Engine.Reports, "single_storage": sorted(Globals["single_storage"]),
              "matrices": sorted(Globals["matrices"])}
    print(json.dumps(Report, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    Main()
