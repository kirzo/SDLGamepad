// Copyright 2026 kirzo

#include "SDLGamepadDevice.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Misc/ConfigCacheIni.h"

THIRD_PARTY_INCLUDES_START
#include <SDL3/SDL.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY(LogSDLGamepad);

namespace UE::SDLGamepad
{
	static const FName InputClassName(TEXT("SDLGamepad"));

	constexpr int32 NumSDLButtons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;

	// XInput's thresholds, SDL reports on the same 16-bit scale
	constexpr int32 LeftStickDeadZone = 7849;
	constexpr int32 RightStickDeadZone = 8689;
	constexpr int32 TriggerThreshold = 3855;
	constexpr int32 AxisDeadZones[] = { LeftStickDeadZone, LeftStickDeadZone, RightStickDeadZone, RightStickDeadZone, TriggerThreshold, TriggerThreshold };

	// UE resends rumble every frame, so this only stops pads whose game stopped ticking
	constexpr Uint32 RumbleDurationMs = 1000;

	/** SDL buttons by index (SDL lays them out by position, like UE's face buttons), then the digital buttons derived from the axes. */
	static const FGamepadKeyNames::Type* GetButtonKeys()
	{
		static const FGamepadKeyNames::Type Keys[] =
		{
			FGamepadKeyNames::FaceButtonBottom, FGamepadKeyNames::FaceButtonRight, FGamepadKeyNames::FaceButtonLeft, FGamepadKeyNames::FaceButtonTop,
			FGamepadKeyNames::SpecialLeft, NAME_None, FGamepadKeyNames::SpecialRight,
			FGamepadKeyNames::LeftThumb, FGamepadKeyNames::RightThumb, FGamepadKeyNames::LeftShoulder, FGamepadKeyNames::RightShoulder,
			FGamepadKeyNames::DPadUp, FGamepadKeyNames::DPadDown, FGamepadKeyNames::DPadLeft, FGamepadKeyNames::DPadRight,
			FGamepadKeyNames::LeftTriggerThreshold, FGamepadKeyNames::RightTriggerThreshold,
			FGamepadKeyNames::LeftStickUp, FGamepadKeyNames::LeftStickDown, FGamepadKeyNames::LeftStickLeft, FGamepadKeyNames::LeftStickRight,
			FGamepadKeyNames::RightStickUp, FGamepadKeyNames::RightStickDown, FGamepadKeyNames::RightStickLeft, FGamepadKeyNames::RightStickRight
		};
		return Keys;
	}

	/** Indexed by SDL_GamepadAxis. */
	static const FGamepadKeyNames::Type* GetAxisKeys()
	{
		static const FGamepadKeyNames::Type Keys[] =
		{
			FGamepadKeyNames::LeftAnalogX, FGamepadKeyNames::LeftAnalogY, FGamepadKeyNames::RightAnalogX, FGamepadKeyNames::RightAnalogY,
			FGamepadKeyNames::LeftTriggerAnalog, FGamepadKeyNames::RightTriggerAnalog
		};
		return Keys;
	}

