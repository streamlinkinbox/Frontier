# Frontier

Fluid references, audit, and reproduction instructions: [Fluid Path](Documentation/Plans/Ongoing/FluidPath.md).

```sh
python3 Experimental/FluidPath/PrepareReferences.py
python3 Experimental/FluidPath/ServeReferences.py
```

Open `http://localhost:8000/` on the machine running the server. WebGPU requires a compatible
browser and a secure context; remote access requires HTTPS. The references are not a finished Vulkan port.
