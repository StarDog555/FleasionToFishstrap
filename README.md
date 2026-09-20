# FleasionToFishstrap V3

A small Windows C++ converter for Fleasion `replacement_rules` configs.

V3 is intentionally **local-only** for `mode: "id"`: it does not ask for a Roblox login, cookie, or Asset Delivery authentication. Instead, it expects you to place the downloaded replacement assets in an `assets` folder beside the JSON file.

## What it does

For RIVALS skybox rules such as:

```json
{
  "name": "Skybox BK",
  "replace_ids": [14147881792],
  "mode": "id",
  "enabled": true,
  "with_id": 93590148140827
}
```

V3 looks for an asset named:

```text
93590148140827
```

or:

```text
93590148140827.png
93590148140827.dds
```

inside:

```text
assets\
```

The file can have **no extension**. V3 detects PNG/DDS from the file bytes.

For PNG sky faces, V3 converts the image to BC1/DXT1 DDS data and writes it with the Roblox sky filename:

```text
PlatformContent\pc\textures\sky\sky512_bk.tex
PlatformContent\pc\textures\sky\sky512_dn.tex
PlatformContent\pc\textures\sky\sky512_ft.tex
PlatformContent\pc\textures\sky\sky512_lf.tex
PlatformContent\pc\textures\sky\sky512_rt.tex
PlatformContent\pc\textures\sky\sky512_up.tex
```

Fishstrap's own logs show these `sky512_*.tex` files being handled by its file-modification system. 

## Downloading the replacement images

The `with_id` values in a Fleasion config are Roblox asset IDs. For the sky used while developing this tool, the replacement images were downloaded manually with the older Asset Delivery URL:

```text
https://assetdelivery.roblox.com/v1/asset?id=ASSET_ID
```

For example:

```text
https://assetdelivery.roblox.com/v1/asset?id=93590148140827
```

This endpoint worked for the sky assets used during testing. It is an older endpoint, so it may return `401 Unauthorized` for some assets or in some situations. V3 itself does **not** authenticate to Roblox or download assets automatically.

After downloading an image, place it in the `assets` folder using its `with_id` as the filename. `.png` is fine, and the extension can also be omitted:

```text
93590148140827.png
102453743082771.png
118903490011578.png
126562643473125.png
77446329776959.png
120495452755285.png
```

## Folder layout

```text
MySky.json
assets\
    93590148140827.png
    102453743082771.png
    118903490011578.png
    126562643473125.png
    77446329776959.png
    120495452755285.png
```

V3 also accepts the six files without extensions. It detects PNG/DDS from the file bytes rather than the filename.

## Run

```bat
FleasionToFishstrapV3.exe "C:\MyMods\MySky.json"
```

The output is created beside the JSON:

```text
MySky_FishstrapModV3\
```

with a conversion report and `_sources` folder.

## Install the generated mod

Copy the **contents** of the generated `MySky_FishstrapModV3` folder into:

```text
%LocalAppData%\Fishstrap\Modifications\
```

Then launch Roblox through Fishstrap.

## Build with MinGW

```bat
g++ -O2 -std=c++17 FleasionToFishstrapV3.cpp -o FleasionToFishstrapV3.exe -lole32 -luuid -lwindowscodecs
```

## Build with MSVC

```bat
cl /O2 /std:c++17 FleasionToFishstrapV3.cpp /Fe:FleasionToFishstrapV3.exe
```

## Notes

`mode: "id"` requires `with_id` and a local replacement asset named with that ID.

`mode: "local"` is supported when `local_path` points to a file.

`mode: "cdn"` is intentionally not fetched by V3. Download the file yourself into `assets` first.

Non-sky rules are skipped unless a future version has a known Fishstrap client path for them. V3 focuses on the six `Skybox BK/DN/FT/LF/RT/UP` files so it does not guess incorrect paths.