	static float NormalizeAxis(int32 Axis, int32 Value)
	{
		if (Axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		{
			return Value / 32767.f;
		}

		const float Normalized = Value / (Value <= 0 ? 32768.f : 32767.f);

		// SDL's Y points down, UE's points up
		return (Axis == SDL_GAMEPAD_AXIS_LEFTY || Axis == SDL_GAMEPAD_AXIS_RIGHTY) ? -Normalized : Normalized;
	}

	static FString GetHardwareId(SDL_Gamepad* Gamepad)
	{
		// The NVIDIA Shield controller reports itself as an Xbox 360 pad
		if (SDL_GetGamepadVendor(Gamepad) == 0x0955)
		{
			return TEXT("ShieldCtrl");
		}

		switch (SDL_GetGamepadType(Gamepad))
		{
		case SDL_GAMEPAD_TYPE_PS4: return TEXT("DualShock");
		case SDL_GAMEPAD_TYPE_PS5: return TEXT("DualSense");
		case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return TEXT("SwitchPro");
		case SDL_GAMEPAD_TYPE_XBOX360: return TEXT("Xbox360");
		case SDL_GAMEPAD_TYPE_XBOXONE: return TEXT("XboxOne");
		default: break;
		}

		const char* TypeName = SDL_GetGamepadStringForType(SDL_GetGamepadType(Gamepad));
		return TypeName ? FString(UTF8_TO_TCHAR(TypeName)) : FString();
	}

	static Uint16 ToMotorSpeed(float Value)
	{
		return static_cast<Uint16>(FMath::Clamp(Value, 0.f, 1.f) * 65535.f);
	}
}

FSDLGamepadDevice::FSDLGamepadDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler, const FString& MappingsFile)
	: MessageHandler(InMessageHandler)
{
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("InitialButtonRepeatDelay"), InitialButtonRepeatDelay, GInputIni);
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("ButtonRepeatDelay"), ButtonRepeatDelay, GInputIni);

	// Focus is gated on the Slate side, and Ctrl+C belongs to UE
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

	// Pads already plugged in arrive as SDL_EVENT_GAMEPAD_ADDED on the first poll
	bInitialized = SDL_Init(SDL_INIT_GAMEPAD);
	if (!bInitialized)
	{
		UE_LOG(LogSDLGamepad, Error, TEXT("SDL_Init failed: %hs"), SDL_GetError());
		return;
	}

	// Pads that gain a mapping here also arrive as SDL_EVENT_GAMEPAD_ADDED
	const int32 NumMappings = SDL_AddGamepadMappingsFromFile(TCHAR_TO_UTF8(*MappingsFile));
	UE_CLOG(NumMappings < 0, LogSDLGamepad, Warning, TEXT("Could not load gamepad mappings from %s: %hs"), *MappingsFile, SDL_GetError());

	// SDL loaded the user's file during init with the same priority as ours, so it goes back on top
	const char* UserMappingsFile = SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE);
	if (UserMappingsFile && *UserMappingsFile)
	{
		SDL_AddGamepadMappingsFromFile(UserMappingsFile);
	}
}

FSDLGamepadDevice::~FSDLGamepadDevice()
{
	for (const FPad& Pad : Pads)
	{
		SDL_CloseGamepad(Pad.Gamepad);
	}

	if (bInitialized)
	{
		SDL_Quit();
	}
}

template<typename FuncType>
void FSDLGamepadDevice::ForEachPadOfController(int32 ControllerId, FuncType Func)
{
	FPlatformUserId UserId = PLATFORMUSERID_NONE;
	FInputDeviceId DeviceId = INPUTDEVICEID_NONE;
	IPlatformInputDeviceMapper::Get().RemapControllerIdToPlatformUserAndDevice(ControllerId, UserId, DeviceId);
	if (!UserId.IsValid())
	{
		return;
	}

	for (FPad& Pad : Pads)
	{
		if (Pad.UserId == UserId)
		{
			Func(Pad);
		}
	}
}

