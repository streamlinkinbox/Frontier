"""Exact finite-support control for small positive targets and weight clipping."""
from decimal import Decimal, getcontext

getcontext().prec = 50
One, Half = Decimal(1), Decimal('0.5')
for Floor in (Decimal('.01'), Decimal('.0001')):
    Targets = (Floor, One + Floor)  # Old receiver: one direction is dark; the other is bright.
    Expectation = Clipped = RareProbability = RareContribution = Maximum = Decimal(0)
    for First in range(2):
        for Second in range(2):
            Sum = Targets[First] + Targets[Second]
            for Selected in (First, Second):
                Probability = Decimal('.25') * Targets[Selected] / Sum
                Weight = Sum / (2 * Targets[Selected])
                Expectation += Probability * Weight  # Both directions have current radiance one.
                Clipped += Probability * min(Weight, Decimal(64))
                Maximum = max(Maximum, Weight)
                if First != Second and Selected == 0:
                    RareProbability += Probability
                    RareContribution += Probability * Weight
    assert abs(Expectation - 1) < Decimal('1e-45')
    assert abs(RareContribution - Decimal('.25')) < Decimal('1e-45')
    if Floor == Decimal('.0001'):
        assert Maximum > 5000 and Clipped < Decimal('.76')
    NoneIn24576 = (One - RareProbability) ** 24576
    print(f'REPLAY_TEMPORAL_FINITE floor={Floor} exact_history_mean={Expectation:.12g} '
          f'raw_history_mean=1 clipped_64_history_mean={Clipped:.12g} '
          f'rare_probability={RareProbability:.12g} rare_mean_share={RareContribution:.12g} '
          f'max_normalization={Maximum:.12g} no_rare_in_24576_iid={NoneIn24576:.12g}')
print('PASS ReplayTemporalFreshHistoryFiniteEnumeration')
