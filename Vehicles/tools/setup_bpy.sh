#!/bin/bash
# Headless bpy setup for a sandbox without root / X11 / OpenGL.
# Installs pip `bpy` into ~/.bpyenv and builds no-op stub .so files for the
# display libraries bpy links against (libGL, libX11, libxkbcommon, ...).
# Usage: bash setup_bpy.sh ; then
#   LD_LIBRARY_PATH=~/.bpystubs ~/.bpyenv/bin/python -c "import bpy"
set -e
ENV=$HOME/.bpyenv; ST=$HOME/.bpystubs
[ -x $ENV/bin/python ] || { python3 -m venv $ENV; $ENV/bin/pip install -q bpy==5.0.1 numpy matplotlib; }
B=$ENV/lib/python3.11/site-packages/bpy
mkdir -p $ST && cd $ST
LIBS="libGL.so.1 libICE.so.6 libSM.so.6 libXfixes.so.3 libXi.so.6 libXrender.so.1 libxkbcommon.so.0 libX11.so.6 libXext.so.6 libXxf86vm.so.1"
# all undefined symbols of bpy minus everything resolvable from system/bundled libs
nm -D --undefined-only $B/__init__.so $B/lib/*.so* 2>/dev/null | awk 'NF>=2{print $NF}' | grep -v ':$' > undef_raw.txt
sed 's/@.*//' undef_raw.txt | sort -u > undef.txt
( ldd $B/__init__.so $B/lib/*.so* 2>/dev/null | awk '/=> \//{print $3}' | grep -v bpystubs; ls $B/lib/*.so* $B/__init__.so ) \
  | sort -u | xargs nm -D --defined-only 2>/dev/null | awk 'NF==3{print $3}' | sed 's/@.*//' | sort -u > defd.txt
{ comm -23 undef.txt defd.txt; grep -E '^(gl|X|_X|xkb_|Ice|_Ice|Sm|_Sm)' undef.txt; } | sort -u \
  | grep -v -E '^(Py|_Py|ZSTD_trace|_ITM|__RML|__TBB|_ZTHN)' > missing.txt
VER=$(grep '@' undef_raw.txt | sed 's/.*@@*//' | sort -u | grep -E '^V_' | head -1)
VSYMS=$(grep "@.*$VER" undef_raw.txt | sed 's/@.*//' | sort -u | sed 's/$/;/' | tr '\n' ' ')
{ echo '#include <stddef.h>'
  while read s; do
    case $s in glXGetProcAddress*) echo "void* $s(const char*n){(void)n;return NULL;}";; *) echo "long $s(){return 0;}";; esac
  done < missing.txt; } > all.c
printf 'BASE { global: *; };\n%s { global: %s } BASE;\n' "$VER" "$VSYMS" > ver.map
for L in $LIBS; do gcc -shared -fPIC -w -Wl,-soname,$L -Wl,--version-script=ver.map -o $L all.c; done
LD_LIBRARY_PATH=$ST $ENV/bin/python -c "import bpy; print('bpy OK', bpy.app.version_string)"