void FSDLGamepadDevice::SendControllerEvents()
{
	using namespace UE::SDLGamepad;

	if (!bInitialized)
	{
		return;
	}

	// Drain everything; buttons and axes are polled below
	SDL_Event Event;
	while (SDL_PollEvent(&Event))
	{
		if (Event.type == SDL_EVENT_GAMEPAD_ADDED)
		{
			OpenPad(Event.gdevice.which);
		}
		else if (Event.type == SDL_EVENT_GAMEPAD_REMOVED)
		{
			ClosePad(Event.gdevice.which);
		}
	}

	// Nothing reaches the game while another app has focus; leaving releases everything held
	const bool bAppActive = !FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive();
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	const double Now = FPlatformTime::Seconds();

	for (FPad& Pad : Pads)
	{
		const FPlatformUserId UserId = DeviceMapper.GetUserForInputDevice(Pad.DeviceId);
		if (UserId != Pad.UserId)
		{
			Pad.UserId = UserId;
			SDL_SetGamepadPlayerIndex(Pad.Gamepad, UserId.IsValid() ? DeviceMapper.GetUserIndexForPlatformUser(UserId) : -1);
		}

		int32 Axes[NumAxes] = {};
		uint32 HeldButtons = 0;
		if (bAppActive)
		{
			for (int32 Axis = 0; Axis < NumAxes; ++Axis)
			{
				Axes[Axis] = SDL_GetGamepadAxis(Pad.Gamepad, static_cast<SDL_GamepadAxis>(Axis));
			}

			for (int32 Button = 0; Button < NumSDLButtons; ++Button)
			{
				HeldButtons |= uint32(SDL_GetGamepadButton(Pad.Gamepad, static_cast<SDL_GamepadButton>(Button))) << Button;
			}

			const int32 LeftX = Axes[SDL_GAMEPAD_AXIS_LEFTX];
			const int32 LeftY = Axes[SDL_GAMEPAD_AXIS_LEFTY];
			const int32 RightX = Axes[SDL_GAMEPAD_AXIS_RIGHTX];
			const int32 RightY = Axes[SDL_GAMEPAD_AXIS_RIGHTY];
			const bool DerivedButtons[] =
			{
				Axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > TriggerThreshold, Axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > TriggerThreshold,
				LeftY < -LeftStickDeadZone, LeftY > LeftStickDeadZone, LeftX < -LeftStickDeadZone, LeftX > LeftStickDeadZone,
				RightY < -RightStickDeadZone, RightY > RightStickDeadZone, RightX < -RightStickDeadZone, RightX > RightStickDeadZone
			};
			static_assert(NumSDLButtons + UE_ARRAY_COUNT(DerivedButtons) == NumButtons);

			int32 Bit = NumSDLButtons;
			for (const bool bHeld : DerivedButtons)
			{
				HeldButtons |= uint32(bHeld) << Bit++;
			}
		}

		SendState(Pad, HeldButtons, Axes, Now);
	}
}

void FSDLGamepadDevice::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
	ForEachPadOfController(ControllerId, [ChannelType, Value](FPad& Pad)
	{
		FForceFeedbackValues Values = Pad.ForceFeedback;
		switch (ChannelType)
		{
		case FForceFeedbackChannelType::LEFT_LARGE: Values.LeftLarge = Value; break;
		case FForceFeedbackChannelType::LEFT_SMALL: Values.LeftSmall = Value; break;
		case FForceFeedbackChannelType::RIGHT_LARGE: Values.RightLarge = Value; break;
		case FForceFeedbackChannelType::RIGHT_SMALL: Values.RightSmall = Value; break;
		}
		Rumble(Pad, Values);
	});
}

void FSDLGamepadDevice::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
{
	ForEachPadOfController(ControllerId, [&Values](FPad& Pad)
	{
		Rumble(Pad, Values);
	});
}

void FSDLGamepadDevice::SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property)
{
	if (!Property || Property->Name != FInputDeviceLightColorProperty::PropertyName())
	{
		return;
	}

	const FInputDeviceLightColorProperty* Light = static_cast<const FInputDeviceLightColorProperty*>(Property);
	const FColor Color = Light->bEnable ? Light->Color : FColor::Black;
	ForEachPadOfController(ControllerId, [Color](FPad& Pad)
	{
		SDL_SetGamepadLED(Pad.Gamepad, Color.R, Color.G, Color.B);
	});
}

void FSDLGamepadDevice::OpenPad(uint32 SDLId)
{
	if (Pads.ContainsByPredicate([SDLId](const FPad& Pad) { return Pad.SDLId == SDLId; }))
	{
		return;
	}

	SDL_Gamepad* Gamepad = SDL_OpenGamepad(SDLId);
	if (!Gamepad)
	{
		UE_LOG(LogSDLGamepad, Warning, TEXT("Could not open gamepad %u: %hs"), SDLId, SDL_GetError());
		return;
	}

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	FPad& Pad = Pads.AddDefaulted_GetRef();
	Pad.Gamepad = Gamepad;
	Pad.SDLId = SDLId;
	Pad.HardwareId = UE::SDLGamepad::GetHardwareId(Gamepad);
	Pad.DeviceId = DeviceMapper.AllocateNewInputDeviceId();

	// Inside the scope, listeners of the connection change can tell which hardware it is before any input
	FInputDeviceScope InputScope(this, UE::SDLGamepad::InputClassName, static_cast<int32>(SDLId), Pad.HardwareId);
	DeviceMapper.Internal_MapInputDeviceToUser(Pad.DeviceId, DeviceMapper.GetPlatformUserForNewlyConnectedDevice(), EInputDeviceConnectionState::Connected);

	UE_LOG(LogSDLGamepad, Log, TEXT("Connected '%hs' (%04x:%04x) as '%s', InputDeviceId %d"), SDL_GetGamepadName(Gamepad), SDL_GetGamepadVendor(Gamepad), SDL_GetGamepadProduct(Gamepad), *Pad.HardwareId, Pad.DeviceId.GetId());
}

