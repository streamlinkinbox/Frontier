"""Render numerical holdout results; these charts are not camera or editor captures."""
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

if len(sys.argv) != 3:
    raise SystemExit('Usage: ReplayTemporalFreshHistoryImages.py MEASURED_LOG OUTPUT_PNG')
Lines = Path(sys.argv[1]).read_text().splitlines()
Results, Comparisons, Fresh = {}, {}, {}
for Line in Lines:
    if Line.startswith(('REPLAY_TEMPORAL_RESULT ', 'REPLAY_TEMPORAL_COMPARE ', 'REPLAY_TEMPORAL_FRESH2_COMPARE ')):
        Row = dict(Field.split('=', 1) for Field in Line.split()[1:])
        if Line.startswith('REPLAY_TEMPORAL_RESULT '):
            Key = tuple(Row[Name] for Name in ('arm', 'scenario', 'group', 'mode', 'floor'))
            Results[Key] = Row
        elif Line.startswith('REPLAY_TEMPORAL_COMPARE ') and Row['left'] == 'baseline:temporal1':
            Key = tuple(Row[Name] for Name in ('scenario', 'group', 'floor'))
            Comparisons[Key] = Row
        elif Line.startswith('REPLAY_TEMPORAL_FRESH2_COMPARE '):
            Key = tuple(Row[Name] for Name in ('scenario', 'group', 'floor'))
            Fresh[Key] = Row
if len(Results) != 160 or len(Comparisons) != 16 or len(Fresh) != 16:
    raise SystemExit('Cannot draw incomplete results')

Width, Height = 1600, 1460
Canvas = Image.new('RGB', (Width, Height), '#101b28')
Draw = ImageDraw.Draw(Canvas)
FontPath = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
BoldPath = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
Title = ImageFont.truetype(BoldPath, 30)
Heading = ImageFont.truetype(BoldPath, 22)
Body = ImageFont.truetype(FontPath, 16)
Small = ImageFont.truetype(FontPath, 14)
Tiny = ImageFont.truetype(FontPath, 13)
Text, Muted = '#e5eef8', '#aabbd0'
OriginalColour, FreshColour = '#95aec4', '#87a5cc'
Colours = {'win': '#68d7b6', 'regression': '#ff9c98', 'inconclusive': '#f7d189', 'invalid': '#c7a8ef'}
Draw.text((40, 22), 'Temporal history: original vs raw-seed holdout', font=Title, fill=Text)
Draw.text((40, 68), '48 paired cohorts x 1,024 supplied receivers | temporal1 | both target floors',
          font=Body, fill=Muted)
Draw.text((40, 95), 'Each floor pair has its own RMSE scale. Lower is better; purple means '
          'a reference-mean gate failed.', font=Small, fill=Muted)

Panels = (('motion_mixed', 'metal', 'Moving / mixed metal', 40, 135),
          ('stationary_mixed', 'metal', 'Stationary / mixed metal', 815, 135),
          ('motion_visibility', 'far', 'Moving / farther receiver', 40, 705),
          ('stationary_visibility', 'far', 'Stationary / farther receiver', 815, 705))
