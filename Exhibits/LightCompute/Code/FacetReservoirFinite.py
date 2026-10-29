# 🧩 Exhaust RGB importance-resampling support across backface/frontface receiver changes and null proposals.
from itertools import product
import math


def ValidateFacetSupport():
    Points=((2.,0.,0.),(0.,2.,1.),(-2.,0.,-1.))
    Normals=((0.,0.,1.),(1.,0.,0.),(0.,1.,0.))
    Receivers=((0.,0.,4.),(0.,0.,-3.),(4.,-2.,1.),(-3.,3.,2.))
    RGB=((1.,.1,.2),(.1,1.,.2),(.1,.2,1.))
    Q=(.2,.3,.1,.4)
    Geometry=[]
    for Receiver in Receivers:
        Row=[]
        for P,N in zip(Points,Normals):
            Delta=[A-B for A,B in zip(Receiver,P)]
            Squared=sum(X*X for X in Delta)
            Row.append(max(0.,sum(A*B for A,B in zip(N,Delta))/math.sqrt(Squared))/Squared)
        Geometry.append(Row)
    Maximum=0.;Failures=[0,0,0];Checks=0;Rows=[]
    for Birth,Current in product(range(4),repeat=2):
        H=[1/64+G for G in Geometry[Birth]]
        F=[[C*Geometry[Current][I] for C in RGB[I]] for I in range(3)]
        Correct=[sum(F[I][C] for I in range(3)) for C in range(3)]
        Estimate=[0.,0.,0.];Wrong=[[0.,0.,0.] for Error in range(3)]
        for Draws in product(range(4),repeat=2):
            Probability=math.prod(Q[I] for I in Draws)
            W=[H[I]/Q[I] if I<3 else 0 for I in Draws];S=sum(W)
            Conditional=[0.,0.,0.];Second=[0.,0.,0.]
            for I,Weight in zip(Draws,W):
                if not Weight:continue
                for C in range(3):
                    Contribution=F[I][C]*S/(2*H[I])
                    Conditional[C]+=Weight/S*Contribution
                    Second[C]+=Weight/S*Contribution*Contribution
            for C in range(3):
                Dense=sum(F[I][C]/Q[I] if I<3 else 0 for I in Draws)/2
                assert abs(Conditional[C]-Dense)<1e-12
                assert Second[C]+1e-12>=Conditional[C]*Conditional[C]
                Estimate[C]+=Probability*Conditional[C];Checks+=1
            for Error in range(3):
                Scores=Geometry[Birth] if Error==0 else H
                Weights=[Scores[I]/Q[I] if I<3 else 0 for I in Draws];Total=sum(Weights)
                for I,Weight in zip(Draws,Weights):
                    if not Weight:continue
                    Denominator=(1/64+Geometry[Current][I]) if Error==2 else Scores[I]
                    Divisor=sum(I<3 for I in Draws) if Error==1 else 2
                    for C in range(3):Wrong[Error][C]+=Probability*Weight/Total*F[I][C]*Total/(Divisor*Denominator)
        Maximum=max(Maximum,max(abs(A-B) for A,B in zip(Estimate,Correct)))
        for Error in range(3):Failures[Error]+=max(abs(A-B) for A,B in zip(Wrong[Error],Correct))>1e-8
        Rows.append((Birth,Current,Correct,Estimate,Wrong))
    assert Maximum<1e-12 and all(N>0 for N in Failures)
    assert any(G==0 for Row in Geometry for G in Row)
    return Checks,Maximum,Failures,Rows


if __name__=='__main__':
    Checks,Maximum,Failures,Rows=ValidateFacetSupport()
    print(f'PASS RGB support:{Checks} channel/configuration checks;error={Maximum:.12g};negative_failures={Failures}')
