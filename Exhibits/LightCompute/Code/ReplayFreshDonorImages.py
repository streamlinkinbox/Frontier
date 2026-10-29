"""Plot actual held-out fresh-neighbor quality, including a measured regression."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Root = Path(__file__).resolve().parents[3]
Folder = Root / 'Exhibits/LightCompute/Results'
Summary = Folder / 'ReplayFreshDonor-paired.txt'
Output = Root / 'Exhibits/LightCompute/Images/ReplayFreshDonorHoldout.png'


def Font(Size, Bold=False):
    try:
        return ImageFont.truetype('DejaVuSans-Bold.ttf' if Bold else 'DejaVuSans.ttf', Size)
    except OSError:
        return ImageFont.load_default()


def Parse():
    Text = Summary.read_text()
    if 'failed_mean_gates=2 candidate_mean_failures=0' not in Text or 'FAIL ReplayFreshDonorPairedSummary' not in Text:
        raise ValueError('Expected the complete holdout, including failed baseline controls')
    Rows = {}
    for Line in Text.splitlines():
        if not Line.startswith('REPLAY_FRESH_COMPARE '):
            continue
        Fields = dict(Field.split('=', 1) for Field in Line.split()[1:])
        Key = tuple(Fields.pop(Name) for Name in ('scene', 'group', 'mode', 'floor'))
        if Key in Rows:
            raise ValueError('Duplicate comparison: ' + str(Key))
        Rows[Key] = Fields
    return Rows


Rows = Parse()
Canvas = Image.new('RGB', (1600, 1040), '#101923')
Draw = ImageDraw.Draw(Canvas)
Title, Heading, Body, Small = Font(30, True), Font(20, True), Font(17), Font(14)
White, Muted, Green, Gray, Red = '#edf3fa', '#a9bbcc', '#60d1b4', '#8da7bd', '#f08e8a'
Panel, Border = '#192735', '#395064'
Draw.text((42, 25), 'Fresh spatial donors: promising holdout, not yet integrated', font=Title, fill=White)
Draw.text((42, 70), '48 paired cohorts x 1,024 supplied receivers | original vs test-only candidate | floor 0.01',
          font=Body, fill=Muted)


def PanelFor(Scene, Box, TitleText, Subtext, Groups):
    X0, Y0, X1, Y1 = Box
    Draw.rounded_rectangle(Box, radius=12, fill=Panel, outline=Border, width=2)
    Draw.text((X0 + 22, Y0 + 18), TitleText, font=Heading, fill=White)
    Draw.text((X0 + 22, Y0 + 52), Subtext, font=Small, fill=Muted)
    Start, Width = X0 + 205, 260
    for Index, (Group, Label) in enumerate(Groups):
        Y = Y0 + 108 + Index * 165
        Row = Rows[(Scene, Group, 'temporal_disk', 'default')]
        if Row['valid_mean_controls'] != '1' or Row['gate'] not in ('win', 'regression'):
            raise ValueError('Expected valid paired gated comparison: ' + str((Scene, Group)))
        Old, New = float(Row['baseline_rmse']), float(Row['fresh_rmse'])
        Maximum = max(Old, New) * 1.12
        Draw.text((X0 + 22, Y + 6), Label, font=Body, fill=White)
        for J, (Value, Colour) in enumerate(((Old, Gray), (New, Green if Row['gate'] == 'win' else Red))):
            BarY = Y + J * 40
            Draw.rounded_rectangle((Start, BarY, Start + Width * Value / Maximum, BarY + 26), radius=4, fill=Colour)
            Draw.text((Start + Width + 10, BarY + 4), f'{Value:.5f}', font=Body, fill=White)
        Change = 100 * (New / Old - 1)
        Draw.text((Start, Y + 88), f'RMSE change {Change:+.2f}% | paired 3-SE {Row["gate"]}', font=Small, fill=White)
        Draw.text((Start, Y + 110), f'Final rays {float(Row["baseline_final_rays"]):,.0f} -> {float(Row["fresh_final_rays"]):,.0f}',
                  font=Small, fill=Muted)
    Sample = Rows[(Scene, Groups[0][0], 'temporal_disk', 'default')]
    Draw.text((X0 + 22, Y1 - 50), f'Three warm frames: {float(Sample["baseline_warm_rays"]):,.0f} -> '
              f'{float(Sample["fresh_warm_rays"]):,.0f} rays', font=Small, fill=Muted)


PanelFor('mixed', (40, 120, 780, 645), 'Mixed materials and visibility',
         'Dim RMSE scaled by 10^3; metal unscaled', (('dim', 'Dim'), ('metal', 'Metal')))
PanelFor('visibility', (820, 120, 1560, 645), 'Same material, unequal visibility',
         'Both groups use the same atlas address', (('near', 'Near square'), ('far', 'Far from square')))
Draw.text((42, 686), 'Gray = original replay    Green = candidate improvement    Red = candidate regression', font=Body, fill=White)
Draw.text((42, 724), 'Candidate: 32/32 means pass. Original: 2/32 lower-floor combined-mode means fail.', font=Body, fill=White)
Draw.text((42, 762), '1,152 fresh-only, temporal-only and spatial-only group-image hashes match between arms.', font=Body, fill=Muted)
Draw.text((42, 800), 'Ray counts exclude warm-up and are not GPU time; triangle tests can increase despite fewer rays.', font=Body, fill=Muted)
Draw.text((42, 838), 'Artificial static fixtures, one bounce, CPU Vulkan. No physical GPU, real camera, motion or production claim.',
          font=Small, fill=Muted)
Draw.text((42, 876), 'Test-only candidate; fixed floor/defaults unchanged. Paired SE and rare tails remain finite-sample evidence.',
          font=Small, fill=Muted)
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('SAVED', Output)
