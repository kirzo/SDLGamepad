<h1 align="center">SDLGamepad</h1>

<p align="center">
  <em>Gamepads for Unreal Engine through SDL3: DualSense, DualShock 4, Switch Pro, Xbox and NVIDIA Shield as standard Gamepad keys, with rumble, light bar and player LEDs.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Unreal%20Engine-5.7-blue?logo=unrealengine" alt="Unreal Engine 5.7" />
  <img src="https://img.shields.io/badge/platform-Win64-lightgrey?logo=windows" alt="Win64" />
  <img src="https://img.shields.io/badge/SDL-3.4.18-1d4f91" alt="SDL 3.4.18" />
  <img src="https://img.shields.io/badge/license-MIT--0-green.svg" alt="MIT No Attribution License" />
  <img src="https://img.shields.io/github/stars/kirzo/SDLGamepad?style=social" alt="GitHub stars" />
</p>

---

## Table of Contents

- [Overview](#overview)
- [Supported Controllers](#supported-controllers)
- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Setup: Avoid Double Input](#setup-avoid-double-input)
- [Controller Mappings](#controller-mappings)
- [Usage](#usage)
- [Notes and Caveats](#notes-and-caveats)
- [Repository Layout](#repository-layout)
- [Related Projects](#related-projects)
- [License](#license)
- [Author](#author)

---

## Overview

**SDLGamepad** is an input device plugin for **Unreal Engine 5** that reads gamepads through **[SDL3](https://github.com/libsdl-org/SDL)** and feeds them into the engine as the standard `Gamepad_*` keys. Enhanced Input, UMG and Slate navigation, CommonUI and force feedback keep working exactly as they do with the built-in XInput device: nothing in your game has to change.

Why SDL: its HIDAPI drivers talk to controllers that Windows' own APIs handle poorly or not at all. The Switch Pro Controller works over USB (SDL performs Nintendo's handshake), PlayStation pads get their light bar and player LEDs, and pads that recent GameInput versions stopped reading, such as the NVIDIA Shield controller, work again.

> **Status:** early version, Windows 64-bit only.

---

## Supported Controllers

Anything SDL recognizes as a gamepad works: the controllers its own drivers handle, plus the thousands covered by the bundled [community mapping database](#controller-mappings). Each controller reports a **hardware device identifier** through `FInputDeviceScope`, so button prompt systems can show the right icons.

| Controller | Hardware device id | Rumble | Light color | Player LED |
|---|---|:---:|:---:|:---:|
| Xbox 360 | `Xbox360` | yes | | |
| Xbox One / Series | `XboxOne` | yes | | |
| DualShock 4 | `DualShock` | yes | yes | |
| DualSense / DualSense Edge | `DualSense` | yes | yes | yes |
| Switch Pro Controller | `SwitchPro` | yes | | yes |
| NVIDIA Shield controller | `ShieldCtrl` | yes | | |
| Anything else | SDL's type name (`ps3`, `gamecube`, `steam`, `standard`...) | if supported | if supported | if supported |

---

## Features

- **Standard keys.** Face buttons, shoulders, stick clicks, Back/Start, d-pad, both sticks and both analog triggers. Face buttons are **positional**: the bottom button is `Gamepad_FaceButton_Bottom` on every pad (A on Xbox, Cross on PlayStation, B on Nintendo).
- **Engine-compatible digital keys.** `Gamepad_LeftTrigger`, `Gamepad_LeftStick_Up` and friends are derived from the axes with XInput's thresholds, and held buttons repeat with the delays from your Input settings, just like the engine's XInput device.
- **Hot plug.** Controllers are registered with the platform input device mapper when connected and released when unplugged, following your project's device mapping policy.
- **Rumble** through the regular force feedback API (Force Feedback Effects, Dynamic Force Feedback, `ClientPlayForceFeedback`...).
- **Light color** through Input Device Properties (`Device Color` properties, or `FInputDeviceLightColorProperty` from C++).
- **Player LEDs** that follow the local player each controller belongs to.
- **Focus aware.** No input reaches the game while another application has focus, and everything held is released when focus is lost.
- **Not loaded in commandlets**, so cooking never starts SDL.

---

## Requirements

- **Unreal Engine 5.7** with a C++-enabled project.
- **Windows 64-bit.** SDL3 3.4.18 (official release binaries) ships with the plugin; there is nothing else to install.

---

## Installation

```bash
cd <YourProject>/Plugins
git clone https://github.com/kirzo/SDLGamepad.git
```

1. Right-click your `.uproject` → **Generate Visual Studio project files**.
2. Build and launch; enable via **Edit → Plugins → SDL Gamepad** if needed.
3. Follow [Setup: Avoid Double Input](#setup-avoid-double-input).

---

## Setup: Avoid Double Input

SDL reads every controller, Xbox pads included. Any other gamepad backend left running will deliver the same presses a second time, so turn the others off in your `.uproject`:

```json
"Plugins": [
	{ "Name": "XInputDevice", "Enabled": false },
	{ "Name": "GameInput", "Enabled": false },
	{ "Name": "GameInputWindows", "Enabled": false }
]
```

`XInputDevice` is enabled by default in every project. The GameInput entries only matter if you had enabled those plugins; if you need GameInput for other device kinds, keep it and set `bProcessGamepad` and `bProcessController` to `False` in its platform settings instead. The same applies to any other gamepad plugin (WinDualShock, Steam Input wrappers and so on).

---

## Controller Mappings

SDL only treats a device as a gamepad when it knows which of its buttons and axes is which. On top of SDL's built-in mappings, the plugin loads a snapshot of **[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)** at startup, from `Resources/SDL_GameControllerDB/gamecontrollerdb.txt` (commit `c1d5289`, October 2026). To update it, replace that file with a newer one from the repository.

Mappings of your own go through SDL's standard environment variables, and both take priority over the bundled database:

- `SDL_GAMECONTROLLERCONFIG`: one or more mapping lines, separated by newlines.
- `SDL_GAMECONTROLLERCONFIG_FILE`: path to a file in the same format as `gamecontrollerdb.txt`.

A mapping line looks like `GUID,name,a:b0,b:b1,...,platform:Windows,`. The easiest way to create one is SDL's `testcontroller` tool or one of the mapping tools listed in the SDL_GameControllerDB README.

A mapping can only rename what the device already sends correctly. A controller whose raw data is broken at the OS level (for example a Switch Pro Controller without the handshake SDL's own driver performs) needs a dedicated SDL driver, not a mapping.

---

## Usage

### Input

Bind `Gamepad_*` keys in your Input Mapping Contexts as usual.

### Rumble

Use any force feedback API. The four UE channels are mixed the XInput way: the left channels drive the low-frequency motor and the right channels the high-frequency one.

### Light color

From Blueprint, activate a **Device Color (Static)** or **Device Color (Curve)** Input Device Property through the Input Device Subsystem. From C++:

```cpp
FInputDeviceLightColorProperty Light(true, FColor::Orange);
FSlateApplication::Get().GetPlatformApplication()->GetInputInterface()->SetDeviceProperty(ControllerId, &Light);
```

### Hardware device ids

To let the engine's **Input Device Subsystem** (and CommonUI) know which controller is in use, add the ids from [Supported Controllers](#supported-controllers) under **Project Settings → Input → Platforms → Platform Settings → Windows → Hardware Devices**, with `SDLGamepad` as the input class name. Code that inspects input events directly can read `FInputDeviceScope::GetCurrent()->HardwareDeviceIdentifier` instead.

---

## Notes and Caveats

- **Polled once per frame**, like the engine's XInput device: a press and release that both happen within a single frame are not seen.
- **Disabling the light** (including a color property that resets after completion) turns it off; SDL has no call to restore the controller's default color.
- **Not yet supported:** gyro, touchpad, adaptive triggers and trigger rumble.
- Only Win64 binaries are bundled.

---

## Repository Layout

```
SDLGamepad/
├── Binaries/ThirdParty/SDL3/Win64/SDL3.dll   # loaded at startup, staged with packaged builds
├── Resources/SDL_GameControllerDB/           # community mapping database and its license
├── Source/
│   ├── SDLGamepad/                           # the input device module
│   └── ThirdParty/SDL3Library/               # SDL3 headers, import library and license
└── SDLGamepad.uplugin
```

The SDL module is called `SDL3Library` on purpose: the engine already ships a module named `SDL3` (headers and Unix-only libraries).

---

## Related Projects

- **[KzUI](https://github.com/kirzo/KzUI)**: UMG framework whose button prompts follow these hardware device ids out of the box.

---

## License

SDLGamepad is released under the **MIT No Attribution License (MIT-0)**. See [LICENSE](LICENSE) for the full text.

Use it, modify it and ship it in anything, commercial or not. No credit required.

SDL3 and SDL_GameControllerDB are distributed under the **zlib License**, which does not require credit in shipped products either; see [Source/ThirdParty/SDL3Library/LICENSE.txt](Source/ThirdParty/SDL3Library/LICENSE.txt) and [Resources/SDL_GameControllerDB/LICENSE](Resources/SDL_GameControllerDB/LICENSE).

---

## Author

Built and maintained by **[Kirzo](https://kirzo.dev/)**.

- 🌐 Website: [kirzo.dev](https://kirzo.dev/)
- 🐙 GitHub: [@kirzo](https://github.com/kirzo)

If SDLGamepad helps your project, consider giving the repository a ⭐. It really helps visibility.