for Scenario, Group, Caption, X, Y in Panels:
    W, H = 745, 540
    Draw.rounded_rectangle((X, Y, X + W, Y + H), radius=12, fill='#192b3a', outline='#4e6880', width=2)
    Draw.text((X + 20, Y + 17), Caption, font=Heading, fill=Text)
    Draw.text((X + 20, Y + 49), 'Original selected history vs test-only raw history', font=Small, fill=Muted)
    for Index, Floor in enumerate(('default', 'lower')):
        Row = Comparisons[(Scenario, Group, Floor)]
        Original = Results[('baseline', Scenario, Group, 'temporal1', Floor)]
        Raw = Results[('raw', Scenario, Group, 'temporal1', Floor)]
        Y0 = Y + 85 + 172 * Index
        Draw.text((X + 20, Y0), 'Floor ' + ('0.01 (default)' if Floor == 'default' else '0.0001 (lower)'),
                  font=Body, fill=Text)
        V0, V1 = float(Row['left_rmse']), float(Row['right_rmse'])
        Scale = 380 / max(V0, V1, 1e-12)
        BarX = X + 165
        Draw.rounded_rectangle((BarX, Y0 + 31, BarX + max(2, int(V0 * Scale)), Y0 + 53),
                               radius=4, fill=OriginalColour)
        Outcome = Row['gate'] if Row['valid_mean_controls'] == '1' else 'invalid'
        Draw.rounded_rectangle((BarX, Y0 + 64, BarX + max(2, int(V1 * Scale)), Y0 + 86),
                               radius=4, fill=Colours[Outcome])
        Draw.text((X + 20, Y0 + 32), 'Original', font=Small, fill=Muted)
        Draw.text((X + 20, Y0 + 65), 'Raw seed', font=Small, fill=Muted)
        Draw.text((X + 562, Y0 + 31), f'{V0:.5f}', font=Small, fill=Text)
        Draw.text((X + 562, Y0 + 64), f'{V1:.5f}', font=Small, fill=Text)
        Gate = Row['gate'] if Outcome != 'invalid' else 'invalid mean gate'
        Draw.text((X + 20, Y0 + 99), f'Paired MSE: {Gate}', font=Small, fill=Colours[Outcome])
        Draw.text((X + 20, Y0 + 123), f"Means: {float(Original['mean']):.5f} / {float(Raw['mean']):.5f}  "
                  f"| reference {float(Raw['reference']):.5f}", font=Small, fill=Muted)
    Work = Fresh[(Scenario, Group, 'lower')]
    Outcome = Work['gate'] if Work['valid_mean_controls'] == '1' else 'invalid mean'
    Draw.line((X + 20, Y + 435, X + W - 20, Y + 435), fill='#4e6880', width=1)
    Draw.text((X + 20, Y + 446), f"Lower floor vs Fresh2: {float(Work['left_rmse']):.5f} -> "
              f"{float(Work['right_rmse']):.5f} RMSE ({Outcome})", font=Small, fill=Text)
    Draw.text((X + 20, Y + 470), f"Final rays {float(Work['left_final_rays']):,.0f} -> "
              f"{float(Work['right_final_rays']):,.0f}; warm rays 0 -> {float(Work['right_warm_rays']):,.0f}",
              font=Small, fill=Muted)
    Draw.text((X + 20, Y + 495), 'Final work is similar in proposal count, not necessarily in ray count.',
              font=Tiny, fill=Muted)

Failed = sum(Row['mean_gate'] != '1' for Row in Results.values())
FailedRaw = sum(Key[0] == 'raw' and Row['mean_gate'] != '1' for Key, Row in Results.items())
Draw.text((40, 1270), 'Gray: original   Green: paired win   Red: paired regression   '
          'Amber: inconclusive   Purple: failed mean', font=Body, fill=Text)
Draw.text((40, 1300), f'Independent reference gates: {Failed} / 160 failed; raw-history failures: {FailedRaw}. '
          'See report for all groups.', font=Body, fill=Text)
Draw.text((40, 1333), 'One-bounce synthetic surfaces, static blocker, CPU Vulkan. Warm-up and temporal caps differ '
          'between moving and stationary.', font=Small, fill=Muted)
Draw.text((40, 1360), 'Operation counts are not elapsed GPU time. Cohort error bars can miss rare tails; '
          'multiple comparisons are not adjusted.', font=Small, fill=Muted)
Draw.text((40, 1387), 'This is a data chart, not a rendered scene, editor capture, real camera, '
          'physical-GPU or Windows proof.', font=Small, fill=Muted)
Draw.text((40, 1413), 'No production shader or history ABI was changed by this test-only alternative.',
          font=Small, fill=Muted)
Output = Path(sys.argv[2])
Output.parent.mkdir(parents=True, exist_ok=True)
Canvas.save(Output)
print('WROTE ' + str(Output))
