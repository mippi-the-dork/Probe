#pragma once

#include "ProbeTestAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Clipboard-only, versioned reusable step groups. No additional assets,
// runtime plugin dependencies, permission fields, or implicit activation.
namespace ProbeStepGroups
{
    struct FRange
    {
        int32 First = INDEX_NONE;
        int32 Last = INDEX_NONE;
        int32 Num() const { return Last >= First && First >= 0 ? Last - First + 1 : 0; }
        bool IsValid() const { return Num() > 0; }
    };

    inline FRange FindContiguous(const TArray<FProbeTestStep>& Steps, int32 Index)
    {
        FRange Range;
        if (!Steps.IsValidIndex(Index)) { return Range; }
        Range.First = Range.Last = Index;
        const FString Name = Steps[Index].GroupName.TrimStartAndEnd();
        if (Name.IsEmpty()) { return Range; }
        while (Range.First > 0 && Steps[Range.First - 1].GroupName.TrimStartAndEnd() == Name) { --Range.First; }
        while (Range.Last + 1 < Steps.Num() && Steps[Range.Last + 1].GroupName.TrimStartAndEnd() == Name) { ++Range.Last; }
        return Range;
    }

    inline bool Encode(const TArray<FProbeTestStep>& Steps, const FRange& Range, FString& Json, FString& Error)
    {
        Json.Reset(); Error.Reset();
        if (!Range.IsValid() || Range.Num() > 256 || !Steps.IsValidIndex(Range.First) || !Steps.IsValidIndex(Range.Last))
        { Error = TEXT("Invalid group range (maximum 256 steps)."); return false; }
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), TEXT("ProbeStepGroup/1"));
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (int32 I = Range.First; I <= Range.Last; ++I)
        {
            FString Data;
            if (!FJsonObjectConverter::UStructToJsonObjectString(FProbeTestStep::StaticStruct(), &Steps[I], Data, 0, 0, 0, nullptr, false))
            { Error = TEXT("Step serialization failed."); return false; }
            TSharedPtr<FJsonObject> Object;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
            if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
            { Error = TEXT("Step JSON invalid."); return false; }
            Entries.Add(MakeShared<FJsonValueObject>(Object));
        }
        Root->SetArrayField(TEXT("steps"), Entries);
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        if (!FJsonSerializer::Serialize(Root, Writer) || Json.Len() > 131072)
        { Error = TEXT("Group serialization exceeds 128 KiB or failed."); Json.Reset(); return false; }
        return true;
    }

    inline bool Decode(const FString& Json, TArray<FProbeTestStep>& Out, FString& Error)
    {
        Out.Reset(); Error.Reset();
        if (Json.Len() > 131072 || Json.IsEmpty())
        { Error = TEXT("Clipboard empty or group exceeds 128 KiB."); return false; }
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        { Error = TEXT("Clipboard does not contain valid JSON."); return false; }
        FString Format;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("ProbeStepGroup/1")
            || !Root->TryGetArrayField(TEXT("steps"), Entries) || !Entries
            || Entries->IsEmpty() || Entries->Num() > 256)
        { Error = TEXT("Not a ProbeStepGroup/1 clipboard block, or invalid step count."); return false; }
        TArray<FProbeTestStep> Pending;
        for (const TSharedPtr<FJsonValue>& Entry : *Entries)
        {
            if (!Entry.IsValid() || Entry->Type != EJson::Object)
            { Error = TEXT("Group contains an invalid step."); return false; }
            FString Data;
            const TSharedRef<TJsonWriter<>> StepWriter = TJsonWriterFactory<>::Create(&Data);
            if (!FJsonSerializer::Serialize(Entry->AsObject().ToSharedRef(), StepWriter))
            { Error = TEXT("Step cannot be serialized."); return false; }
            FProbeTestStep Step;
            if (!FJsonObjectConverter::JsonObjectStringToUStruct(Data, &Step, 0, 0, false, nullptr, nullptr))
            { Error = TEXT("Step cannot be decoded into Probe schema."); return false; }
            // Validate enum ordinal to avoid malformed clipboard data producing
            // undefined assertion catalog indexing after authoring.
            const UEnum* Enum = StaticEnum<EProbeStepType>();
            if (!Enum || !Enum->IsValidEnumValue(static_cast<int64>(Step.Type)))
            { Error = TEXT("Step references an unsupported assertion type."); return false; }
            Pending.Add(MoveTemp(Step));
        }
        Out = MoveTemp(Pending);
        return true;
    }
}
