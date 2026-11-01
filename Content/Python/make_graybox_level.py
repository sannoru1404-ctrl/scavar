"""Creates the pilot graybox level /Game/Maps/Radichi_Graybox from code.

Run inside the editor (Python console or command line):
  UnrealEditor-Cmd SCAVAR.uproject -ExecCmds="py Content/Python/make_graybox_level.py; Quit" -unattended

Layout (Unreal units are centimeters): a 2 km x 2 km flat plain centred on the origin, a village of simple buildings with loot
points, a farm and a granary (POI_010 / POI_011 positions relative to the Radichi square x 1-3, y 1-3 km), four player starts
on the edges (>= 300 m apart) and a PlayerStart cluster for quick tests. Nothing here is final art: every building is a scaled cube.
"""
import math
import random
import unreal

LEVEL_PATH = "/Game/Maps/Radichi_Graybox"
SQUARE_CM = 200000.0           # 2 km
ORIGIN_KM = (1.0, 1.0)         # Radichi square corner in the canon coordinate system
CUBE = "/Engine/BasicShapes/Cube.Cube"
PLANE = "/Engine/BasicShapes/Plane.Plane"

# canon coordinates (km) -> world centimeters relative to the centre of the square
def km_to_cm(x_km, y_km):
    return ((x_km - ORIGIN_KM[0] - 1.0) * 100000.0, (y_km - ORIGIN_KM[1] - 1.0) * 100000.0)

actor_lib = unreal.EditorActorSubsystem()
level_lib = unreal.EditorLevelLibrary
rng = random.Random(20011)


def spawn(actor_class, location, rotation=unreal.Rotator(0, 0, 0), label=None):
    actor = level_lib.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation)
    if label:
        actor.set_actor_label(label)
    return actor


def spawn_box(label, location, size_cm):
    mesh = unreal.EditorAssetLibrary.load_asset(CUBE)
    actor = spawn(unreal.StaticMeshActor, location, label=label)
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.set_actor_scale3d(unreal.Vector(size_cm[0] / 100.0, size_cm[1] / 100.0, size_cm[2] / 100.0))
    return actor


def spawn_building(name, centre, size_cm, usage, tier, building_id, points=5):
    spawn_box(name, (centre[0], centre[1], size_cm[2] / 2.0), size_cm)
    loot_class = unreal.load_class(None, "/Script/ScavarGameplay.ScavarLootSpawnPoint")
    for i in range(points):
        angle = 2.0 * math.pi * i / points
        radius = min(size_cm[0], size_cm[1]) * 0.25
        loc = (centre[0] + math.cos(angle) * radius, centre[1] + math.sin(angle) * radius, 60.0)
        point = spawn(loot_class, loc, label=f"{name}_Loot{i}")
        point.set_editor_property("usage_tags", [unreal.Name(u) for u in usage])
        point.set_editor_property("tier", tier)
        point.set_editor_property("building_id", building_id)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        unreal.EditorAssetLibrary.delete_asset(LEVEL_PATH)
    unreal.EditorLevelLibrary.new_level(LEVEL_PATH)

    # ground
    plane = unreal.EditorAssetLibrary.load_asset(PLANE)
    ground = spawn(unreal.StaticMeshActor, (0, 0, 0), label="Ground")
    ground.static_mesh_component.set_static_mesh(plane)
    ground.set_actor_scale3d(unreal.Vector(SQUARE_CM / 100.0, SQUARE_CM / 100.0, 1.0))

    # lighting
    spawn(unreal.DirectionalLight, (0, 0, 1000), unreal.Rotator(0, -45, 30), "Sun")
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="SkyAtmosphere")
    spawn(unreal.SkyLight, (0, 0, 500), label="SkyLight")

    tier = unreal.ScavarRiskTier.R1
    building_id = 1

    # POI_010 farm and POI_011 granary at their canon positions
    farm = km_to_cm(1.50, 2.50)
    granary = km_to_cm(2.60, 2.40)
    spawn_building("POI_010_Farm", farm, (3000, 2400, 450), ["farm", "residential"], tier, building_id, points=8)
    building_id += 1
    spawn_building("POI_011_Granary", granary, (5000, 2800, 600), ["farm", "storage"], tier, building_id, points=10)
    building_id += 1

    # village houses around SET_RADICHI (2.10, 2.00)
    village = km_to_cm(2.10, 2.00)
    for i in range(14):
        angle = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(1500, 9000)
        centre = (village[0] + math.cos(angle) * dist, village[1] + math.sin(angle) * dist)
        size = (rng.uniform(900, 1400), rng.uniform(800, 1200), 320)
        spawn_building(f"House_{i:02d}", centre, size, ["residential", "farm"], tier, building_id, points=rng.randint(3, 7))
        building_id += 1

    # perimeter walls so nobody falls off the plain
    half = SQUARE_CM / 2.0
    for name, loc, size in (("WallN", (0, half, 400), (SQUARE_CM, 100, 800)), ("WallS", (0, -half, 400), (SQUARE_CM, 100, 800)),
                            ("WallE", (half, 0, 400), (100, SQUARE_CM, 800)), ("WallW", (-half, 0, 400), (100, SQUARE_CM, 800))):
        spawn_box(name, loc, size)

    # player starts: four corners ring (>= 300 m between any two) + one test start near the farm
    for i, (x, y) in enumerate(((-80000, -80000), (80000, -80000), (80000, 80000), (-80000, 80000))):
        spawn(unreal.PlayerStart, (x, y, 150), label=f"Spawn_{i}")
    spawn(unreal.PlayerStart, (farm[0] + 2500, farm[1], 150), label="Spawn_TestFarm")

    unreal.EditorLoadingAndSavingUtils.save_current_level()
    unreal.log(f"Graybox level saved to {LEVEL_PATH}. Set it as GameDefaultMap in Project Settings > Maps & Modes.")


main()
