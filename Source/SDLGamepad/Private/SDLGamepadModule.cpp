// Copyright 2026 kirzo

#include "IInputDeviceModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "SDLGamepadDevice.h"

class FSDLGamepadModule : public IInputDeviceModule
{
public:
	virtual void StartupModule() override
	{
		BaseDir = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("SDLGamepad"))->GetBaseDir());

		// SDL3 is delay-loaded, so it must be in memory before the first SDL call
		const FString DllPath = FPaths::Combine(BaseDir, TEXT("Binaries/ThirdParty/SDL3/Win64/SDL3.dll"));
		if (!FPlatformProcess::GetDllHandle(*DllPath))
		{
			UE_LOG(LogSDLGamepad, Error, TEXT("Could not load %s, gamepads are disabled"), *DllPath);
			return;
		}

		IInputDeviceModule::StartupModule();
	}

	virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
	{
		return MakeShared<FSDLGamepadDevice>(InMessageHandler, FPaths::Combine(BaseDir, TEXT("Resources/SDL_GameControllerDB/gamecontrollerdb.txt")));
	}

private:
	FString BaseDir;
};

IMPLEMENT_MODULE(FSDLGamepadModule, SDLGamepad)