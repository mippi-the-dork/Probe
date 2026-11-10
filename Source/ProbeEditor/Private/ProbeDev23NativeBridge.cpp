#include "ProbeDev23NativeBridge.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformTime.h"

namespace ProbeDev23
{
static bool ReadBrace(const FString& Line, const TCHAR* Prefix, FString& Value)
{
    const FString Token = FString(Prefix) + TEXT("={");
    const int32 At = Line.Find(Token, ESearchCase::CaseSensitive);
    if (At == INDEX_NONE) { return false; }
    const int32 First = At + Token.Len();
    const int32 Last = Line.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, First);
    if (Last == INDEX_NONE || Last <= First) { return false; }
    Value = Line.Mid(First, Last - First);
    return true;
}
bool ParseStarted(const FString& Line, FString& Path)
{
    return Line.Contains(TEXT("Test Started.")) && ReadBrace(Line, TEXT("Path"), Path);
}
bool ParseCompleted(const FString& Line, FString& Path, FString& Outcome)
{
    return Line.Contains(TEXT("Test Completed.")) && ReadBrace(Line, TEXT("Path"), Path)
        && ReadBrace(Line, TEXT("Result"), Outcome);
}
Probe::EResultStatus StatusFromOutcome(const FString& Outcome)
{
    if (Outcome.Equals(TEXT("Success"), ESearchCase::IgnoreCase)
        || Outcome.Equals(TEXT("Passed"), ESearchCase::IgnoreCase)) { return Probe::EResultStatus::Passed; }
    if (Outcome.Equals(TEXT("Skipped"), ESearchCase::IgnoreCase)
        || Outcome.Equals(TEXT("NotRun"), ESearchCase::IgnoreCase)) { return Probe::EResultStatus::Skipped; }
    if (Outcome.Equals(TEXT("Fail"), ESearchCase::IgnoreCase)
        || Outcome.Equals(TEXT("Failed"), ESearchCase::IgnoreCase)) { return Probe::EResultStatus::Failed; }
    return Probe::EResultStatus::Error;
}
bool FNativeLogBridge::Start(const FString& FullPath)
{
    if (FullPath.IsEmpty() || !GLog) { return false; }
    Stop();
    {
        FScopeLock Lock(&Guard);
        Current = FObservedRun();
        Current.Path = FullPath;
        StartSeconds = FPlatformTime::Seconds();
        TestStartSeconds = 0;
        bListening = true;
    }
    GLog->AddOutputDevice(this);
    return true;
}
void FNativeLogBridge::Stop()
{
    if (GLog) { GLog->RemoveOutputDevice(this); }
    FScopeLock Lock(&Guard);
    bListening = false;
}
bool FNativeLogBridge::Read(FObservedRun& Out) const
{
    FScopeLock Lock(&Guard);
    Out = Current;
    return Out.bCompleted;
}
void FNativeLogBridge::Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category)
{
    // Only authoritative controller messages, never another plugin's free-form logs.
    if (Category != FName(TEXT("LogAutomationController")) || !V) { return; }
    const FString Line(V);
    FString Path, Outcome;
    const bool Started = ParseStarted(Line, Path);
    const bool Completed = ParseCompleted(Line, Path, Outcome);
    FScopeLock Lock(&Guard);
    if (!bListening || Current.bCompleted) { return; }
    if (Started && Path == Current.Path)
    {
        Current.bStarted = true;
        TestStartSeconds = FPlatformTime::Seconds();
    }
    if (Completed && Path == Current.Path)
    {
        Current.bStarted = true;
        Current.bCompleted = true;
        Current.Outcome = Outcome;
        const double Since = TestStartSeconds > 0 ? TestStartSeconds : StartSeconds;
        Current.DurationMs = FMath::Max(0.0, (FPlatformTime::Seconds() - Since) * 1000.0);
    }
    else if (Current.bStarted && Current.Events.Num() < 64
        && (Line.Contains(TEXT("BeginEvents:")) || Line.Contains(TEXT("EndEvents:"))
            || Line.Contains(TEXT("Error:")) || Line.Contains(TEXT("Warning:"))))
    { Current.Events.Add(Line.Left(1000)); }
}
}
