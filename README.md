# FleasionToFishstrap

Converts a Fleasion-style `replacement_rules` JSON file into a Fishstrap-style local mod folder layout.

## Important

This converter only converts replacement information that can be represented as local client files.

A Fleasion rule using:

```json
"mode": "id",
"with_id": 123456789
```

is an **asset ID → asset ID redirect**. Fishstrap's local file mod system does not directly use that format.

The converter can create the expected client path layout, but it **cannot download a Roblox asset ID and turn it into the required client texture/file**. You still need the actual replacement file for a working Fishstrap mod.

## Usage

Run the converter with the JSON file as the first argument:

```bat
FleasionToFishstrap.exe "C:\Path\To\config.json"
```

The output folder is created in the **same directory as the input JSON**.

For example:

```text
C:\MyMods\sky.json
```

becomes:

```text
C:\MyMods\sky_FishstrapMod\
```

## Example output

For a six-face skybox, the generated structure is:

```text
sky_FishstrapMod\
└── PlatformContent\
    └── pc\
        └── textures\
            └── sky\
                ├── sky512_bk.tex
                ├── sky512_dn.tex
                ├── sky512_ft.tex
                ├── sky512_lf.tex
                ├── sky512_rt.tex
                └── sky512_up.tex
```

## Build on Windows

### Visual Studio Developer Command Prompt

```bat
cl /O2 /std:c++17 FleasionToFishstrap.cpp /Fe:FleasionToFishstrap.exe
```

### MinGW-w64

```bat
g++ -O2 -std=c++17 FleasionToFishstrap.cpp -o FleasionToFishstrap.exe
```

## Input format

The input JSON must contain a top-level `replacement_rules` array.

Example:

```json
{
  "replacement_rules": [
    {
      "name": "Skybox BK",
      "replace_ids": [14147881792],
      "mode": "id",
      "enabled": true,
      "with_id": 93590148140827
    }
  ]
}
```

## Notes

- The program does **not** install certificates.
- The program does **not** intercept HTTPS traffic.
- The program does **not** modify Roblox network traffic.
- The converter is intended to generate a local Fishstrap mod layout.
- Roblox updates can change client files and paths, so generated mods may need to be updated.
