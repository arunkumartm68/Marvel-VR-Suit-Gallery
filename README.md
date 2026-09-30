# Marvel VR Suit Gallery

A mixed-reality app for **Meta Quest 3S** (and Quest 3), built with **Unreal Engine 5.7.4**, that places a
**life-size superhero armor suit inside your real room** through passthrough. You walk around it, inspect it up
close, and grab, move, rotate and resize it with your bare hands.

It is a gallery, not a game: the suit is an artifact standing on your floor. There is no suit-wearing or
body-fitting mechanic.

> Fan project for personal and educational use. Marvel, Iron Man and related names are trademarks of Marvel /
> Disney. This repository is not affiliated with or endorsed by them.

---

## Features

- **Passthrough mixed reality.** Your real room stays visible; the model is composited into it.
- **True 1:1 scale.** The Iron Man Mark 85 suit stands 1.90 m tall, calibrated in real centimetres.
- **Real floor detection.** Uses the Quest room scan (MR Utility Kit) to find the floor, with the Guardian
  boundary floor as a fallback, so the feet stand on the floor instead of floating or sinking.
- **Smart placement.** The model appears about 1.75 m in front of you, facing you, and is moved closer if a
  wall or furniture is in the way.
- **Hand-tracking interaction** (controllers also work):
  - **Grab and move:** pinch on the model (thumb + index) and move your hand. It is released where you let go.
  - **Rotate:** turn your hand while pinching.
  - **Resize:** pinch with both hands and pull apart or push together (0.2x to 3x).
  - Grabbing only works when your hand is on the model, so it never moves by accident.
- **Hand menu to choose models.** The app starts with an empty room. Turn your left palm towards your face to
  open the **MODELS** menu beside your hand, then tap a button with your right index finger.
- **Data-driven model list.** Add a model by adding an entry to `DA_ModelCatalog`; no code changes needed.
  Models are loaded only when chosen, to save memory on the headset.
- **Lighting that follows the model.** Key, fill and rim lights stay attached to the suit, and their brightness
  is compensated when you resize, so it looks the same at any size.
- **Your real hands.** No virtual hand meshes are drawn over your hands.
- **Quest-friendly rendering.** Mobile forward renderer, multiview, no dynamic shadows, fixed foveated rendering.

## Using the app on the headset

| Action | Hand tracking | Controllers |
|---|---|---|
| Open / close the model menu | Turn your left palm towards your face | Left **Menu** button |
| Choose a model | Tap its button with your right index fingertip | Point the right controller at it, pull the trigger |
| Remove the model | Tap **CLEAR** | Point + trigger on **CLEAR** |
| Put the model back where it was placed, at life size | Tap **RESET** | Point + trigger on **RESET**, or press **A** / **X** |
| Grab, move, rotate | Pinch on the model and move your hand | Grip or trigger on the model |
| Resize | Pinch with both hands, pull apart or together | Grip with both controllers |

Tapping the model that is already in the room brings it back in front of you at life size.

Before the first launch, run **Space Setup** on the headset (Settings > Physical Space > Space Setup) so the app
can find your real floor. Allow the **spatial data** permission when the app asks for it.

---

## Requirements

| Tool | Version used |
|---|---|
| Unreal Engine | 5.7.4 (Epic Games Launcher build, with **Android** target platform installed) |
| Meta XR plugin (OculusXR, includes MR Utility Kit) | 1.205, installed in the engine's `Plugins/Marketplace` folder |
| Visual Studio | 2022 with the "Game development with C++" workload |
| Android Studio | with SDK Platform 32+, **NDK 27.2.12479018**, JDK 17 (bundled `jbr`) |
| Headset | Meta Quest 3S or Quest 3, with **Developer Mode** enabled |

## Getting started

1. **Clone**
   ```bash
   git clone https://github.com/arunkumartm68/Marvel-VR-Suit-Gallery.git
   ```
2. **Install the Meta XR plugin** for UE 5.7 (from Fab / the Meta developer site) into
   `<UE_5.7>/Engine/Plugins/Marketplace/MetaXR`.
