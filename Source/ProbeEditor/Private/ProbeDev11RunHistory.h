#pragma once

#include "ProbeTestRunner.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Project-local, bounded, versioned run history. No UObjects or asset modifications.
// All entry points are invoked on the Editor game thread, never from an async IO task.
namespace ProbeDev11RunHistory
{
    static constexpr int32 RetainedRuns = 30; // Legacy Dev11 retention default for existing source-level tests.
    static constexpr int32 DefaultSavedRuns = 10;
    static constexpr int32 MaximumSavedRuns = 50;
    static constexpr int64 MaxHistoryBytes = 64LL * 1024 * 1024;
    static constexpr int32 MaxTests = 2000;
    static constexpr int32 MaxStepsPerTest = 2000;
    static constexpr int64 MaxFileBytes = 12 * 1024 * 1024;

    struct FSnapshot
    {
        FDateTime TimestampUtc;
        FString EngineVersion;
        FString PackPath; // Optional Dev12 provenance; old history files remain readable.
        TArray<Probe::FTestResult> Results;
    };

    inline FString DefaultDirectory()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("History"));
    }

    inline FString StatusString(Probe::EResultStatus Status) { return Probe::StatusName(Status); }

    inline bool ParseStatus(const FString& Text, Probe::EResultStatus& Out)
    {
        if (Text == TEXT("PASSED")) { Out = Probe::EResultStatus::Passed; return true; }
        if (Text == TEXT("FAILED")) { Out = Probe::EResultStatus::Failed; return true; }
        if (Text == TEXT("SKIPPED")) { Out = Probe::EResultStatus::Skipped; return true; }
        if (Text == TEXT("ERROR")) { Out = Probe::EResultStatus::Error; return true; }
        return false;
    }

    inline TSharedRef<FJsonObject> ToJson(const FSnapshot& Snapshot)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), TEXT("ProbeRunHistory/1"));
        Root->SetStringField(TEXT("timestamp_utc"), Snapshot.TimestampUtc.ToIso8601());
        Root->SetStringField(TEXT("engine_version"), Snapshot.EngineVersion);
        Root->SetStringField(TEXT("pack_path"), Snapshot.PackPath);
        TArray<TSharedPtr<FJsonValue>> Tests;
        for (const Probe::FTestResult& Result : Snapshot.Results)
        {
            TSharedRef<FJsonObject> Test = MakeShared<FJsonObject>();
            Test->SetStringField(TEXT("name"), Result.Name);
            Test->SetStringField(TEXT("id"), Result.Id);
            Test->SetStringField(TEXT("asset_path"), Result.AssetPath);
            Test->SetNumberField(TEXT("variation_index"), Result.VariationIndex);
            Test->SetStringField(TEXT("variation_name"), Result.VariationName);
            Test->SetStringField(TEXT("status"), StatusString(Result.Status));
            Test->SetStringField(TEXT("detail"), Result.Detail);
            Test->SetNumberField(TEXT("duration_ms"), Result.DurationMs);
            Test->SetNumberField(TEXT("attempts"), Result.Attempts);
            TArray<TSharedPtr<FJsonValue>> Steps;
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                TSharedRef<FJsonObject> StepJson = MakeShared<FJsonObject>();
                StepJson->SetNumberField(TEXT("index"), Step.Index);
                StepJson->SetStringField(TEXT("assertion_id"), Step.AssertionId);
                StepJson->SetStringField(TEXT("status"), StatusString(Step.Status));
                StepJson->SetStringField(TEXT("description"), Step.Description);
                StepJson->SetStringField(TEXT("group"), Step.GroupName);
                StepJson->SetStringField(TEXT("detail"), Step.Detail);
                StepJson->SetNumberField(TEXT("duration_ms"), Step.DurationMs);
                Steps.Add(MakeShared<FJsonValueObject>(StepJson));
            }
            Test->SetArrayField(TEXT("steps"), MoveTemp(Steps));
            Tests.Add(MakeShared<FJsonValueObject>(Test));
        }
        Root->SetArrayField(TEXT("tests"), MoveTemp(Tests));
        return Root;
    }

    inline bool FromJson(const TSharedPtr<FJsonObject>& Root, FSnapshot& Out)
    {
        if (!Root.IsValid()) { return false; }
        FString Format, Timestamp;
        const TArray<TSharedPtr<FJsonValue>>* Tests = nullptr;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("ProbeRunHistory/1")
            || !Root->TryGetStringField(TEXT("timestamp_utc"), Timestamp)
            || !FDateTime::ParseIso8601(*Timestamp, Out.TimestampUtc)
            || !Root->TryGetArrayField(TEXT("tests"), Tests)
            || !Tests || Tests->IsEmpty() || Tests->Num() > MaxTests)
        { return false; }
        Root->TryGetStringField(TEXT("engine_version"), Out.EngineVersion);
        Root->TryGetStringField(TEXT("pack_path"), Out.PackPath);
        TArray<Probe::FTestResult> Parsed;
        for (const TSharedPtr<FJsonValue>& Value : *Tests)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) { return false; }
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            if (!Object.IsValid()) { return false; }
            Probe::FTestResult Result;
            FString Status;
            if (!Object->TryGetStringField(TEXT("asset_path"), Result.AssetPath)
                || !Object->TryGetStringField(TEXT("status"), Status)
                || !ParseStatus(Status, Result.Status))
            { return false; }
            Object->TryGetStringField(TEXT("name"), Result.Name);
            Object->TryGetStringField(TEXT("id"), Result.Id);
            Object->TryGetStringField(TEXT("variation_name"), Result.VariationName);
            Object->TryGetStringField(TEXT("detail"), Result.Detail);
            double Number = 0;
            if (Object->TryGetNumberField(TEXT("variation_index"), Number)) { Result.VariationIndex = static_cast<int32>(Number); }
            if (Object->TryGetNumberField(TEXT("duration_ms"), Number)) { Result.DurationMs = Number; }
            if (Object->TryGetNumberField(TEXT("attempts"), Number)) { Result.Attempts = FMath::Max(1, static_cast<int32>(Number)); }
            const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
            if (Object->TryGetArrayField(TEXT("steps"), Steps) && Steps)
            {
                if (Steps->Num() > MaxStepsPerTest) { return false; }
                for (const TSharedPtr<FJsonValue>& StepValue : *Steps)
                {
                    if (!StepValue.IsValid() || StepValue->Type != EJson::Object) { return false; }
                    const TSharedPtr<FJsonObject> StepObject = StepValue->AsObject();
                    if (!StepObject.IsValid()) { return false; }
                    Probe::FStepResult Step;
                    if (!StepObject->TryGetStringField(TEXT("status"), Status)
                        || !ParseStatus(Status, Step.Status)) { return false; }
                    if (StepObject->TryGetNumberField(TEXT("index"), Number)) { Step.Index = static_cast<int32>(Number); }
                    StepObject->TryGetStringField(TEXT("assertion_id"), Step.AssertionId);
                    StepObject->TryGetStringField(TEXT("description"), Step.Description);
                    StepObject->TryGetStringField(TEXT("group"), Step.GroupName);
                    StepObject->TryGetStringField(TEXT("detail"), Step.Detail);
                    if (StepObject->TryGetNumberField(TEXT("duration_ms"), Number)) { Step.DurationMs = Number; }
                    Result.Steps.Add(MoveTemp(Step));
                }
            }
            Parsed.Add(MoveTemp(Result));
        }
        Out.Results = MoveTemp(Parsed);
        return true;
    }

    inline bool Serialize(const FSnapshot& Snapshot, FString& OutText)
    {
        if (Snapshot.Results.IsEmpty() || Snapshot.Results.Num() > MaxTests) { return false; }
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutText);
        return FJsonSerializer::Serialize(ToJson(Snapshot), Writer);
    }

    inline bool Deserialize(const FString& Text, FSnapshot& Out)
    {
        if (Text.IsEmpty() || Text.Len() > MaxFileBytes) { return false; }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        FSnapshot Parsed;
        if (!FJsonSerializer::Deserialize(Reader, Root) || !FromJson(Root, Parsed)) { return false; }
        Out = MoveTemp(Parsed);
        return true;
    }

    inline TArray<FString> ListPaths(const FString& Directory)
    {
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *FPaths::Combine(Directory, TEXT("ProbeHistory-*.json")), true, false);
        Names.Sort([](const FString& A, const FString& B) { return A > B; });
        TArray<FString> Paths;
        for (const FString& Name : Names) { Paths.Add(FPaths::Combine(Directory, Name)); }
        return Paths;
    }

    inline bool Read(const FString& Path, FSnapshot& Out)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        if (Size <= 0 || Size > MaxFileBytes) { return false; }
        FString Json;
        return FFileHelper::LoadFileToString(Json, *Path) && Deserialize(Json, Out);
    }

    inline bool ReadLatest(const FString& Directory, FSnapshot& Out)
    {
        for (const FString& Path : ListPaths(Directory))
        { if (Read(Path, Out)) { return true; } }
        return false;
    }

    // Only files created by Probe's history writer participate in retention. Never
    // delete unrelated files in Saved/Probe/History, including pinned baselines.
    inline void Prune(const FString& Directory, int32 MaxSavedRuns = DefaultSavedRuns,
        int64 MaxTotalBytes = MaxHistoryBytes)
    {
        const TArray<FString> Paths = ListPaths(Directory);
        const int32 Limit = FMath::Clamp(MaxSavedRuns, 0, MaximumSavedRuns);
        int64 Bytes = 0;
        int32 Kept = 0;
        for (const FString& Path : Paths)
        {
            const int64 Size = IFileManager::Get().FileSize(*Path);
            if (Kept < Limit && Size > 0 && Size <= MaxFileBytes
                && Bytes <= MaxTotalBytes - Size)
            { Bytes += Size; ++Kept; }
            else { IFileManager::Get().Delete(*Path, false, false, true); }
        }
    }

    inline bool Save(const FString& Directory, const FSnapshot& Snapshot, FString* OutPath = nullptr,
        int32 MaxSavedRuns = RetainedRuns)
    {
        FString Json;
        if (!Serialize(Snapshot, Json) || Json.Len() > MaxFileBytes) { return false; }
        if (!IFileManager::Get().MakeDirectory(*Directory, true)) { return false; }
        const FString Name = FString::Printf(TEXT("ProbeHistory-%s-%s.json"),
            *Snapshot.TimestampUtc.ToString(TEXT("%Y%m%d-%H%M%S")),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
        const FString Path = FPaths::Combine(Directory, Name);
        if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        { return false; }
        if (IFileManager::Get().FileSize(*Path) > MaxFileBytes)
        {
            IFileManager::Get().Delete(*Path, false, false, true);
            return false;
        }
        if (OutPath) { *OutPath = Path; }
        Prune(Directory, MaxSavedRuns);
        return true;
    }

    inline FString Label(const FSnapshot& Snapshot)
    {
        int32 Passed = 0, Problems = 0;
        for (const Probe::FTestResult& Result : Snapshot.Results)
        {
            if (Result.Status == Probe::EResultStatus::Passed) { ++Passed; }
            else if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error) { ++Problems; }
        }
        return FString::Printf(TEXT("%s  |  %d tests, %d passed, %d problems"),
            *Snapshot.TimestampUtc.ToString(TEXT("%Y-%m-%d %H:%M UTC")), Snapshot.Results.Num(), Passed, Problems);
    }
}
