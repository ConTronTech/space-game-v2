# Models

## Directory Structure
```
models/
  ship/           -- Player ship (exterior + interior cockpit)
    ship.obj
    low-polly-ship.mtl
  stations/       -- Station models (drop OBJ + MTL here)
    orbital/      -- Orbital stations (floating in space)
    planetary/    -- Planetary stations (surface landing pads)
```

## Adding New Models
1. Export from Blender as Wavefront OBJ
2. Check "Triangulate Faces" in export options (or loader handles quads)
3. Set origin to desired anchor point (camera eye for cockpit, center for stations)
4. Materials: MTL file with Kd colors auto-loaded
5. Material named "CANOPY" gets 30% alpha automatically

## Current Materials (Ship)
- BODY -- Dark purple hull
- CANOPY -- Blue transparent glass (30% alpha)
- THRUST-BODY -- Grey engine housing
- THRUST-JET -- Orange thruster glow
