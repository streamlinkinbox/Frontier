# 🧩 Enumerate weighted-reservoir first moments, null normalization and historical support failures.
from itertools import product
import math


def ValidateWeightedPairFinite():
    Count = 0
    for Probabilities in ((.2,.3,.1,.4),(.05,.15,.5,.3)):
        for Target in ((.5,1.5,2.),(2.,.2,.9),(.1,4.,.3)):
            for Integrand in ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.),(3.,.7,.2)):
                Expectation = 0.
                for Draws in product(range(4), repeat=4):
                    Probability = math.prod(Probabilities[I] for I in Draws)
                    Weights = [Target[I]/Probabilities[I] if I<3 else 0. for I in Draws]
                    Total = sum(Weights)
                    Conditional = 0.
                    if Total:
                        for I,W in zip(Draws,Weights):
                            if W:Conditional += W/Total*Integrand[I]*Total/(4*Target[I])
                    Direct = sum(Integrand[I]/Probabilities[I] if I<3 else 0. for I in Draws)/4
                    assert abs(Conditional-Direct)<1e-12
                    Expectation += Probability*Conditional
                    Count += 1
                assert abs(Expectation-sum(Integrand))<1e-12
    # Each deliberately wrong estimator must disagree,not merely fail a syntactic predicate.
    Probabilities,Target,Integrand=(.2,.3,.1,.4),(.5,1.5,2.),(3.,.7,.2)
    Wrong=[0.,0.,0.]
    for Draws in product(range(4),repeat=4):
        Probability=math.prod(Probabilities[I] for I in Draws)
        for Error in range(3):
            H=Target if Error!=2 else (.5,1.5,0.)
            Weights=[H[I]/Probabilities[I] if I<3 else 0. for I in Draws]
            Total=sum(Weights)
            Present=sum(I<3 for I in Draws)
            for I,W in zip(Draws,Weights):
                if not W:continue
                Divisor=Present if Error==0 else 4
                Denominator=Integrand[I] if Error==1 else H[I]
                Wrong[Error]+=Probability*W/Total*Integrand[I]*Total/(Divisor*Denominator)
    assert all(abs(X-sum(Integrand))>.01 for X in Wrong)
    return Count,Wrong


if __name__ == '__main__':
    Count,Wrong=ValidateWeightedPairFinite()
    print(f'PASS finite RIS configurations={Count} maximum_first_moment_error<1e-12 negative_means={Wrong} correct=3.9')