3. **Open `Marvel.uproject`.** Unreal asks to build the `MRSuitViewer` C++ module; click **Yes**.
   (Or right-click the `.uproject` > *Generate Visual Studio project files* and build `MarvelEditor` in
   Development Editor.)
4. The startup map is `Content/MRSuitViewer/Maps/L_MRSuitViewer`.

## Build and install on the Quest

**From the editor:** Platforms > Android (ASTC) > *Package Project*, then run the generated
`Install_Marvel-arm64.bat` with the headset connected over USB.

**From the command line** (close the editor first, or add `-nocompileeditor`):

```powershell
$env:NDKROOT   = "$env:LOCALAPPDATA\Android\Sdk\ndk\27.2.12479018"
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
& "<UE_5.7>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\Marvel.uproject" `
  -noP4 -platform=Android -cookflavor=ASTC -clientconfig=Development `
  -build -cook -stage -pak -package -archive -archivedirectory="<path>\Builds"
```

The APK and OBB are written to `Builds/Android_ASTC`. Install with the generated batch file, or:

```bash
adb install -r Builds/Android_ASTC/Marvel-arm64.apk
```

then push the OBB with the `win-x64/UnrealAndroidFileTool.exe push` command from the batch file.

> **Important:** if you add or remove a `UPROPERTY` in C++, rebuild the editor (close it first) **before**
> packaging. The cook runs through the editor's copy of the module, and a stale one produces packages the
> headset cannot load ("Bad export index" crash at startup).

---

## Adding a new model

1. Import the mesh into `Content/MRSuitViewer/<YourModel>/` at real-world size in centimetres, with the pivot
   on the floor between the feet. Keep it Quest-friendly (a few hundred thousand triangles at most).
2. Create a **Suit Configuration** data asset (`MRSuitConfiguration`): set the mesh, rotation offset so it faces
   +X, target height, and floor offset if needed.
3. Create a Blueprint child of **`BP_MRSuit`** and set its *Configuration* (add lights here too if you like).
4. Open **`Content/MRSuitViewer/Data/DA_ModelCatalog`** and add an entry: display name, description,
   actor class (your Blueprint) and configuration.

The hand menu shows one button per catalog entry automatically.

## Project structure

```
Source/MRSuitViewer/          C++ module (the framework)
  MRSuitViewer.*               Session: passthrough, room scan, floor detection, model placement, model catalog
  MRViewerPawn.*               Player: head + hands, pinch detection, grab / move / rotate / two-hand resize
  MRSuit.*                     A model standing in the room: mesh, contact shadow, lights, grab behaviour
  MRSuitConfiguration.*        Per-model calibration data asset
  MRModelCatalog.h             List of models offered in the hand menu
  MRWristMenuComponent.*       Hand menu beside the left hand
Content/MRSuitViewer/
  Blueprints/                  BP_MRGameMode, BP_MRPawn, BP_MRSuitViewer, BP_MRSuit, BP_IronMan85
  Data/                        DA_ModelCatalog, DA_SuitConfiguration
  Maps/L_MRSuitViewer          Empty MR level (passthrough shows your room)
  Materials/, Lighting/, Suit/ Materials, studio lighting cubemap, Iron Man Mk 85 mesh
SourceArt/IronManMk85/         Quest-optimised suit (.blend / .fbx) and the script that prepared it
Tools/                         Editor Python scripts that create / configure the content
Config/                        Quest, Meta XR, rendering and input settings
```

Logic lives in C++; the `BP_` Blueprints are thin subclasses that expose the settings.

## Status and known limitations

- The hand menu is new; its size and placement may need tuning on the headset.
- Grabbing uses a box around the model, so near the torso you can grab slightly before touching the armor.
- The Iron Man materials are flat colours (the source model had no textures).
- Development builds only; no Meta Store packaging (signing, entitlement) is set up.

## Credits

- Built with Unreal Engine 5.7, the Meta XR plugin and Meta MR Utility Kit.
- Iron Man Mark 85 model: third-party model from Sketchfab, reduced and prepared for Quest. The original download
  is **not** included in this repository; check the original model's license before reusing it.
