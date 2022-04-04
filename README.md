# TEK Injector
[![Discord](https://img.shields.io/discord/937821572285206659?style=flat-square&label=Discord&logo=discord&logoColor=white&color=7289DA)](https://discord.gg/47SFqqMBFN)

This repository contains the code for TEK Injector

## What is it?

TEK Injector is a dll that modifies certain Steam API functions after injection into ARK: Survival Evolved game process using it. It is intended to be used by [TEK Launcher](https://github.com/Nuclearistt/TEKLauncher)  
It does the following changes to interaction between Steam API and the game:
- Using Steam App ID 480 (Spacewar) to access Steam interfaces
- Return true for all ownership checks ignoring Steam's real response
- Make server list requests use app ID 346110 (ARK) and add extra filter for it to search only servers that use [TEK Wrapper](https://github.com/Nuclearistt/TEKWrapper)
- Override mods-related functions to load mods only from **{Game root}\Mods** folder and assume all mods there to be subscribed
- Forward all mod subscribe/download requests to TEK Launcher and receive download progress from it

## How does it work?

The lifetime of TEK Injector is the following:
- Injection into suspended process (or not suspended, but as early as possible, before steam_api64.dll is loaded)
- Using Windows NT API notification to wait for **steam_api64.dll** to be loaded
- Replacing *SteamAPI_Init* function in **steam_api64.dll** image with its own via [Detours](https://github.com/microsoft/Detours)
- Using custom init function to replace certain function pointers in Steam API interfaces so they point to TEK Injector's functions

## License

TEK Injector is licensed under the [MIT](LICENSE.TXT) license.
