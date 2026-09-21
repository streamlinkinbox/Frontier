// Shared physical spacing calculation for the solver and size selector labels.
// At ordinary aspect ratios, count * spacing^3 (nominal water volume) is constant.
// Extreme narrow footprints constrain spacing so seed particles still fit.
export const particleSpacingFor=(count,[width,depth])=>
  Math.min(.12*Math.cbrt(6144/count*width*depth/17),Math.min(width,depth)/4);
