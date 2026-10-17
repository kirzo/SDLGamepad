// Copyright 2026 kirzo

#pragma once

#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSDLGamepad, Log, All);

struct SDL_Gamepad;

/** Reads gamepads through SDL3 and sends them to the message handler as the standard Gamepad_* keys. */
class FSDLGamepadDevice : public IInputDevice
{
public:
	/** MappingsFile: SDL_GameControllerDB-format mappings loaded on top of SDL's built-in ones. */
	FSDLGamepadDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler, const FString& MappingsFile);
	virtual ~FSDLGamepadDevice() override;

	//~ Begin IInputDevice
	virtual void Tick(float DeltaTime) override {}
	virtual void SendControllerEvents() override;
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override { MessageHandler = InMessageHandler; }
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;
	virtual void SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property) override;
	virtual bool IsGamepadAttached() const override { return !Pads.IsEmpty(); }
	//~ End IInputDevice

private:
	static constexpr int32 NumAxes = 6;

	/** SDL's 15 positional buttons plus the 10 digital buttons UE derives from triggers and sticks. */
	static constexpr int32 NumButtons = 25;

	struct FPad
	{
		SDL_Gamepad* Gamepad = nullptr;
		uint32 SDLId = 0;
		FString HardwareId;
		FInputDeviceId DeviceId;
		FPlatformUserId UserId;
		uint32 HeldButtons = 0;
		int32 Axes[NumAxes] = {};
		double RepeatTimes[NumButtons] = {};
		FForceFeedbackValues ForceFeedback;
	};

	void OpenPad(uint32 SDLId);
	void ClosePad(uint32 SDLId);

	/** Diffs the new state against the last one sent and emits the analog, press, release and repeat events. */
	void SendState(FPad& Pad, uint32 HeldButtons, const int32* Axes, double Now);

	static void Rumble(FPad& Pad, const FForceFeedbackValues& Values);

	/** Runs Func on every pad owned by the platform user behind ControllerId. */
	template<typename FuncType> void ForEachPadOfController(int32 ControllerId, FuncType Func);

	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	TArray<FPad> Pads;
	float InitialButtonRepeatDelay = 0.2f;
	float ButtonRepeatDelay = 0.1f;
	bool bInitialized = false;
};