void FSDLGamepadDevice::ClosePad(uint32 SDLId)
{
	const int32 Index = Pads.IndexOfByPredicate([SDLId](const FPad& Pad) { return Pad.SDLId == SDLId; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	FPad& Pad = Pads[Index];
	const int32 ReleasedAxes[NumAxes] = {};
	SendState(Pad, 0, ReleasedAxes, 0.0);

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	DeviceMapper.Internal_MapInputDeviceToUser(Pad.DeviceId, DeviceMapper.GetUserForUnpairedInputDevices(), EInputDeviceConnectionState::Disconnected);

	UE_LOG(LogSDLGamepad, Log, TEXT("Disconnected '%s', InputDeviceId %d"), *Pad.HardwareId, Pad.DeviceId.GetId());

	SDL_CloseGamepad(Pad.Gamepad);
	Pads.RemoveAtSwap(Index);
}

void FSDLGamepadDevice::SendState(FPad& Pad, uint32 HeldButtons, const int32* Axes, double Now)
{
	using namespace UE::SDLGamepad;

	// Slate creates a user for any index it is sent, so unpaired pads stay silent
	if (Pad.UserId.IsValid())
	{
		FInputDeviceScope InputScope(this, InputClassName, static_cast<int32>(Pad.SDLId), Pad.HardwareId);

		const FGamepadKeyNames::Type* AxisKeys = GetAxisKeys();
		for (int32 Axis = 0; Axis < NumAxes; ++Axis)
		{
			if (Axes[Axis] != Pad.Axes[Axis] || FMath::Abs(Axes[Axis]) > AxisDeadZones[Axis])
			{
				MessageHandler->OnControllerAnalog(AxisKeys[Axis], Pad.UserId, Pad.DeviceId, NormalizeAxis(Axis, Axes[Axis]));
			}
		}

		const FGamepadKeyNames::Type* ButtonKeys = GetButtonKeys();
		const uint32 ChangedButtons = HeldButtons ^ Pad.HeldButtons;
		for (int32 Button = 0; Button < NumButtons; ++Button)
		{
			const uint32 Bit = 1u << Button;
			if (ButtonKeys[Button].IsNone())
			{
				continue;
			}

			if (ChangedButtons & Bit)
			{
				if (HeldButtons & Bit)
				{
					MessageHandler->OnControllerButtonPressed(ButtonKeys[Button], Pad.UserId, Pad.DeviceId, false);
					Pad.RepeatTimes[Button] = Now + InitialButtonRepeatDelay;
				}
				else
				{
					MessageHandler->OnControllerButtonReleased(ButtonKeys[Button], Pad.UserId, Pad.DeviceId, false);
				}
			}
			else if ((HeldButtons & Bit) && Now >= Pad.RepeatTimes[Button])
			{
				MessageHandler->OnControllerButtonPressed(ButtonKeys[Button], Pad.UserId, Pad.DeviceId, true);
				Pad.RepeatTimes[Button] = Now + ButtonRepeatDelay;
			}
		}
	}

	Pad.HeldButtons = HeldButtons;
	FMemory::Memcpy(Pad.Axes, Axes, sizeof(Pad.Axes));
}

void FSDLGamepadDevice::Rumble(FPad& Pad, const FForceFeedbackValues& Values)
{
	using namespace UE::SDLGamepad;

	Pad.ForceFeedback = Values;

	// XInput's mix: each side drives one motor; trigger motors are not used
	const Uint16 LowFrequency = ToMotorSpeed(FMath::Max(Values.LeftLarge, Values.LeftSmall));
	const Uint16 HighFrequency = ToMotorSpeed(FMath::Max(Values.RightLarge, Values.RightSmall));
	SDL_RumbleGamepad(Pad.Gamepad, LowFrequency, HighFrequency, RumbleDurationMs);
}