"""Plot paired, measured moving-receiver comparisons without hiding failed controls."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Source = Root / 'Exhibits/LightCompute/Results/ReplayFreshDonorMotion-paired.txt'
Destination = Root / 'Exhibits/LightCompute/Images/ReplayFreshDonorMotion.png'


def Font(Size, Bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf', Size)
    except OSError:
        return ImageFont.load_default()


def ReadMeasurements():
    Comparisons, MeanResults, Control = {}, {}, None
    for Line in Source.read_text().splitlines():
        if Line.startswith('REPLAY_FRESH_MOTION_COMPARE '):
            Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
            Key = tuple(Fields[Name] for Name in ('scene', 'group', 'mode', 'floor'))
            if Key in Comparisons:
                raise ValueError('Duplicate comparison: ' + str(Key))
            Comparisons[Key] = Fields
        elif Line.startswith('REPLAY_FRESH_MOTION_RESULT '):
            Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
            Key = tuple(Fields[Name] for Name in ('arm', 'scene', 'group', 'mode', 'floor'))
            if Key in MeanResults:
                raise ValueError('Duplicate mean: ' + str(Key))
            MeanResults[Key] = Fields
        elif Line.startswith('REPLAY_FRESH_MOTION_CONTROLS '):
            Control = dict(Field.split('=', 1) for Field in Line.split()[1:])
    if len(Comparisons) != 48 or len(MeanResults) != 96 or not Control:
        raise ValueError('The complete paired holdout and its controls are required')
    return Comparisons, MeanResults, Control


Comparisons, MeanResults, Control = ReadMeasurements()
Canvas = Image.new('RGB', (1600, 1430), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(30, True), Font(20, True), Font(17), Font(14)
White, Muted, Gray = '#edf3fa', '#a9bbcc', '#8da7bd'
Green, Red, Amber, Purple = '#60d1b4', '#f08e8a', '#f3c77b', '#cf9bea'
Panel, Border = '#192735', '#395064'
Draw.text((42, 24), 'Fresh spatial donors: moving-receiver holdout', font=Title, fill=White)
Draw.text((42, 69), '48 paired cohorts x 1,024 supplied receivers | original vs test-only candidate | floor 0.01',
          font=Body, fill=Muted)
Draw.text((42, 99), 'One and four fresh proposals per pixel; each panel uses its own RMSE scale.', font=Small, fill=Muted)


def DrawPanel(Box, Scene, Group, Label, Subtext):
    X0, Y0, X1, Y1 = Box
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Draw.text((X0 + 22, Y0 + 18), Label, font=Heading, fill=White)
    Draw.text((X0 + 22, Y0 + 50), Subtext, font=Small, fill=Muted)
    Rows = [Comparisons[(Scene, Group, Mode, 'default')] for Mode in ('combined1', 'combined4')]
    Maximum = max(float(Row[Name]) for Row in Rows for Name in ('baseline_rmse', 'fresh_rmse')) * 1.1
    for Index, Row in enumerate(Rows):
        Y = Y0 + 90 + Index * 185
        Old, New = float(Row['baseline_rmse']), float(Row['fresh_rmse'])
        Valid = Row['valid_mean_controls'] == '1'
        Verdict = Row['gate'] if Valid else 'mean gate failed: not comparable'
        Colour = Purple if not Valid else {'win': Green, 'regression': Red, 'inconclusive': Amber}[Row['gate']]
        Draw.text((X0 + 22, Y), f'Temporal + disk / {1 if Index == 0 else 4} fresh', font=Body, fill=White)
        Start, Width = X0 + 180, 310
        for Offset, (Number, BarColour) in enumerate(((Old, Gray), (New, Colour))):
            BarY = Y + 33 + Offset * 37
            Draw.rounded_rectangle((Start, BarY, Start + Width * Number / Maximum, BarY + 24),
                                   radius=4, fill=BarColour)
            Draw.text((Start + Width + 12, BarY + 2), f'{Number:.5f}', font=Small, fill=White)
        Draw.text((X0 + 22, Y + 112), f'Paired 3-SE: {Verdict}', font=Small, fill=Colour)
        Draw.text((X0 + 22, Y + 137),
                  f'Final rays {float(Row["baseline_final_rays"]):,.0f} -> {float(Row["fresh_final_rays"]):,.0f}; '
                  f'warm rays {float(Row["baseline_warm_rays"]):,.0f} -> {float(Row["fresh_warm_rays"]):,.0f}',
                  font=Small, fill=Muted)


DrawPanel((40, 135, 780, 645), 'mixed', 'dim', 'Mixed: dim material', 'RMSE scaled by 1,000')
DrawPanel((820, 135, 1560, 645), 'mixed', 'metal', 'Mixed: metal material', 'Unscaled RMSE')
DrawPanel((40, 670, 780, 1180), 'visibility', 'near', 'Same metal: nearer receiver', 'Unscaled RMSE')
DrawPanel((820, 670, 1560, 1180), 'visibility', 'far', 'Same metal: farther receiver', 'Unscaled RMSE')
BaselineGates = sum(Row['mean_gate'] == '1' for Key, Row in MeanResults.items() if Key[0] == 'baseline')
CandidateGates = sum(Row['mean_gate'] == '1' for Key, Row in MeanResults.items() if Key[0] == 'fresh')
Draw.text((42, 1208), 'Gray: original   Green: win   Red: regression   Amber: inconclusive   Purple: failed mean',
          font=Body, fill=White)
Draw.text((42, 1244), f'Independent mean gates: original {BaselineGates}/48, candidate {CandidateGates}/48; '
          f'negative-control hashes {Control["matching_group_image_hashes"]}.', font=Body, fill=White)
Draw.text((42, 1282), 'Direct recorder continues capped history; the completion-gated C++ sequence resets on changed receivers.',
          font=Small, fill=Muted)
Draw.text((42, 1315), 'Synthetic receiver motion, static blocker, one bounce, CPU Vulkan. Counts are not GPU timing.',
          font=Small, fill=Muted)
Draw.text((42, 1348), 'Not an editor capture or real camera test. Finite-sample mean gates can miss rare tails.',
          font=Small, fill=Muted)
Temporal = MeanResults[('baseline', 'mixed', 'metal', 'temporal1', 'lower')]
TemporalCandidate = MeanResults[('fresh', 'mixed', 'metal', 'temporal1', 'lower')]
if Temporal['mean'] != TemporalCandidate['mean'] or Temporal['mean_gate'] != '0' or TemporalCandidate['mean_gate'] != '0':
    raise ValueError('Expected the recorded shared temporal-only failure')
Draw.text((42, 1381), f'Lower-floor temporal-only metal: {float(Temporal["mean"]):.5f} +/- '
          f'{float(Temporal["batch_se"]):.5f} vs reference {float(Temporal["reference"]):.5f} (both arms fail).',
          font=Small, fill=Red)
Destination.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Destination)
print('SAVED', Destination)
