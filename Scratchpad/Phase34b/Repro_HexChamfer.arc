echo Kernel defect reproducer: a single-edge chamfer of a 120-degree prism edge returns genus 1 with a 90-degree wedge volume, no refusal
polygon (0,0) 2 6 --name=Hex
extrude Hex 2 --name=Prism
topology Prism
chamfer Prism 0.4 --edges=6 --name=PrismOne
topology PrismOne
echo expected: genus 0, V14 E21 F9, volume 20.7846 - 2 * 0.5 * 0.4 * 0.4 * sin(120 deg) = 20.6460
