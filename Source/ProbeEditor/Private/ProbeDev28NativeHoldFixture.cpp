#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
// Explicitly outside the "Probe" prefix so Automation RunTests Probe does not
// spend eight seconds on this manual cancellation fixture.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev28NativeHold,
    "VerificationFixtures.Probe.NativeHoldEightSeconds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev28NativeHold::RunTest(const FString&)
{
    const double StartedAt = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([StartedAt]() -> bool
    {
        return (FPlatformTime::Seconds() - StartedAt) >= 8.0;
    }));
    return true;
}
#endif
